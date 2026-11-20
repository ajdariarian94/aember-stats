/**
 * @file collector.h
 * @brief Samples system and aember statistics into one JSON line.
 *
 * Everything is read from files, so the collector runs unprivileged inside
 * an LXC container: /proc and /sys (system-wide counters, the host's network
 * namespace with `"network": "host"`) and the heartbeat aember's PID 1 writes
 * to /var/log/aember-monitor.json, which aember-containerd binds into every
 * container at /host-logs.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aember::stats {

/// Jiffies of all CPUs together (the `cpu` line of /proc/stat).
struct CpuTimes {
  std::uint64_t busy = 0;
  std::uint64_t total = 0;
};

struct Memory {
  std::uint64_t total_kb = 0;
  std::uint64_t available_kb = 0;
};

struct Load {
  double one = 0, five = 0, fifteen = 0;
};

struct NetCounters {
  std::uint64_t rx_bytes = 0;
  std::uint64_t tx_bytes = 0;
};

struct Thermal {
  std::string zone;
  std::string type;
  double celsius = 0;
};

// ---- Parsers (pure, unit-tested with fixture text) ----------------------

std::optional<CpuTimes> ParseProcStat(std::string_view text);
/// Number of `cpuN` lines in /proc/stat.
int CountCpus(std::string_view text);
/// Busy share between two samples, 0..100; 0 when no time passed.
double CpuPercent(const CpuTimes& before, const CpuTimes& after);
std::optional<Memory> ParseMeminfo(std::string_view text);
std::optional<Load> ParseLoadavg(std::string_view text);
std::optional<double> ParseUptime(std::string_view text);
/// Interfaces of /proc/net/dev (loopback skipped) → byte counters.
std::map<std::string, NetCounters> ParseNetDev(std::string_view text);

/// A JSON string literal (quotes and escapes included).
std::string JsonString(std::string_view text);
/// @p text if it is a JSON object (starts with '{' and ends with '}' after
/// trimming), else nullopt. The heartbeat is embedded verbatim, not parsed.
std::optional<std::string> JsonObject(std::string_view text);

struct Paths {
  std::filesystem::path proc = "/proc";
  std::filesystem::path sys = "/sys";
  /// aember's PID 1 heartbeat, as containerd binds it into containers.
  std::filesystem::path heartbeat = "/host-logs/aember-monitor.json";
  /// File systems to report (statvfs).
  std::vector<std::filesystem::path> disks = {"/host-logs"};
};

/**
 * Keeps the previous sample, so CPU usage and network rates are deltas.
 * Sample() returns one JSON object, no newline. Format (version 1):
 *
 *   {"version":1,"time":<unix s>,"uptime_s":..,
 *    "cpu":{"percent":..,"cores":..},
 *    "memory":{"total_kb":..,"available_kb":..,"used_percent":..},
 *    "load":[1m,5m,15m],
 *    "net":[{"iface":..,"rx_bytes":..,"tx_bytes":..,"rx_bps":..,"tx_bps":..}],
 *    "disks":[{"path":..,"total_kb":..,"available_kb":..}],
 *    "thermal":[{"zone":..,"type":..,"celsius":..}],
 *    "aember":<heartbeat object or null>}
 */
class Collector {
 public:
  explicit Collector(Paths paths = {});

  std::string Sample();

  /// Test hook: the clock used for rates.
  void SetNow(std::chrono::steady_clock::time_point now) { now_ = now; }

 private:
  std::string ReadFile(const std::filesystem::path& path) const;
  std::vector<Thermal> ReadThermal() const;

  Paths paths_;
  std::optional<std::chrono::steady_clock::time_point> now_;
  std::optional<CpuTimes> last_cpu_;
  std::map<std::string, NetCounters> last_net_;
  std::optional<std::chrono::steady_clock::time_point> last_time_;
};

}  // namespace aember::stats
