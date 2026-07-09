#include "cgraph/daemon_endpoint.hpp"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#endif

namespace cgraph {

std::filesystem::path unix_runtime_dir() {
  if (const auto* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && runtime[0] != '\0') {
    return std::filesystem::path(runtime) / "graphd";
  }

  auto temp = std::filesystem::temp_directory_path();
  if (const auto* user = std::getenv("USER"); user != nullptr && user[0] != '\0') {
    return temp / ("graphd-" + std::string(user));
  }
  return temp / "graphd";
}

std::filesystem::path unix_socket_path(const DaemonIdentity& identity) {
  return unix_runtime_dir() / (identity.endpoint_name + ".sock");
}

bool unix_endpoint_is_live(const std::filesystem::path& socket_path) {
#ifndef _WIN32
  std::error_code error;
  if (!std::filesystem::exists(socket_path, error)) {
    return false;  // no endpoint at all -> nothing listening
  }
  const std::string path = socket_path.string();
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() + 1 > sizeof(addr.sun_path)) {
    return false;  // path too long to have been bound in the first place
  }
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

  const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe_fd < 0) {
    return false;
  }
  const int rc = ::connect(probe_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::close(probe_fd);
  return rc == 0;  // a live listener accepted the connection
#else
  (void)socket_path;
  return false;
#endif
}

void ensure_unix_socket_dir(const std::filesystem::path& socket_path) {
  const auto dir = socket_path.parent_path();
  std::filesystem::create_directories(dir);
  std::filesystem::permissions(
      dir,
      std::filesystem::perms::owner_read |
          std::filesystem::perms::owner_write |
          std::filesystem::perms::owner_exec,
      std::filesystem::perm_options::replace);
}

std::string windows_named_pipe_path(const DaemonIdentity& identity) {
  return R"(\\.\pipe\)" + identity.endpoint_name;
}

void configure_windows_named_pipe_security() {
#ifdef _WIN32
  // The named pipe server should create a user-scoped security descriptor before
  // calling CreateNamedPipe. The concrete descriptor is attached when the Windows
  // pipe server lands.
#else
  throw std::logic_error("Windows named-pipe security is only available on Windows");
#endif
}

}  // namespace cgraph
