#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include <aember-stats/collector.h>

using namespace aember::stats;

namespace {

const std::filesystem::path kData = AEMBER_STATS_TEST_DATA;

std::string Read(const std::filesystem::path& path) {
  std::ifstream f{path};
  return {std::istreambuf_iterator<char>{f}, {}};
}

Paths FixturePaths() {
  Paths p;
  p.proc = kData / "proc";
  p.sys = kData / "sys";
  p.heartbeat = kData / "heartbeat.json";
  p.disks = {kData};
  return p;
}

}  // namespace

TEST(ParseProcStat, SumsAllCpusAndCountsIdleAndIowaitAsIdle) {
  const auto t = ParseProcStat(Read(kData / "proc" / "stat"));
  ASSERT_TRUE(t);
  EXPECT_EQ(t->total, 1000U);
  EXPECT_EQ(t->busy, 150U);  // user 100 + system 50
  EXPECT_EQ(CountCpus(Read(kData / "proc" / "stat")), 2);
}

TEST(ParseProcStat, RejectsGarbage) {
  EXPECT_FALSE(ParseProcStat(""));
  EXPECT_FALSE(ParseProcStat("cpu  1 2 x 4\n"));
  EXPECT_FALSE(ParseProcStat("cpu 1 2\n"));
}

TEST(CpuPercent, IsTheBusyShareOfTheElapsedJiffies) {
  EXPECT_DOUBLE_EQ(CpuPercent({100, 1000}, {150, 1100}), 50.0);
  EXPECT_DOUBLE_EQ(CpuPercent({100, 1000}, {100, 1000}), 0.0);  // no time
  EXPECT_DOUBLE_EQ(CpuPercent({100, 1000}, {50, 1100}), 0.0);   // reset
}

TEST(ParseMeminfo, ReadsTotalAndAvailable) {
  const auto m = ParseMeminfo(Read(kData / "proc" / "meminfo"));
  ASSERT_TRUE(m);
  EXPECT_EQ(m->total_kb, 512000U);
  EXPECT_EQ(m->available_kb, 384000U);
  EXPECT_FALSE(ParseMeminfo("MemTotal: 1 kB\n"));  // no MemAvailable
}

TEST(ParseLoadavgAndUptime, ReadTheFirstFields) {
  const auto l = ParseLoadavg(Read(kData / "proc" / "loadavg"));
  ASSERT_TRUE(l);
  EXPECT_DOUBLE_EQ(l->one, 0.52);
  EXPECT_DOUBLE_EQ(l->fifteen, 0.10);
  EXPECT_DOUBLE_EQ(*ParseUptime(Read(kData / "proc" / "uptime")), 3725.42);
  EXPECT_FALSE(ParseLoadavg("1.0 2.0"));
}

TEST(ParseNetDev, SkipsHeadersAndLoopback) {
  const auto net = ParseNetDev(Read(kData / "proc" / "net" / "dev"));
  ASSERT_EQ(net.size(), 1U);
  EXPECT_EQ(net.at("eth0").rx_bytes, 1000000U);
  EXPECT_EQ(net.at("eth0").tx_bytes, 200000U);
}

TEST(Json, EscapesStringsAndKeepsObjectsOnOneLine) {
  EXPECT_EQ(JsonString("a\"b\\c\n"), R"("a\"b\\c\n")");
  EXPECT_EQ(JsonString(std::string{"\x01", 1}), R"("\u0001")");
  EXPECT_EQ(*JsonObject("  {\"a\":\n1}\n"), "{\"a\": 1}");
  EXPECT_FALSE(JsonObject(""));
  EXPECT_FALSE(JsonObject("[1,2]"));
  EXPECT_FALSE(JsonObject("{\"truncated\":"));
}

TEST(Collector, FirstSampleHasEverySection) {
  Collector c{FixturePaths()};
  const auto s = c.Sample();
  EXPECT_TRUE(s.starts_with("{\"version\":1,"));
  EXPECT_TRUE(s.ends_with("}"));
  EXPECT_EQ(s.find('\n'), std::string::npos);
  for (const auto* key : {"\"uptime_s\":3725",
                          "\"cores\":2",
                          "\"used_percent\":25.0",
                          "\"load\":[0.52,0.25,0.10]",
                          "\"iface\":\"eth0\"",
                          "\"type\":\"cpu-thermal\",\"celsius\":42.5",
                          "\"disks\":[{\"path\":",
                          "\"aember\":{\"pid1\":{\"pid\":1"}) {
    EXPECT_NE(s.find(key), std::string::npos) << key << " in " << s;
  }
  // No previous sample yet: no CPU usage, no rates.
  EXPECT_NE(s.find("\"percent\":0.0"), std::string::npos);
  EXPECT_NE(s.find("\"rx_bps\":0"), std::string::npos);
}

TEST(Collector, RatesComeFromTheElapsedTime) {
  const auto dir = std::filesystem::temp_directory_path() /
                   ("aember-stats-test-" + std::to_string(::getpid()));
  std::filesystem::remove_all(dir);
  std::filesystem::copy(kData, dir, std::filesystem::copy_options::recursive);
  Paths p = FixturePaths();
  p.proc = dir / "proc";
  Collector c{p};
  const auto t0 =
      std::chrono::steady_clock::time_point{} + std::chrono::hours{1};
  c.SetNow(t0);
  c.Sample();

  // 2 s later: +2000 rx bytes, +1000 tx bytes, 50 of 100 jiffies busy.
  std::ofstream{dir / "proc" / "net" / "dev"}
      << "  eth0: 1002000 0 0 0 0 0 0 0 201000 0 0 0 0 0 0 0\n";
  std::ofstream{dir / "proc" / "stat"} << "cpu  150 0 50 850 50 0 0 0\n";
  c.SetNow(t0 + std::chrono::seconds{2});
  const auto s = c.Sample();
  EXPECT_NE(s.find("\"rx_bps\":1000,\"tx_bps\":500"), std::string::npos) << s;
  EXPECT_NE(s.find("\"percent\":50.0"), std::string::npos) << s;
  std::filesystem::remove_all(dir);
}

TEST(Collector, MissingFilesGiveASampleWithoutThem) {
  Paths p;
  p.proc = "/nonexistent/proc";
  p.sys = "/nonexistent/sys";
  p.heartbeat = "/nonexistent/heartbeat.json";
  p.disks = {"/nonexistent"};
  Collector c{p};
  const auto s = c.Sample();
  EXPECT_NE(s.find("\"aember\":null}"), std::string::npos) << s;
  EXPECT_NE(s.find("\"net\":[]"), std::string::npos);
  EXPECT_NE(s.find("\"disks\":[]"), std::string::npos);
}

// Regression: after aember renamed its status file to aember-status.json,
// the default still read aember-monitor.json and every sample had
// "aember":null.
TEST(Collector, DefaultStatusFileIsAembersCurrentName) {
  EXPECT_EQ(Paths{}.heartbeat, "/host-logs/aember-status.json");
}
