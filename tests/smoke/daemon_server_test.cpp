#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/protocol.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

// Records a failed expectation with a name, so a CI flake says WHICH step
// failed instead of a bare non-zero exit.
void expect(bool& ok, bool condition, const char* what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << '\n';
    ok = false;
  }
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
  options.idle_timeout = std::chrono::seconds(60);
  options.build_graph_on_start = true;  // build the real graph so update has a baseline
  // Fast watch/persist cadence so live-watch coverage below runs in test time.
  options.code_poll_interval = std::chrono::milliseconds(50);
  options.watch_debounce = std::chrono::milliseconds(50);
  options.persist_interval = std::chrono::seconds(1);

  int server_rc = -1;
  std::thread server([&] { server_rc = cgraph::run_daemon_server(root, options); });

  bool ok = true;

  // Status round-trips immediately (the daemon serves while building on a worker
  // thread), so poll until the initial build publishes the baseline graph
  // (file + function node) rather than assuming it is ready on the first reply.
  int nodes_before = 0;
  for (int attempt = 0; attempt < 200 && nodes_before < 2; ++attempt) {
    const auto status = request_with_retry(socket_path, cgraph::make_request("status"));
    expect(ok, status && (*status)["ok"] == true, "status round-trip");
    nodes_before = status ? (*status)["result"].value("node_count", 0) : 0;
    if (nodes_before < 2) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, nodes_before >= 2, "initial build published baseline graph");

  // Single-owner bind: with daemon A resident on this root, a second daemon start
  // for the SAME root must defer (return 0) without unlinking A's live endpoint, and
  // A must keep answering. Regression guard against endpoint theft on a start race.
  const int defer_rc = cgraph::run_daemon_server(root, options);
  expect(ok, defer_rc == 0, "second daemon for a served root defers cleanly");
  const auto still_a = request_with_retry(socket_path, cgraph::make_request("status"));
  expect(ok, still_a && (*still_a)["ok"] == true,
         "original daemon still owns the socket after a deferred start");

  // Enrichment re-plan gating: the initial plan runs once after the build; a
  // code-only `update .` must NOT re-plan (no doc tree walk), but a doc change
  // must. enrichment_plans_run is the observable: a re-plan increments it.
  int plans_run = 0;
  for (int attempt = 0; attempt < 300 && plans_run < 1; ++attempt) {
    const auto s = request_with_retry(socket_path, cgraph::make_request("status"));
    plans_run = s ? (*s)["result"].value("enrichment_plans_run", 0) : 0;
    if (plans_run < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, plans_run >= 1, "initial enrichment plan ran after build");

  const auto code_update = request_with_retry(socket_path, cgraph::make_request("update", {{"path", "."}}));
  expect(ok, code_update && (*code_update)["ok"] == true, "code-only update accepted");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // several watcher poll windows
  const auto after_code = request_with_retry(socket_path, cgraph::make_request("status"));
  const int plans_after_code = after_code ? (*after_code)["result"].value("enrichment_plans_run", 0) : -1;
  expect(ok, plans_after_code == plans_run, "code-only rescan did not re-plan enrichment");

  write_file(root / "notes.md", "# Notes\nsome documentation\n");
  bool replanned = false;
  for (int attempt = 0; attempt < 600 && !replanned; ++attempt) {
    const auto s = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status"));
    replanned = s && (*s)["result"].value("enrichment_plans_run", 0) > plans_run;
    if (!replanned) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, replanned, "doc change triggered an enrichment re-plan");

  // Add a new source file, then `update .` must rescan and grow the graph.
  write_file(root / "src" / "beta.ts", "export function beta() { return alpha(); }\n");
  const auto update = request_with_retry(socket_path, cgraph::make_request("update", {{"path", "."}}));
  expect(ok, update && (*update)["ok"] == true && (*update)["result"]["full_rescan"] == true, "update op full rescan");
  const auto nodes_after = update ? (*update)["result"].value("node_count", 0) : 0;
  expect(ok, nodes_after > nodes_before, "update grew the graph");

  // The newly-added symbol is now queryable on the resident daemon.
  const auto query = request_with_retry(socket_path, cgraph::make_request("query", {{"q", "beta"}}));
  expect(ok, query && (*query)["ok"] == true && !(*query)["result"]["nodes"].empty(), "added symbol queryable");

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
  expect(ok, enriched, "valid fragment merged into live snapshot");
  const auto topic = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "Topic"}}));
  expect(ok, topic && !(*topic)["result"]["nodes"].empty(), "enriched concept queryable");

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
  expect(ok, failed_seen && nodes_at_fail == nodes_pre_enrich + 2, "malformed fragment rejected, graph unchanged");

  // Live watching: a new source file lands in the graph with NO update op. The
  // gitignored peer (written first, same watch window) must never enter.
  write_file(root / ".gitignore", "scratch\n");
  write_file(root / "scratch" / "hidden.ts", "export function hidden() { return 0; }\n");
  write_file(root / "src" / "gamma.ts", "export function gamma() { return 2; }\n");
  bool watched = false;
  for (int attempt = 0; attempt < 600 && !watched; ++attempt) {
    const auto q = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "gamma"}}));
    watched = q && !(*q)["result"]["nodes"].empty();
    if (!watched) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, watched, "live watcher picked up new file without update op");

  // status reports the watcher and counts the applied update.
  const auto watch_status = request_with_retry(socket_path, cgraph::make_request("status"));
  expect(ok,
         watch_status && (*watch_status)["result"].value("watching", false) &&
             (*watch_status)["result"].value("incremental_updates", 0) >= 1,
         "status reports watcher and applied update");

  // The semantic overlay survives the code-only incremental rebuild (fragments
  // are re-overlaid after the rebuild publishes).
  bool overlay_kept = false;
  for (int attempt = 0; attempt < 200 && !overlay_kept; ++attempt) {
    const auto q = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "Topic"}}));
    overlay_kept = q && !(*q)["result"]["nodes"].empty();
    if (!overlay_kept) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, overlay_kept, "semantic overlay survived incremental rebuild");

  // The gitignored file never entered the graph (gamma arriving proves the same
  // watch window was processed).
  const auto hidden = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "hidden"}}));
  expect(ok, hidden && (*hidden)["result"]["nodes"].empty(), "gitignored file never entered the graph");

  // Incremental state is re-persisted: graph.json on disk gains the new symbol
  // once the persist interval elapses (no update op, no shutdown).
  bool persisted = false;
  for (int attempt = 0; attempt < 600 && !persisted; ++attempt) {
    std::ifstream input(root / "cgraph-out" / "graph.json");
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    persisted = contents.find("gamma") != std::string::npos;
    if (!persisted) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, persisted, "incremental state re-persisted to graph.json");

  // An unknown op is an error, not a crash; shutdown stops the loop cleanly.
  const auto bad = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("nonsense"));
  expect(ok, bad && (*bad)["ok"] == false, "unknown op rejected");
  const int nodes_at_shutdown =
      watch_status ? (*watch_status)["result"].value("node_count", 0) : 0;
  const auto shutdown = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("shutdown"));
  expect(ok, shutdown && (*shutdown)["ok"] == true, "shutdown accepted");

  server.join();
  expect(ok, server_rc == 0, "first server exited cleanly");

  // Restart on the same root: the daemon fast-loads the persisted graph, so the
  // extraction index is NOT hydrated. The first live edit must trigger a
  // hydrating full rescan — a per-file rebuild here would collapse the graph to
  // just the changed file (regression coverage for exactly that bug).
  int server2_rc = -1;
  std::thread server2([&] { server2_rc = cgraph::run_daemon_server(root, options); });
  int nodes_loaded = 0;
  for (int attempt = 0; attempt < 600 && nodes_loaded < nodes_at_shutdown; ++attempt) {
    const auto s = request_with_retry(socket_path, cgraph::make_request("status"));
    nodes_loaded = s ? (*s)["result"].value("node_count", 0) : 0;
    if (nodes_loaded < nodes_at_shutdown) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, nodes_loaded >= nodes_at_shutdown, "fast-load restored the full graph");

  write_file(root / "src" / "delta.ts", "export function delta() { return 3; }\n");
  bool delta_seen = false;
  for (int attempt = 0; attempt < 600 && !delta_seen; ++attempt) {
    const auto q = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("query", {{"q", "delta"}}));
    delta_seen = q && !(*q)["result"]["nodes"].empty();
    if (!delta_seen) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  expect(ok, delta_seen, "first edit after fast-load picked up live");
  const auto post_edit = request_with_retry(socket_path, cgraph::make_request("status"));
  const int nodes_post_edit = post_edit ? (*post_edit)["result"].value("node_count", 0) : 0;
  expect(ok, nodes_post_edit > nodes_at_shutdown, "post-restart edit grew the graph (no collapse)");

  const auto shutdown2 = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("shutdown"));
  expect(ok, shutdown2 && (*shutdown2)["ok"] == true, "second shutdown accepted");
  server2.join();
  expect(ok, server2_rc == 0, "second server exited cleanly");

  // Never-idle + stale-socket reclaim on a fresh root. (a) A daemon started with
  // idle_timeout == 0 must reclaim a stale (non-live) socket file left at its
  // endpoint by a crashed predecessor and bind anyway. (b) It must stay resident
  // well past what a positive timeout would have killed, because idle shutdown is
  // disabled — the property that lets the supervisor keep it alive indefinitely.
  const auto root2 = fs::temp_directory_path() / "cgraph_daemon_never_idle_test";
  fs::remove_all(root2);
  fs::create_directories(root2);
  write_file(root2 / "src" / "one.ts", "export function one() { return 1; }\n");
  const auto socket2 = cgraph::unix_socket_path(cgraph::daemon_identity_for(root2));
  cgraph::ensure_unix_socket_dir(socket2);
  write_file(socket2, "stale, not a live listener");  // stale endpoint from a "crash"

  cgraph::DaemonServerOptions immortal = options;
  immortal.idle_timeout = std::chrono::seconds::zero();  // never idle-shut-down
  immortal.build_graph_on_start = false;                 // serve immediately, no build wait
  int immortal_rc = -1;
  std::thread immortal_server([&] { immortal_rc = cgraph::run_daemon_server(root2, immortal); });

  const auto reclaimed = request_with_retry(socket2, cgraph::make_request("status"));
  expect(ok, reclaimed && (*reclaimed)["ok"] == true, "daemon reclaimed the stale socket and serves");

  // With a positive timeout the daemon would exit after idle_timeout of inactivity;
  // with 0 it must still answer after several idle select windows (200ms each).
  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  const auto still_up = request_with_retry(socket2, cgraph::make_request("status"));
  expect(ok, still_up && (*still_up)["ok"] == true, "idle_timeout==0 keeps the daemon resident");

  const auto shutdown3 = cgraph::request_over_unix_socket(socket2, cgraph::make_request("shutdown"));
  expect(ok, shutdown3 && (*shutdown3)["ok"] == true, "never-idle daemon shuts down on explicit op");
  immortal_server.join();
  expect(ok, immortal_rc == 0, "never-idle server exited cleanly");
  fs::remove_all(root2);

  fs::remove_all(root);
  return ok ? 0 : 1;
}
