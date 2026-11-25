// The /proc parsers on any text: what they return is in range.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <aember-stats/collector.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  using namespace aember::stats;
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  const auto cpu = ParseProcStat(text);
  if (cpu && cpu->busy > cpu->total) std::abort();
  if (CountCpus(text) < 0) std::abort();
  if (cpu) {
    const double p = CpuPercent({}, *cpu);
    if (!(p >= 0 && p <= 100)) std::abort();
  }
  ParseMeminfo(text);
  if (const auto l = ParseLoadavg(text)) {
    for (const double v : {l->one, l->five, l->fifteen}) {
      if (!std::isfinite(v) || v < 0) std::abort();
    }
  }
  if (const auto up = ParseUptime(text); up && (!std::isfinite(*up) || *up < 0))
    std::abort();
  for (const auto& [name, c] : ParseNetDev(text)) {
    if (name.empty() || name == "lo") std::abort();
  }
  return 0;
}
