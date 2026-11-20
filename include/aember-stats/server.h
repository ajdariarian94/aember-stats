/**
 * @file server.h
 * @brief Pushes one JSON line per sample to every client of a Unix socket.
 *
 * The socket lives in a directory that the stats and the monitor container
 * both bind from the host, so no network is involved. Clients only read;
 * a client that can't keep up (a full socket buffer) is dropped, so a
 * stalled viewer never blocks the collector.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aember::stats {

class Server {
 public:
  /// Binds and listens on @p path (an old socket file there is replaced).
  /// Throws std::system_error on failure.
  explicit Server(std::filesystem::path path);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  /// Accepts pending clients; returns after at most @p timeout_ms, or as soon
  /// as a client connected.
  void Accept(int timeout_ms);

  /// Sends @p line plus '\n' to every client; drops clients that fail.
  void Broadcast(const std::string& line);

  [[nodiscard]] std::size_t Clients() const { return clients_.size(); }
  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
  int listen_fd_ = -1;
  std::vector<int> clients_;
};

}  // namespace aember::stats
