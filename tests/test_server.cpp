#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <aember-stats/server.h>

using aember::stats::Server;

namespace {

std::filesystem::path TempSocket() {
  return std::filesystem::temp_directory_path() /
         ("aember-stats-" + std::to_string(::getpid())) / "stats.sock";
}

int Connect(const std::filesystem::path& path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

std::string ReadLine(int fd) {
  std::string line;
  char c = 0;
  while (::read(fd, &c, 1) == 1 && c != '\n') line += c;
  return line;
}

}  // namespace

TEST(Server, EveryClientGetsEveryLine) {
  const auto path = TempSocket();
  {
    Server server{path};
    EXPECT_TRUE(std::filesystem::is_socket(path));
    const int a = Connect(path);
    const int b = Connect(path);
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    server.Accept(1000);
    while (server.Clients() < 2) server.Accept(1000);

    server.Broadcast(R"({"n":1})");
    server.Broadcast(R"({"n":2})");
    EXPECT_EQ(ReadLine(a), R"({"n":1})");
    EXPECT_EQ(ReadLine(a), R"({"n":2})");
    EXPECT_EQ(ReadLine(b), R"({"n":1})");
    ::close(a);
    ::close(b);
  }
  EXPECT_FALSE(std::filesystem::exists(path));  // removed on shutdown
  std::filesystem::remove_all(path.parent_path());
}

TEST(Server, AGoneClientIsDroppedAndTheOthersKeepGoing) {
  const auto path = TempSocket();
  Server server{path};
  const int gone = Connect(path);
  const int stays = Connect(path);
  while (server.Clients() < 2) server.Accept(1000);
  ::close(gone);
  server.Broadcast("x");  // may still succeed into the closed peer's buffer
  server.Broadcast("y");
  EXPECT_EQ(server.Clients(), 1U);
  EXPECT_EQ(ReadLine(stays), "x");
  ::close(stays);
  std::filesystem::remove_all(path.parent_path());
}

TEST(Server, ReplacesAStaleSocketFile) {
  const auto path = TempSocket();
  std::filesystem::create_directories(path.parent_path());
  { std::ofstream{path} << "stale"; }
  Server server{path};
  EXPECT_TRUE(std::filesystem::is_socket(path));
  struct stat st {};
  ASSERT_EQ(::stat(path.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0666U);  // the monitor may run as another uid
  std::filesystem::remove_all(path.parent_path());
}
