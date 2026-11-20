"""What makes this repository different from its siblings that share the CLI
(aember-stats, aember-monitor): the container name and what it needs."""

from __future__ import annotations

NAME = "aember-stats"
# aember-resources release with the SDKs (compilers, sysroots) this repo builds
# with; v0.1.13 adds Qt to the SDKs (only aember-monitor needs it).
RESOURCES_VERSION = "0.1.13"
RESOURCES_REPO = "ajdariarian94/aember-resources"
