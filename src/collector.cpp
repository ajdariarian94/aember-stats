/**
 * @file collector.cpp
 * @brief /proc, /sys and heartbeat sampling (see collector.h).
 */
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

#include <sys/statvfs.h>

#include <aember-stats/collector.h>

namespace aember::stats {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                        s.front() == '\n' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                        s.back() == '\n' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

std::vector<std::string_view> Lines(std::string_view text) {
  std::vector<std::string_view> lines;
  while (!text.empty()) {
    const auto end = text.find('\n');
    lines.push_back(text.substr(0, end));
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return lines;
}

std::vector<std::string_view> Fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const auto start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    if (i > start) fields.push_back(line.substr(start, i - start));
  }
  return fields;
}

template <typename T>
std::optional<T> Number(std::string_view text) {
  T value{};
  const auto* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

std::string Fixed(double value) {
  return std::format("{:.1f}", value);
}

/// Length of the valid UTF-8 sequence at @p s[i] (1-4), 0 if it isn't one
/// (stray continuation bytes, overlong forms, surrogates, > U+10FFFF).
std::size_t Utf8Length(std::string_view s, std::size_t i) {
  const auto b = [&](std::size_t k) {
    return static_cast<unsigned char>(s[i + k]);
  };
  const auto c0 = b(0);
  if (c0 < 0x80) return 1;
  std::size_t n = 0;
  unsigned lo = 0x80, hi = 0xbf;  // the second byte's range
  if (c0 >= 0xc2 && c0 <= 0xdf) {
    n = 2;
  } else if (c0 >= 0xe0 && c0 <= 0xef) {
    n = 3;
    if (c0 == 0xe0) lo = 0xa0;  // overlong
    if (c0 == 0xed) hi = 0x9f;  // surrogates
  } else if (c0 >= 0xf0 && c0 <= 0xf4) {
    n = 4;
    if (c0 == 0xf0) lo = 0x90;  // overlong
    if (c0 == 0xf4) hi = 0x8f;  // > U+10FFFF
  } else {
    return 0;
  }
  if (i + n > s.size() || b(1) < lo || b(1) > hi) return 0;
  for (std::size_t k = 2; k < n; ++k) {
    if (b(k) < 0x80 || b(k) > 0xbf) return 0;
  }
  return n;
}

/// A strict RFC 8259 check (UTF-8, no NaN/Infinity, numbers that fit a
/// double), so what JsonObject() passes through parses in every viewer.
class JsonCheck {
 public:
  explicit JsonCheck(std::string_view text) : t_(text) {}

  bool Object() {
    Space();
    if (!Peek('{')) return false;
    if (!Value(0)) return false;
    Space();
    return i_ == t_.size();
  }

 private:
  static constexpr int kMaxDepth = 64;

  bool Peek(char c) const { return i_ < t_.size() && t_[i_] == c; }
  bool Eat(char c) {
    if (!Peek(c)) return false;
    ++i_;
    return true;
  }
  void Space() {
    while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' ||
                              t_[i_] == '\n' || t_[i_] == '\r')) {
      ++i_;
    }
  }
  bool Word(std::string_view w) {
    if (t_.substr(i_, w.size()) != w) return false;
    i_ += w.size();
    return true;
  }

  bool Value(int depth) {
    if (depth > kMaxDepth) return false;
    Space();
    if (i_ >= t_.size()) return false;
    switch (t_[i_]) {
      case '{':
        return Members(depth);
      case '[':
        return Elements(depth);
      case '"':
        return String();
      case 't':
        return Word("true");
      case 'f':
        return Word("false");
      case 'n':
        return Word("null");
      default:
        return Number();
    }
  }

  bool Members(int depth) {
    ++i_;  // {
    Space();
    if (Eat('}')) return true;
    while (true) {
      Space();
      if (!String()) return false;
      Space();
      if (!Eat(':') || !Value(depth + 1)) return false;
      Space();
      if (Eat('}')) return true;
      if (!Eat(',')) return false;
    }
  }

  bool Elements(int depth) {
    ++i_;  // [
    Space();
    if (Eat(']')) return true;
    while (true) {
      if (!Value(depth + 1)) return false;
      Space();
      if (Eat(']')) return true;
      if (!Eat(',')) return false;
    }
  }

  bool String() {
    if (!Eat('"')) return false;
    while (i_ < t_.size()) {
      const auto c = static_cast<unsigned char>(t_[i_]);
      if (c == '"') {
        ++i_;
        return true;
      }
      if (c < 0x20) return false;
      if (c == '\\') {
        if (++i_ >= t_.size()) return false;
        const char e = t_[i_++];
        if (e == 'u') {
          if (i_ + 4 > t_.size()) return false;
          for (int k = 0; k < 4; ++k) {
            if (!std::isxdigit(static_cast<unsigned char>(t_[i_++])))
              return false;
          }
        } else if (std::string_view{"\"\\/bfnrt"}.find(e) ==
                   std::string_view::npos) {
          return false;
        }
        continue;
      }
      const auto n = Utf8Length(t_, i_);
      if (n == 0) return false;
      i_ += n;
    }
    return false;  // no closing quote
  }

  bool Number() {
    const auto start = i_;
    Eat('-');
    if (Eat('0')) {
    } else if (Digits() == 0) {
      return false;
    }
    if (Eat('.') && Digits() == 0) return false;
    if (Eat('e') || Eat('E')) {
      if (!Eat('+')) Eat('-');
      if (Digits() == 0) return false;
    }
    double value = 0;  // must fit a double (1e999 doesn't)
    const auto* first = t_.data() + start;
    const auto [ptr, ec] = std::from_chars(first, t_.data() + i_, value);
    return ec == std::errc{} && ptr == t_.data() + i_;
  }

  std::size_t Digits() {
    const auto start = i_;
    while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
    return i_ - start;
  }

  std::string_view t_;
  std::size_t i_ = 0;
};

