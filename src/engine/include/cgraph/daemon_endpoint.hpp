#pragma once

#include "cgraph/daemon_identity.hpp"

#include <filesystem>

namespace cgraph {

[[nodiscard]] std::filesystem::path unix_runtime_dir();
[[nodiscard]] std::filesystem::path unix_socket_path(const DaemonIdentity& identity);
// True if a live daemon is currently listening on socket_path. A stale socket
// file left by a crashed daemon refuses the connection (returns false); a live
// listener accepts it (returns true). No-op false on Windows.
[[nodiscard]] bool unix_endpoint_is_live(const std::filesystem::path& socket_path);
void ensure_unix_socket_dir(const std::filesystem::path& socket_path);
[[nodiscard]] std::string windows_named_pipe_path(const DaemonIdentity& identity);
void configure_windows_named_pipe_security();

}  // namespace cgraph
