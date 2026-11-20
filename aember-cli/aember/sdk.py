"""The aember-resources SDKs: where they are, how to get one, how to use it.

An SDK is the Poky cross toolchain plus the target sysroot of one aember
architecture, published as an installer (`aember-sdk-<arch>-v<V>.sh`) in the
aember-resources GitHub release. It is installed into
`<resources>/<arch>/sdk/`, where `<resources>` is `build/aember-resources`
of this repo or `$AEMBER_RESOURCES_DIR` (point it at aember's
`build/aember-resources` to share the SDKs aember already fetched).
"""

from __future__ import annotations

import logging
import os
import shutil
import subprocess
import urllib.request
from pathlib import Path

from aember import project

log = logging.getLogger("aember.sdk")

# aember architecture → target sysroot name inside the SDK (environment-setup
# is "environment-setup-<sysroot>").
SYSROOTS: dict[str, str] = {
    "x64-poky-linux": "core2-64-poky-linux",
    "aarch64-poky-linux": "cortexa53-poky-linux",
    "arm-poky-linux": "armv7at2hf-neon-poky-linux-gnueabi",
    "riscv64-poky-linux": "riscv64imafdc-poky-linux",
    "x64-poky-linux-musl": "core2-64-poky-linux-musl",
    "aarch64-poky-linux-musl": "cortexa53-poky-linux-musl",
    "arm-poky-linux-musl": "armv7at2hf-neon-poky-linux-musleabi",
    "riscv64-poky-linux-musl": "riscv64imafdc-poky-linux-musl",
}
ARCHS = list(SYSROOTS)


def check_arch(arch: str) -> str:
    if arch not in SYSROOTS:
        raise ValueError(f"Unknown architecture '{arch}'. Known: {ARCHS}")
    return arch


def resources_dir(root: Path) -> Path:
    env = os.environ.get("AEMBER_RESOURCES_DIR")
    return Path(env) if env else root / "build" / "aember-resources"


def sdk_dir(root: Path, arch: str) -> Path:
    return resources_dir(root) / check_arch(arch) / "sdk"


def env_script(root: Path, arch: str) -> Path:
    return sdk_dir(root, arch) / f"environment-setup-{SYSROOTS[arch]}"


def sysroot(root: Path, arch: str) -> Path:
    return sdk_dir(root, arch) / "sysroots" / SYSROOTS[arch]


def installer_name(arch: str, version: str = project.RESOURCES_VERSION) -> str:
    return f"aember-sdk-{arch}-v{version}.sh"


def installer_url(arch: str, version: str = project.RESOURCES_VERSION) -> str:
    base = os.environ.get("AEMBER_RESOURCES_URL") or (
        f"https://github.com/{project.RESOURCES_REPO}/releases/download/v{version}"
    )
    return f"{base.rstrip('/')}/{installer_name(arch, version)}"


def is_installed(root: Path, arch: str) -> bool:
    return env_script(root, arch).is_file()


def fetch(root: Path, arch: str, force: bool = False) -> Path:
    """Download and run the SDK installer unless the SDK is there already."""
    out = sdk_dir(root, arch)
    if is_installed(root, arch) and not force:
        log.info("SDK for %s present: %s", arch, out)
        return out
    downloads = root / "build" / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    installer = downloads / installer_name(arch)
    if not installer.is_file():
        url = installer_url(arch)
        log.info("Downloading %s", url)
        part = installer.with_suffix(".part")
        with urllib.request.urlopen(url) as resp, part.open("wb") as f:
            shutil.copyfileobj(resp, f)
        part.rename(installer)
    shutil.rmtree(out, ignore_errors=True)
    log.info("Installing the SDK into %s", out)
    subprocess.run(["sh", str(installer), "-d", str(out), "-y"], check=True)
    installer.unlink()  # ~600 MB each: the installed SDK is what counts
    return out


def shell(root: Path, arch: str, command: str) -> str:
    """A bash command line that runs @p command in the SDK's environment.

    LD_LIBRARY_PATH is cleared (the SDK refuses to run with one set).
    """
    script = env_script(root, arch)
    if not script.is_file():
        raise FileNotFoundError(
            f"No SDK for {arch} at {sdk_dir(root, arch)}; run `aember sdk -a {arch}`."
        )
    return f"unset LD_LIBRARY_PATH; . '{script}' >/dev/null && {command}"
