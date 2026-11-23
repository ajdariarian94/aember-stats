#!/usr/bin/env python3
"""The repository's `aember` CLI (sdk, build, pack, install, test, all):
the shared one from the aember-build-helper submodule, told what this
repository is. `aember-cli/run.py --help`."""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "aember-build-helper" / "project-cli"))
from aember_project import Project, main  # noqa: E402

NAME = "aember-stats"
# aember-resources release with the SDKs (compilers, sysroots).
RESOURCES_VERSION = "0.1.13"
TESTS = "host"  # host compiler: no target libraries needed

if __name__ == "__main__":
    sys.exit(main(Project(NAME, RESOURCES_VERSION, tests=TESTS, root=ROOT)))
