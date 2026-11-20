"""Builds: cross builds with an SDK (`build`), host builds for the unit
tests (`test`), and packing the staged install as an aember container
(`pack`) with the aember-build-helper submodule."""

from __future__ import annotations

import json
import logging
import os
import shlex
import subprocess
from pathlib import Path

from aember import project, sdk

log = logging.getLogger("aember.build")

BUILD_HELPER = Path("tools") / "aember-build-helper" / "aember-cli" / "run.py"


def jobs() -> int:
    """2/3 of the CPUs (all of them crashes WSL), or $AEMBER_BUILD_JOBS."""
    env = os.environ.get("AEMBER_BUILD_JOBS")
    if env:
        return max(1, int(env))
    return max(1, (os.cpu_count() or 3) * 2 // 3)


def build_dir(root: Path, arch: str, build_type: str) -> Path:
    return root / "build" / "configure" / arch / build_type


def stage_dir(root: Path, arch: str) -> Path:
    return root / "build" / "stage" / arch


def out_dir(root: Path, arch: str) -> Path:
    return root / "build" / "aember-out" / arch


def run(
    cmd: list[str] | str,
    cwd: Path,
    env: dict[str, str] | None = None,
    shell_cmd: bool = False,
) -> None:
    log.debug("$ %s", cmd if shell_cmd else shlex.join(cmd))
    if shell_cmd:
        subprocess.run(["bash", "-c", cmd], cwd=cwd, env=env, check=True)
    else:
        subprocess.run(cmd, cwd=cwd, env=env, check=True)


def cross_build(root: Path, arch: str, build_type: str = "Release") -> Path:
    """Configure, build and install into build/stage/<arch>/ with the SDK."""
    bdir = build_dir(root, arch, build_type)
    stage = stage_dir(root, arch)
    toolchain = "$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake"
    configure = shlex.join(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(bdir),
            "-G",
            "Ninja",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            "-DBUILD_TESTS=OFF",
        ]
    )
    commands = " && ".join(
        [
            f'{configure} "-DCMAKE_TOOLCHAIN_FILE={toolchain}"',
            f"cmake --build {shlex.quote(str(bdir))} --parallel {jobs()}",
            f"rm -rf {shlex.quote(str(stage))}",
            f"cmake --install {shlex.quote(str(bdir))} --prefix {shlex.quote(str(stage))}",
        ]
    )
    run(sdk.shell(root, arch, commands), root, shell_cmd=True)
    log.info("Installed %s for %s into %s", project.NAME, arch, stage)
    return stage


SANITIZE_FLAGS = "-fsanitize=address,undefined -fno-omit-frame-pointer"
SANITIZER_ENV = {
    "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
    "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
}


def host_tests(root: Path, sanitize: bool = False) -> None:
    """Build the unit tests with the host compiler and run them
    (@p sanitize: ASan + UBSan)."""
    variant = "Debug-asan" if sanitize else "Debug"
    bdir = root / "build" / "configure" / "host" / variant
    flags = {"CXXFLAGS": SANITIZE_FLAGS, "LDFLAGS": SANITIZE_FLAGS} if sanitize else {}
    env = {**os.environ, **flags}
    run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(bdir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DBUILD_TESTS=ON",
        ],
        root,
        env,
    )
    run(["cmake", "--build", str(bdir), "--parallel", str(jobs())], root)
    subprocess.run(
        ["ctest", "--test-dir", str(bdir), "--output-on-failure"],
        cwd=root,
        env={**os.environ, **(SANITIZER_ENV if sanitize else {})},
        check=True,
    )


TEST_ARCH = "x64-poky-linux"
# glibc's loader in the x64 SDK sysroot (runs the SDK's binaries on the host)
TEST_LOADER = "ld-linux-x86-64.so.2"


