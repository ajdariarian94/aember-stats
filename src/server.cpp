/**
 * @file server.cpp
 * @brief Unix stream socket, JSON lines to every client (see server.h).
 */
#include <cerrno>
#include <cstring>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <aember-stats/server.h>

namespace aember::stats {

namespace {

[[noreturn]] void Fail(const char* what) {
  throw std::system_error(errno, std::system_category(), what);
}

}  // namespace

Server::Server(std::filesystem::path path) : path_{std::move(path)} {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const auto str = path_.string();
  if (str.size() >= sizeof(addr.sun_path)) {
    throw std::system_error(
        ENAMETOOLONG, std::system_category(), "socket path too long");
  }
  std::memcpy(addr.sun_path, str.c_str(), str.size() + 1);

  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  ::unlink(str.c_str());

  listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listen_fd_ < 0) Fail("socket");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
      0) {
    const int saved = errno;
    ::close(listen_fd_);
    errno = saved;
    Fail("bind");
  }
  // Any local user (the monitor container may run as another uid) may read.
  ::chmod(str.c_str(), 0666);
  if (::listen(listen_fd_, 8) != 0) {
    const int saved = errno;
    ::close(listen_fd_);
    errno = saved;
    Fail("listen");
  }
}

Server::~Server() {
  for (const int fd : clients_) ::close(fd);
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    ::unlink(path_.c_str());
  }
}

void Server::Accept(int timeout_ms) {
  pollfd pfd{listen_fd_, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0) return;
  while (true) {
    const int fd =
        ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return;  // EAGAIN: no more pending clients
    clients_.push_back(fd);
  }
}

void Server::Broadcast(const std::string& line) {
  const std::string message = line + "\n";
  std::erase_if(clients_, [&](int fd) {
    const auto n =
        ::send(fd, message.data(), message.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n == static_cast<ssize_t>(message.size())) return false;
    ::close(fd);  // gone, or too slow to take a whole line
    return true;
  });
}

}  // namespace aember::stats
