/**
 * @file main.cpp
 * @brief aember-stats: sample statistics every interval, serve them as JSON
 *        lines on a Unix socket (or print one sample with --once).
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include <aember-stats/collector.h>
#include <aember-stats/server.h>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int /*signal*/) {
  g_stop = true;
}

constexpr std::string_view kUsage =
    "usage: aember-stats [--socket PATH] [--interval-ms N] [--heartbeat PATH]\n"
    "                    [--proc DIR] [--sys DIR] [--disk PATH]... [--once]\n";

}  // namespace

int main(int argc, char** argv) {
  aember::stats::Paths paths;
  std::string socket_path = "/run/aember-stats/stats.sock";
  int interval_ms = 1000;
  bool once = false;
  bool disks_given = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::cerr << kUsage;
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--socket") {
      socket_path = value();
    } else if (arg == "--interval-ms") {
      interval_ms = std::max(100, std::stoi(value()));
    } else if (arg == "--heartbeat") {
      paths.heartbeat = value();
    } else if (arg == "--proc") {
      paths.proc = value();
    } else if (arg == "--sys") {
      paths.sys = value();
    } else if (arg == "--disk") {
      if (!disks_given) paths.disks.clear();
      disks_given = true;
      paths.disks.emplace_back(value());
    } else if (arg == "--once") {
      once = true;
    } else {
      std::cerr << kUsage;
      return arg == "--help" || arg == "-h" ? 0 : 2;
    }
  }

  aember::stats::Collector collector{paths};
  if (once) {
    std::cout << collector.Sample() << '\n';
    return 0;
  }

  std::signal(SIGTERM, OnSignal);
  std::signal(SIGINT, OnSignal);
  try {
    aember::stats::Server server{socket_path};
    std::cout << "aember-stats: serving " << socket_path << " every "
              << interval_ms << " ms\n"
              << std::flush;
    auto next = std::chrono::steady_clock::now();
    while (!g_stop) {
      const auto line = collector.Sample();
      server.Broadcast(line);
      next += std::chrono::milliseconds{interval_ms};
      // Accept clients until the next sample is due.
      while (!g_stop) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              next - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) break;
        server.Accept(static_cast<int>(left));
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "aember-stats: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
