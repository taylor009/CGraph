#include "cgraph/daemon_endpoint.hpp"

#include <filesystem>

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph_daemon_endpoint_test";
  std::filesystem::create_directories(root);

  const auto identity = cgraph::daemon_identity_for(root);
  const auto socket = cgraph::unix_socket_path(identity);
  if (socket.extension() != ".sock") {
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto pipe = cgraph::windows_named_pipe_path(identity);
  if (pipe.find(R"(\\.\pipe\graphd-)") != 0) {
    std::filesystem::remove_all(root);
    return 1;
  }

  cgraph::ensure_unix_socket_dir(socket);
  const auto permissions = std::filesystem::status(socket.parent_path()).permissions();
  std::filesystem::remove_all(root);

  const auto group_or_other =
      std::filesystem::perms::group_read |
      std::filesystem::perms::group_write |
      std::filesystem::perms::group_exec |
      std::filesystem::perms::others_read |
      std::filesystem::perms::others_write |
      std::filesystem::perms::others_exec;

  if ((permissions & group_or_other) != std::filesystem::perms::none) {
    return 1;
  }

  return 0;
}