/// A finite, non-negative number of /proc (nan, inf and -1 aren't).
std::optional<double> Finite(std::string_view text) {
  const auto v = Number<double>(text);
  if (!v || !std::isfinite(*v) || *v < 0) return std::nullopt;
  return v;
}

}  // namespace

std::optional<CpuTimes> ParseProcStat(std::string_view text) {
  for (const auto line : Lines(text)) {
    const auto f = Fields(line);
    if (f.empty() || f[0] != "cpu") continue;
    // user nice system idle iowait irq softirq steal (guest is in user)
    if (f.size() < 5) return std::nullopt;
    CpuTimes t;
    for (std::size_t i = 1; i < f.size() && i <= 8; ++i) {
      const auto v = Number<std::uint64_t>(f[i]);
      if (!v) return std::nullopt;
      t.total += *v;
      if (i != 4 && i != 5) t.busy += *v;  // idle, iowait
    }
    return t;
  }
  return std::nullopt;
}

int CountCpus(std::string_view text) {
  int n = 0;
  for (const auto line : Lines(text)) {
    if (line.size() > 3 && line.starts_with("cpu") && line[3] >= '0' &&
        line[3] <= '9') {
      ++n;
    }
  }
  return n;
}

double CpuPercent(const CpuTimes& before, const CpuTimes& after) {
  if (after.total <= before.total || after.busy < before.busy) return 0;
  const auto busy = static_cast<double>(after.busy - before.busy);
  const auto total = static_cast<double>(after.total - before.total);
  return std::clamp(100.0 * busy / total, 0.0, 100.0);
}

std::optional<Memory> ParseMeminfo(std::string_view text) {
  std::optional<std::uint64_t> total, available;
  for (const auto line : Lines(text)) {
    const auto f = Fields(line);
    if (f.size() < 2) continue;
    if (f[0] == "MemTotal:") total = Number<std::uint64_t>(f[1]);
    if (f[0] == "MemAvailable:") available = Number<std::uint64_t>(f[1]);
  }
  if (!total || !available) return std::nullopt;
  return Memory{*total, *available};
}

std::optional<Load> ParseLoadavg(std::string_view text) {
  const auto f = Fields(Trim(text));
  if (f.size() < 3) return std::nullopt;
  const auto one = Finite(f[0]);
  const auto five = Finite(f[1]);
  const auto fifteen = Finite(f[2]);
  if (!one || !five || !fifteen) return std::nullopt;
  return Load{*one, *five, *fifteen};
}

std::optional<double> ParseUptime(std::string_view text) {
  const auto f = Fields(Trim(text));
  if (f.empty()) return std::nullopt;
  const auto up = Finite(f[0]);
  // Whole seconds go out as an integer: keep it in range.
  if (!up || *up > 1e15) return std::nullopt;
  return up;
}

std::map<std::string, NetCounters> ParseNetDev(std::string_view text) {
  std::map<std::string, NetCounters> result;
  for (const auto line : Lines(text)) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) continue;  // the two header lines
    const auto name = std::string{Trim(line.substr(0, colon))};
    if (name.empty() || name == "lo") continue;
    const auto f = Fields(line.substr(colon + 1));
    // rx: bytes packets errs drop fifo frame compressed multicast; tx: bytes ..
    if (f.size() < 9) continue;
    const auto rx = Number<std::uint64_t>(f[0]);
    const auto tx = Number<std::uint64_t>(f[8]);
    if (rx && tx) result[name] = NetCounters{*rx, *tx};
  }
  return result;
}

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        } else if (const auto n = Utf8Length(text, i); n == 0) {
          out += "\\ufffd";  // not UTF-8 (an interface or sensor name)
        } else {
          out.append(text.substr(i, n));
          i += n - 1;
        }
    }
  }
  return out + "\"";
}

std::optional<std::string> JsonObject(std::string_view text) {
  const auto t = Trim(text);
  if (t.size() < 2 || t.front() != '{' || t.back() != '}') return std::nullopt;
  // A half-written or broken heartbeat would make the whole line invalid
  // and the monitor drop the sample: only valid JSON goes through.
  if (!JsonCheck{t}.Object()) return std::nullopt;
  // One line on the wire: drop raw newlines (legal JSON whitespace only).
  std::string out{t};
  std::ranges::replace(out, '\n', ' ');
  std::ranges::replace(out, '\r', ' ');
  return out;
}

