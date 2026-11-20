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


def run(cmd: list[str] | str, cwd: Path, shell_cmd: bool = False) -> None:
    log.debug("$ %s", cmd if shell_cmd else shlex.join(cmd))
    if shell_cmd:
        subprocess.run(["bash", "-c", cmd], cwd=cwd, check=True)
    else:
        subprocess.run(cmd, cwd=cwd, check=True)


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


def host_tests(root: Path) -> None:
    """Build the unit tests with the host compiler and run them."""
    bdir = root / "build" / "configure" / "host" / "Debug"
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
    )
    run(["cmake", "--build", str(bdir), "--parallel", str(jobs())], root)
    run(["ctest", "--test-dir", str(bdir), "--output-on-failure"], root)


def container_manifest(root: Path, arch: str) -> Path:
    """aember-container.json for @p arch: the repo's manifest plus the arch
    and the SDK sysroot the libraries come from."""
    manifest = json.loads((root / "aember-container.json").read_text())
    manifest["arch"] = arch
    manifest["rootfs"] = {"dir": str(stage_dir(root, arch))}
    manifest.setdefault("libs", {})["sysroot"] = str(sdk.sysroot(root, arch))
    for f in manifest.get("files", []):  # paths are relative to the repo
        f["src"] = str((root / f["src"]).resolve())
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
            "--out",
            str(out_dir(root, arch)),
        ],
        root,
    )
