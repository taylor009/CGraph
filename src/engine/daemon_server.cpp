#include "cgraph/daemon_server.hpp"

#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/pipeline.hpp"
#include "cgraph/protocol.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace cgraph {
namespace {

#ifndef _WIN32

// Fills a sockaddr_un for `path`. Returns false if the path is too long for the
// platform's sun_path (a hard limit, ~104 bytes on macOS) — a clear failure
// rather than a silently truncated, wrong endpoint.
[[nodiscard]] bool fill_sockaddr(sockaddr_un& addr, const std::string& path) {
  if (path.size() >= sizeof(addr.sun_path)) {
    return false;
  }
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  return true;
}

[[nodiscard]] bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto written = ::write(fd, data + sent, size - sent);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    sent += static_cast<std::size_t>(written);
  }
  return true;
}

[[nodiscard]] bool read_exact(int fd, std::uint8_t* data, std::size_t size) {
  std::size_t received = 0;
  while (received < size) {
    const auto got = ::read(fd, data + received, size - received);
    if (got == 0) {
      return false;  // peer closed early
    }
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    received += static_cast<std::size_t>(got);
  }
  return true;
}

// Reads one length-prefixed frame (4-byte little-endian length + body) and
// decodes it, reusing the shared protocol codec.
[[nodiscard]] std::optional<nlohmann::json> read_frame(int fd) {
  std::array<std::uint8_t, 4> header{};
  if (!read_exact(fd, header.data(), header.size())) {
    return std::nullopt;
  }
  const auto length =
      static_cast<std::uint32_t>(header[0]) |
      (static_cast<std::uint32_t>(header[1]) << 8U) |
      (static_cast<std::uint32_t>(header[2]) << 16U) |
      (static_cast<std::uint32_t>(header[3]) << 24U);
  std::vector<std::uint8_t> frame(static_cast<std::size_t>(length) + 4);
  std::memcpy(frame.data(), header.data(), header.size());
  if (length > 0 && !read_exact(fd, frame.data() + 4, length)) {
    return std::nullopt;
  }
  return decode_frame(frame);
}

[[nodiscard]] bool write_frame(int fd, const nlohmann::json& payload) {
  const auto frame = encode_frame(payload);
  return write_all(fd, frame.data(), frame.size());
}

#endif  // !_WIN32

}  // namespace

#ifdef _WIN32

int run_daemon_server(const std::filesystem::path& root, DaemonServerOptions) {
  (void)root;
  std::cerr << "graphd: the Unix-socket server is not implemented on Windows yet\n";
  return 1;
}

std::optional<nlohmann::json> request_over_unix_socket(const std::filesystem::path&, const nlohmann::json&) {
  return std::nullopt;
}

#else

int run_daemon_server(const std::filesystem::path& root, DaemonServerOptions options) {
  const auto identity = daemon_identity_for(root);
  const auto socket_path = unix_socket_path(identity);
  ensure_unix_socket_dir(socket_path);
  ::unlink(socket_path.c_str());  // clear any stale endpoint from a crashed daemon

  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::cerr << "graphd: socket() failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  sockaddr_un addr{};
  if (!fill_sockaddr(addr, socket_path.string())) {
    std::cerr << "graphd: socket path too long: " << socket_path << '\n';
    ::close(listen_fd);
    return 1;
  }
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "graphd: bind() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    return 1;
  }
  if (::listen(listen_fd, 16) != 0) {
    std::cerr << "graphd: listen() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    (void)cleanup_daemon_endpoint(socket_path);
    return 1;
  }

  DaemonState state;
  state.pid = ::getpid();
  if (options.build_graph_on_start) {
    auto pipeline = run_one_shot(identity.project_root);
    publish_graph_snapshot(state, std::move(pipeline.graph));
  }

  std::cerr << "graphd listening on " << socket_path << " for root " << identity.project_root << '\n';

  while (!state.shutdown_requested) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd, &read_set);
    timeval timeout{};
    timeout.tv_sec = static_cast<time_t>(options.idle_timeout.count());

    const int ready = ::select(listen_fd + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "graphd: select() failed: " << std::strerror(errno) << '\n';
      break;
    }
    if (ready == 0) {
      std::cerr << "graphd: idle timeout, shutting down\n";
      break;
    }

    const int conn = ::accept(listen_fd, nullptr, nullptr);
    if (conn < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    // The thin client uses one connection per request: read it, answer it, close.
    if (const auto request = read_frame(conn)) {
      const auto response = handle_daemon_request(state, *request);
      (void)write_frame(conn, response);
    }
    ::close(conn);
  }

  ::close(listen_fd);
  (void)cleanup_daemon_endpoint(socket_path);
  return 0;
}

std::optional<nlohmann::json> request_over_unix_socket(
    const std::filesystem::path& socket_path,
    const nlohmann::json& request) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::nullopt;
  }
  sockaddr_un addr{};
  if (!fill_sockaddr(addr, socket_path.string())) {
    ::close(fd);
    return std::nullopt;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);  // ECONNREFUSED / ENOENT -> caller spawns and retries
    return std::nullopt;
  }
  std::optional<nlohmann::json> response;
  if (write_frame(fd, request)) {
    response = read_frame(fd);
  }
  ::close(fd);
  return response;
}

#endif  // _WIN32

}  // namespace cgraph
