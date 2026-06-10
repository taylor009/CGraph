#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/protocol.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>

// Exercises the real Unix-socket daemon end to end: a server thread binds the
// per-root endpoint, a client connects over an actual socket, and a status
// request round-trips through the length-prefixed frame protocol. No mocks.
int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "cgraph_daemon_server_test";
  fs::remove_all(root);
  fs::create_directories(root);

  const auto identity = cgraph::daemon_identity_for(root);
  const auto socket_path = cgraph::unix_socket_path(identity);
  // Clear any stale endpoint so bind() succeeds on a clean run.
  fs::remove(socket_path);

  cgraph::DaemonServerOptions options;
  options.idle_timeout = std::chrono::seconds(5);
  options.build_graph_on_start = false;  // serve immediately; transport is what we test

  int server_rc = -1;
  std::thread server([&] { server_rc = cgraph::run_daemon_server(root, options); });

  // Poll until the server is accepting, then status must round-trip.
  std::optional<nlohmann::json> status;
  for (int attempt = 0; attempt < 100 && !status; ++attempt) {
    status = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status"));
    if (!status) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  bool ok = status.has_value() && (*status)["ok"] == true &&
            (*status)["result"].contains("node_count");

  // An unknown op must be reported as an error, not crash the server.
  const auto bad = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("nonsense"));
  ok = ok && bad.has_value() && (*bad)["ok"] == false;

  // Shutdown must be acknowledged and stop the server loop.
  const auto shutdown = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("shutdown"));
  ok = ok && shutdown.has_value() && (*shutdown)["ok"] == true;

  server.join();
  ok = ok && server_rc == 0;

  fs::remove_all(root);
  return ok ? 0 : 1;
}
