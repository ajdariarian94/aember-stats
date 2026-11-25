# AGENTS.md

Guidance for AI coding agents working in **aember-stats**.

## What this repo is

An LXC container for aember (a C++23 PID 1 for container appliances) that
collects statistics and serves them as JSON lines on
`/run/aember-stats/stats.sock`. Its viewer is the sibling repo
`../aember-monitor` (a small Qt app in its own container). Siblings:
`../aember` (the init system), `../aember-resources` (kernel, BusyBox, LXC,
SDKs), `../aember-build-helper` (packs containers; a submodule here).

The JSON format (`version` 1) is an interface: aember-monitor parses it.
Change it in both repos together, add fields rather than renaming them, and
bump `version` for anything incompatible. It is documented in
`include/aember-stats/collector.h` and README.md.

## Layout

```
include/aember-stats/collector.h   sampling + the pure /proc parsers
include/aember-stats/server.h      Unix socket, one line per client per sample
src/                               collector.cpp, server.cpp, main.cpp (flags)
tests/                             GoogleTest (host build), fixtures in tests/data/
fuzz/                              libFuzzer targets (clang, ASan + UBSan), corpus, run.sh
aember-container.json              the container manifest (aember-build-helper)
aember-cli/run.py                  `aember` CLI: sdk, build, pack, install, test, all
                                   (aember-build-helper's project-cli/, told NAME etc.)
tools/aember-build-helper/         submodule
```

## Rules

- **Read files only.** The collector must run unprivileged in a container:
  /proc, /sys, `statvfs`, and the heartbeat at
  `/host-logs/aember-status.json` (aember-containerd binds the device's
  /var/log at /host-logs). No netlink, no root-only interfaces.
- The heartbeat is embedded verbatim, after `JsonObject()` checked it is
  strictly valid JSON (RFC 8259, UTF-8, numbers that fit a double, nesting
  up to 64) and put it on one line: one broken heartbeat must not make the
  whole line unreadable for the monitor. `JsonString()` writes U+FFFD for
  bytes that aren't UTF-8. There is deliberately no JSON library (the fuzz
  targets use nlohmann/json only to check the output).
- Parsers are pure functions over text, unit-tested with fixtures. A missing
  or unreadable file leaves its section out (or empty), never an error.
- The socket server never blocks the sampling loop: non-blocking sends, a
  client that can't take a whole line is dropped.
- The shared directory: the manifest's LXC pre-start hook creates
  `/run/aember-stats` on the device and binds it in. aember-monitor's
  manifest has the same two lines; keep them in sync.
- The CLI (`aember-cli/`, called `aember` like in the sibling repos) is
  Python 3.10+ standard library only and is run through `run.py`; never
  `pip install` it (aember's dev container has aember's own `aember`
  package). It is shared with aember-monitor, aember-auth and aember-fleet and
  lives in the aember-build-helper submodule (`project-cli/`);
  `aember-cli/run.py` holds what differs. Change the CLI there.
- WSL crashes when builds use every core: `jobs()` uses 2/3 of the CPUs
  (`AEMBER_BUILD_JOBS` overrides).

## Builds and tests

- Cross builds source the SDK's `environment-setup-<sysroot>` and use its
  `OEToolchainConfig.cmake` (build-helper's `project-cli/aember_project/build.py`); no toolchain files here.
- `aember-cli/run.py test`: a host build with
  `-DBUILD_TESTS=ON` (GoogleTest via FetchContent, pinned by hash) and ctest.
- Before calling a change done: `run.py test`, a cross build + pack of at
  least x64, and, for anything the device sees, the container in aember
  (`run.py install -a x64-poky-linux ../aember`, then `aember app pack` and
  `aember app launch --headless` there).
- C++23, `-Werror`, clang-format (`.clang-format` from aember). Python:
  black, 88 columns.
- Fuzzing: `fuzz/run.sh [SECONDS] [TARGET...]` (CI: 60 s per target, the
  `fuzz` job; needs clang-18, libclang-rt-18-dev, nlohmann-json3-dev).
  `fuzz_parsers` (the /proc parsers), `fuzz_json` (JsonString/JsonObject)
  and `fuzz_sample` (Collector::Sample over a fuzzed /proc, /sys and
  heartbeat: every line must parse as one JSON object). A crash gets a fix,
  a `Regression:` test and its input in `fuzz/corpus/<target>/`.
- A test that pins down a fixed bug starts with a comment
  `Regression: <the bug>` (`grep -rn "Regression:"` lists them). Every bug
  fix gets one.

## Working agreements

- Branch off `main`, merge through a PR; commit, push and merge only when
  asked.
