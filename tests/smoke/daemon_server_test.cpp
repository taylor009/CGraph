#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/protocol.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

std::optional<nlohmann::json> request_with_retry(const std::filesystem::path& socket_path, const nlohmann::json& req) {
  std::optional<nlohmann::json> response;
  for (int attempt = 0; attempt < 100 && !response; ++attempt) {
    response = cgraph::request_over_unix_socket(socket_path, req);
    if (!response) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  return response;
}

}  // namespace

// Exercises the real Unix-socket daemon end to end: a server thread binds the
// per-root endpoint, a client connects over an actual socket, status/query
// round-trip through the length-prefixed protocol, and `update .` triggers a
// live rescan that picks up a newly-added source file. No mocks.
int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "cgraph_daemon_server_test";
  fs::remove_all(root);
  fs::create_directories(root);
  write_file(root / "src" / "alpha.ts", "export function alpha() { return 1; }\n");

  const auto identity = cgraph::daemon_identity_for(root);
  const auto socket_path = cgraph::unix_socket_path(identity);
  fs::remove(socket_path);

  cgraph::DaemonServerOptions options;
  options.idle_timeout = std::chrono::seconds(5);
  options.build_graph_on_start = true;  // build the real graph so update has a baseline

  int server_rc = -1;
  std::thread server([&] { server_rc = cgraph::run_daemon_server(root, options); });

  bool ok = true;

  // Status round-trips and reports the baseline graph (file + function node).
  const auto status = request_with_retry(socket_path, cgraph::make_request("status"));
  ok = ok && status && (*status)["ok"] == true;
  const auto nodes_before = status ? (*status)["result"].value("node_count", 0) : 0;
  ok = ok && nodes_before >= 2;

  // Add a new source file, then `update .` must rescan and grow the graph.
  write_file(root / "src" / "beta.ts", "export function beta() { return alpha(); }\n");
  const auto update = request_with_retry(socket_path, cgraph::make_request("update", {{"path", "."}}));
  ok = ok && update && (*update)["ok"] == true && (*update)["result"]["full_rescan"] == true;
  const auto nodes_after = update ? (*update)["result"].value("node_count", 0) : 0;
  ok = ok && nodes_after > nodes_before;

  // The newly-added symbol is now queryable on the resident daemon.
  const auto query = request_with_retry(socket_path, cgraph::make_request("query", {{"q", "beta"}}));
  ok = ok && query && (*query)["ok"] == true && !(*query)["result"]["nodes"].empty();

  // An unknown op is an error, not a crash; shutdown stops the loop cleanly.
  const auto bad = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("nonsense"));
  ok = ok && bad && (*bad)["ok"] == false;
  const auto shutdown = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("shutdown"));
  ok = ok && shutdown && (*shutdown)["ok"] == true;

  server.join();
  ok = ok && server_rc == 0;

  fs::remove_all(root);
  return ok ? 0 : 1;
}
