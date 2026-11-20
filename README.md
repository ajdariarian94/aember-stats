# aember-stats

A tiny LXC container for [aember](https://github.com/ajdariarian94/aember)
that only **collects statistics** and serves them to viewers such as
[aember-monitor](https://github.com/ajdariarian94/aember-monitor). It has no
UI and no dependencies besides the C++ runtime: one ~150 KB binary in a ~2 MB
container.

Every second it samples:

| What | From |
|---|---|
| CPU usage (all cores), core count, uptime, load | `/proc/stat`, `/proc/uptime`, `/proc/loadavg` |
| Memory (total, available, used %) | `/proc/meminfo` |
| Network bytes and rates per interface | `/proc/net/dev` (aember's network: `"network": "host"`) |
| Disk space | `statvfs` of `/host-logs` (the device's `/var/log`) |
| Temperatures | `/sys/class/thermal/thermal_zone*` |
| aember itself: PID 1, services and containers, network state | the heartbeat aember's PID 1 writes to `/var/log/aember-status.json`, which aember-containerd binds into every container at `/host-logs` |

and writes one JSON object per line to every client of the Unix socket
**`/run/aember-stats/stats.sock`**:

```json
{"version":1,"time":1795194984,"uptime_s":36142,
 "cpu":{"percent":12.5,"cores":4},
 "memory":{"total_kb":512000,"available_kb":384000,"used_percent":25.0},
 "load":[0.52,0.25,0.10],
 "net":[{"iface":"eth0","rx_bytes":1000000,"tx_bytes":200000,"rx_bps":1000,"tx_bps":500}],
 "disks":[{"path":"/host-logs","total_kb":495844,"available_kb":470120}],
 "thermal":[{"zone":"thermal_zone0","type":"cpu-thermal","celsius":42.5}],
 "aember":{"pid1":{...},"network":{...},"services":[...]}}
```

(shown wrapped; on the wire each sample is a single line). `aember` is the
heartbeat object unchanged, or `null` when there is none. A client that
doesn't keep up is disconnected; reconnecting is always safe.

`/run/aember-stats` is a directory on the device (aember mounts `/run` as
tmpfs): the container's LXC pre-start hook creates it and binds it in, and a
viewer container binds the same directory. No network is involved.

## Build

Everything runs in the dev container (`.devcontainer/`, aember's dev image).
The cross compilers are the aember-resources SDKs, fetched on demand:

```bash
git submodule update --init                     # tools/aember-build-helper
aember-cli/run.py sdk   -a aarch64-poky-linux   # SDK into build/aember-resources/
aember-cli/run.py build -a aarch64-poky-linux   # → build/stage/<arch>/
aember-cli/run.py pack  -a aarch64-poky-linux   # → build/aember-out/<arch>/aember-stats/
aember-cli/run.py install -a aarch64-poky-linux ../aember   # into aember's overlay
aember-cli/run.py all   -a aarch64-poky-linux   # sdk + build + pack
aember-cli/run.py test                          # unit tests (host build) + CLI tests
```

Architectures: `x64-poky-linux`, `aarch64-poky-linux`, `arm-poky-linux`,
`riscv64-poky-linux` and their `-musl` variants. To reuse the SDKs aember
already downloaded: `export AEMBER_RESOURCES_DIR=/aember_ws/aember/build/aember-resources`.
`AEMBER_BUILD_JOBS` caps the build jobs (default: 2/3 of the CPUs).

Packing uses [aember-build-helper](https://github.com/ajdariarian94/aember-build-helper)
(submodule in `tools/`), with `aember-container.json` as the manifest.

## Run it outside aember

```bash
build/configure/host/Debug/aember-stats --once            # one sample to stdout
build/configure/host/Debug/aember-stats --socket /tmp/s.sock --heartbeat /dev/null &
socat - UNIX-CONNECT:/tmp/s.sock                          # one line per second
```

Options: `--socket PATH`, `--interval-ms N` (≥ 100, default 1000),
`--heartbeat PATH`, `--proc DIR`, `--sys DIR`, `--disk PATH` (repeatable),
`--once`.

## License

See [LICENSE](LICENSE).