def sdk_tests(root: Path, sanitize: bool = False) -> None:
    """Build the unit tests with the x64 SDK and run each through the SDK's
    loader, with the sysroot's libraries and Qt plugins (offscreen).
    @p sanitize: ASan + UBSan (the SDK's gcc-sanitizers, aember-resources
    v0.1.8+)."""
    bdir = build_dir(root, TEST_ARCH, "Debug-tests-asan" if sanitize else "Debug-tests")
    # The SDK's environment-setup sets CXXFLAGS/LDFLAGS: append to them.
    san = (
        f'export CXXFLAGS="$CXXFLAGS {SANITIZE_FLAGS}" '
        f'LDFLAGS="$LDFLAGS {SANITIZE_FLAGS}" && '
        if sanitize
        else ""
    )
    toolchain = "$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake"
    configure = shlex.join(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(bdir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DBUILD_TESTS=ON",
        ]
    )
    run(
        sdk.shell(
            root,
            TEST_ARCH,
            f'{san}{configure} "-DCMAKE_TOOLCHAIN_FILE={toolchain}" && '
            f"cmake --build {shlex.quote(str(bdir))} --parallel {jobs()}",
        ),
        root,
        shell_cmd=True,
    )
    sysroot = sdk.sysroot(root, TEST_ARCH)
    libs = ":".join(str(sysroot / d) for d in ("lib", "usr/lib"))
    env = {
        **os.environ,
        "QT_QPA_PLATFORM": "offscreen",
        "QT_PLUGIN_PATH": str(sysroot / "usr" / "lib" / "plugins"),
        **(SANITIZER_ENV if sanitize else {}),
    }
    env.pop("LD_LIBRARY_PATH", None)
    tests = sorted(p for p in bdir.rglob("*-unittest") if p.is_file())
    if not tests:
        raise FileNotFoundError(f"No test binaries in {bdir}")
    for test in tests:
        log.info("Running %s", test.name)
        subprocess.run(
            [str(sysroot / "lib" / TEST_LOADER), "--library-path", libs, str(test)],
            cwd=root,
            env=env,
            check=True,
        )


def container_manifest(root: Path, arch: str) -> Path:
    """aember-container.json for @p arch: the repo's manifest plus the arch
    and the SDK sysroot the libraries come from."""
    manifest = json.loads((root / "aember-container.json").read_text())
    manifest["arch"] = arch
    manifest["rootfs"] = {"dir": str(stage_dir(root, arch))}
    manifest.setdefault("libs", {})["sysroot"] = str(sdk.sysroot(root, arch))
    files = []
    for f in manifest.get("files", []):
        # "{sysroot}/..." is a file from the SDK's target sysroot; other paths
        # are relative to the repo. "optional": skip it if the arch lacks it
        # (e.g. glibc's locale archive on musl).
        src = f["src"].replace("{sysroot}", str(sdk.sysroot(root, arch)))
        src_path = (root / src).resolve()
        if f.pop("optional", False) and not src_path.exists():
            log.info("%s: no %s, skipped", arch, src)
            continue
        f["src"] = str(src_path)
        files.append(f)
    if "files" in manifest:
        manifest["files"] = files
    path = root / "build" / "manifests" / arch / "aember-container.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    return path


def pack(root: Path, arch: str) -> Path:
    """Pack build/stage/<arch>/ into build/aember-out/<arch>/<name>/."""
    helper = root / BUILD_HELPER
    if not helper.is_file():
        raise FileNotFoundError(f"{helper} is missing: git submodule update --init")
    if not stage_dir(root, arch).is_dir():
        raise FileNotFoundError(
            f"Nothing staged for {arch}: run `aember build -a {arch}`"
        )
    out = out_dir(root, arch)
    run(
        [
            "python3",
            str(helper),
            "build",
            "--manifest",
            str(container_manifest(root, arch)),
            "--out",
            str(out),
        ],
        root,
    )
    return out / project.NAME


def install(root: Path, arch: str, target: Path) -> None:
    """Put the packed container into an aember checkout's overlay."""
    run(
        [
            "python3",
            str(root / BUILD_HELPER),
            "install",
            str(target),
            "--manifest",
            str(container_manifest(root, arch)),
            "--arch",
            arch,
            "--out",
            str(out_dir(root, arch)),
        ],
        root,
    )
