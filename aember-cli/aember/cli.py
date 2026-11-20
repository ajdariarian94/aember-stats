"""`aember`: the repository's command line (run it with aember-cli/run.py).

    aember sdk -a <arch>        fetch and install the aember-resources SDK
    aember build -a <arch>      cross-build and stage the install
    aember pack -a <arch>       pack the staged install as an aember container
    aember install -a <arch> <aember checkout>
    aember test                 unit tests (C++ on the host, and this CLI's)
    aember all -a <arch>        sdk + build + pack
"""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from pathlib import Path

from aember import build, project, sdk

log = logging.getLogger("aember")


def find_root() -> Path:
    """The repository root: the directory with aember-container.json."""
    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / "aember-container.json").is_file():
            return parent
    raise FileNotFoundError("aember-container.json not found above the CLI")


def cli_tests(root: Path) -> None:
    subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-t", "."],
        cwd=root / "aember-cli",
        check=True,
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="aember", description=f"{project.NAME} tools")
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="command", required=True)

    def arch_arg(p: argparse.ArgumentParser) -> None:
        p.add_argument("-a", "--arch", default="x64-poky-linux", choices=sdk.ARCHS)

    p = sub.add_parser("sdk", help="fetch and install an SDK")
    arch_arg(p)
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("build", help="cross-build and stage")
    arch_arg(p)
    p.add_argument("--build-type", default="Release", choices=["Release", "Debug"])
    p = sub.add_parser("pack", help="pack the staged install as a container")
    arch_arg(p)
    p = sub.add_parser("install", help="install the container into aember")
    arch_arg(p)
    p.add_argument("target", type=Path, help="aember checkout or rootfs overlay")
    p = sub.add_parser("all", help="sdk + build + pack")
    arch_arg(p)
    p = sub.add_parser("test", help="unit tests")
    p.add_argument("--no-cpp", action="store_true", help="only the CLI tests")

    args = ap.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
    root = find_root()
    try:
        if args.command == "sdk":
            sdk.fetch(root, args.arch, force=args.force)
        elif args.command == "build":
            build.cross_build(root, args.arch, args.build_type)
        elif args.command == "pack":
            log.info("Packed %s", build.pack(root, args.arch))
        elif args.command == "install":
            build.install(root, args.arch, args.target)
        elif args.command == "all":
            sdk.fetch(root, args.arch)
            build.cross_build(root, args.arch)
            log.info("Packed %s", build.pack(root, args.arch))
        elif args.command == "test":
            cli_tests(root)
            if not args.no_cpp:
                if project.TESTS == "sdk":
                    build.sdk_tests(root)
                else:
                    build.host_tests(root)
    except (FileNotFoundError, ValueError) as e:
        log.error("%s", e)
        return 1
    except subprocess.CalledProcessError as e:
        log.error("command failed (exit %s): %s", e.returncode, e.cmd)
        return 1
    return 0
