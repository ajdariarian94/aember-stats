#!/usr/bin/env python3
"""Run the repository's `aember` CLI without installing it."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from aember.cli import main  # noqa: E402

sys.exit(main())
