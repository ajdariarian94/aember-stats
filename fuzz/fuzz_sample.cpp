// Collector::Sample over a fake /proc, /sys and heartbeat with fuzzed
// contents, twice (rates are deltas): every line is one JSON object.
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <fuzzer/FuzzedDataProvider.h>
#include <unistd.h>

#include <aember-stats/collector.h>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream{p, std::ios::binary | std::ios::trunc} << text;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  using namespace aember::stats;
  static const fs::path dir =
      fs::temp_directory_path() /
      ("aember-stats-fuzz-" + std::to_string(::getpid()));
  FuzzedDataProvider fdp{data, size};
  Paths paths{dir / "proc", dir / "sys", dir / "heartbeat.json", {dir}};
  const auto zone = paths.sys / "class" / "thermal" / "thermal_zone0";
  Collector c{paths};
  auto now = std::chrono::steady_clock::time_point{};
  for (int round = 0; round < 2; ++round) {
    for (const char* f : {"uptime", "stat", "meminfo", "loadavg", "net/dev"}) {
      Write(paths.proc / f, fdp.ConsumeRandomLengthString(512));
    }
    Write(zone / "temp", fdp.ConsumeRandomLengthString(32));
    Write(zone / "type", fdp.ConsumeRandomLengthString(32));
    Write(paths.heartbeat, fdp.ConsumeRandomLengthString(2048));
    now += std::chrono::milliseconds{fdp.ConsumeIntegralInRange(0, 5000)};
    c.SetNow(now);
    const auto line = c.Sample();
    const auto j = nlohmann::json::parse(line, nullptr, false);
    if (!j.is_object() || line.find('\n') != std::string::npos) std::abort();
  }
  return 0;
}
