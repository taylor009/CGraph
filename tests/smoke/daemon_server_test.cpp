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

  // Semantic enrichment: drop a valid fragment into the daemon's drop dir and
  // the watcher must merge it into the live snapshot (no update op issued).
  const auto nodes_pre_enrich = nodes_after;
  write_file(root / "cgraph-out" / "semantic-drop" / "chunk_00.json", R"({
    "nodes": [
      {"id": "doc:guide", "label": "Guide", "type": "document"},
      {"id": "concept:topic", "label": "Topic", "type": "concept"}
    ],
    "edges": [{"source": "doc:guide", "target": "concept:topic", "relation": "DESCRIBES"}]
  })");
  bool enriched = false;
  for (int attempt = 0; attempt < 200 && !enriched; ++attempt) {
    const auto s = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status"));
    enriched = s && (*s)["result"].value("node_count", 0) >= nodes_pre_enrich + 2;
    if (!enriched) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  ok = ok && enriched;
  const auto topic = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "Topic"}}));
  ok = ok && topic && !(*topic)["result"]["nodes"].empty();

  // A malformed fragment is rejected: enrichment_state goes failed, graph unchanged.
  write_file(root / "cgraph-out" / "semantic-drop" / "chunk_01.json", R"({"nodes":[{"id":"x"}],"edges":[]})");
  bool failed_seen = false;
  int nodes_at_fail = 0;
  for (int attempt = 0; attempt < 200 && !failed_seen; ++attempt) {
    const auto s = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status"));
    if (s && (*s)["result"]["enrichment_state"] == "failed") {
      failed_seen = true;
      nodes_at_fail = (*s)["result"].value("node_count", 0);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  ok = ok && failed_seen && nodes_at_fail == nodes_pre_enrich + 2;  // bad fragment added nothing

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
