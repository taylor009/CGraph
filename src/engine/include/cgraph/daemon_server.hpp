#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <optional>

namespace cgraph {

struct DaemonServerOptions {
  // Exit after this much inactivity so idle per-project daemons don't linger.
  std::chrono::seconds idle_timeout{300};
  // When false, the graph is built synchronously before the socket is accepted
  // on (the default); tests can disable to serve an empty graph immediately.
  bool build_graph_on_start = true;
  // Directory the daemon watches for host-dropped chunk_NN.json fragments.
  // Empty -> default_semantic_drop_dir(root / "cgraph-out").
  std::filesystem::path drop_dir;
  // How often the serve loop polls the drop directory for new fragments.
  std::chrono::milliseconds drop_poll_interval{200};
};

// Runs the per-project daemon: builds the deterministic graph for `root`, binds
// the per-root Unix socket endpoint, and serves length-prefixed JSON-frame
// requests (query/path/explain/update/status/shutdown) until a shutdown op or
// the idle timeout. Returns 0 on clean exit, non-zero on a setup failure.
[[nodiscard]] int run_daemon_server(const std::filesystem::path& root, DaemonServerOptions options = {});

// Connects to a daemon's Unix socket, sends one request frame, and returns the
// decoded response. Returns nullopt if the socket is absent/unreachable or the
// exchange fails — the signal the thin client uses to spawn and retry.
[[nodiscard]] std::optional<nlohmann::json> request_over_unix_socket(
    const std::filesystem::path& socket_path,
    const nlohmann::json& request);

}  // namespace cgraph
