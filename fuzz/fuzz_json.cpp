// JsonString and JsonObject on any bytes: the result is JSON a strict
// parser (nlohmann/json, like the monitor's Qt) accepts, on one line.
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <aember-stats/collector.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  using namespace aember::stats;
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  const auto s = JsonString(text);
  if (!nlohmann::json::accept(s) || s.find('\n') != std::string::npos)
    std::abort();
  const auto o = JsonObject(text);
  if (o) {
    const auto j = nlohmann::json::parse(*o, nullptr, false);
    if (!j.is_object() || o->find('\n') != std::string::npos) std::abort();
  }
  return 0;
}