Collector::Collector(Paths paths) : paths_{std::move(paths)} {}

std::string Collector::ReadFile(const std::filesystem::path& path) const {
  std::ifstream file{path};
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

std::vector<Thermal> Collector::ReadThermal() const {
  std::vector<Thermal> zones;
  std::error_code ec;
  const auto dir = paths_.sys / "class" / "thermal";
  for (const auto& entry : std::filesystem::directory_iterator{dir, ec}) {
    const auto name = entry.path().filename().string();
    if (!name.starts_with("thermal_zone")) continue;
    const auto milli = Number<long>(Trim(ReadFile(entry.path() / "temp")));
    if (!milli) continue;
    zones.push_back({name,
                     std::string{Trim(ReadFile(entry.path() / "type"))},
                     static_cast<double>(*milli) / 1000.0});
  }
  std::ranges::sort(zones, {}, &Thermal::zone);
  return zones;
}

std::string Collector::Sample() {
  const auto now = now_.value_or(std::chrono::steady_clock::now());
  const double seconds =
      last_time_ ? std::chrono::duration<double>(now - *last_time_).count()
                 : 0.0;

  std::string out = std::format("{{\"version\":1,\"time\":{}",
                                static_cast<long long>(std::time(nullptr)));

  if (const auto up = ParseUptime(ReadFile(paths_.proc / "uptime"))) {
    out += std::format(",\"uptime_s\":{}", static_cast<long long>(*up));
  }

  const auto stat = ReadFile(paths_.proc / "stat");
  const auto cpu = ParseProcStat(stat);
  const double cpu_percent =
      (cpu && last_cpu_) ? CpuPercent(*last_cpu_, *cpu) : 0.0;
  out += std::format(",\"cpu\":{{\"percent\":{},\"cores\":{}}}",
                     Fixed(cpu_percent),
                     CountCpus(stat));
  if (cpu) last_cpu_ = cpu;

  if (const auto mem = ParseMeminfo(ReadFile(paths_.proc / "meminfo"))) {
    const double used =
        mem->total_kb == 0 || mem->available_kb >= mem->total_kb
            ? 0.0
            : 100.0 * static_cast<double>(mem->total_kb - mem->available_kb) /
                  static_cast<double>(mem->total_kb);
    out += std::format(
        ",\"memory\":{{\"total_kb\":{},\"available_kb\":{},"
        "\"used_percent\":{}}}",
        mem->total_kb,
        mem->available_kb,
        Fixed(used));
  }

  if (const auto load = ParseLoadavg(ReadFile(paths_.proc / "loadavg"))) {
    out += std::format(",\"load\":[{:.2f},{:.2f},{:.2f}]",
                       load->one,
                       load->five,
                       load->fifteen);
  }

  const auto net = ParseNetDev(ReadFile(paths_.proc / "net" / "dev"));
  out += ",\"net\":[";
  bool first = true;
  for (const auto& [iface, c] : net) {
    double rx_bps = 0, tx_bps = 0;
    if (const auto it = last_net_.find(iface);
        it != last_net_.end() && seconds > 0) {
      if (c.rx_bytes >= it->second.rx_bytes) {
        rx_bps =
            static_cast<double>(c.rx_bytes - it->second.rx_bytes) / seconds;
      }
      if (c.tx_bytes >= it->second.tx_bytes) {
        tx_bps =
            static_cast<double>(c.tx_bytes - it->second.tx_bytes) / seconds;
      }
    }
    out += std::format(
        "{}{{\"iface\":{},\"rx_bytes\":{},\"tx_bytes\":{},\"rx_bps\":{},"
        "\"tx_bps\":{}}}",
        first ? "" : ",",
        JsonString(iface),
        c.rx_bytes,
        c.tx_bytes,
        static_cast<long long>(rx_bps),
        static_cast<long long>(tx_bps));
    first = false;
  }
  out += "]";
  last_net_ = net;

  out += ",\"disks\":[";
  first = true;
  for (const auto& path : paths_.disks) {
    struct statvfs vfs {};
    if (::statvfs(path.c_str(), &vfs) != 0) continue;
    const auto unit = static_cast<std::uint64_t>(vfs.f_frsize);
    out += std::format("{}{{\"path\":{},\"total_kb\":{},\"available_kb\":{}}}",
                       first ? "" : ",",
                       JsonString(path.string()),
                       vfs.f_blocks * unit / 1024,
                       vfs.f_bavail * unit / 1024);
    first = false;
  }
  out += "]";

  out += ",\"thermal\":[";
  first = true;
  for (const auto& t : ReadThermal()) {
    out += std::format("{}{{\"zone\":{},\"type\":{},\"celsius\":{}}}",
                       first ? "" : ",",
                       JsonString(t.zone),
                       JsonString(t.type),
                       Fixed(t.celsius));
    first = false;
  }
  out += "]";

  const auto heartbeat = JsonObject(ReadFile(paths_.heartbeat));
  out += ",\"aember\":" + heartbeat.value_or("null") + "}";

  last_time_ = now;
  return out;
}

}  // namespace aember::stats
