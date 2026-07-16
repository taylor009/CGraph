#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/protocol.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// Opens a raw connected client socket to `socket_path`, or -1. Lets the wire
// hardening tests send hand-crafted (hostile) bytes the protocol codec would
// never produce -- an oversized length header, a truncated frame -- straight at
// the real daemon.
int raw_connect(const std::filesystem::path& socket_path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const auto path = socket_path.string();
  if (path.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    return -1;
  }
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

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

  // Wire hardening 1 (DoS): a 4-byte header declaring a ~4 GB body must be
  // rejected WITHOUT the daemon allocating that body, and the daemon must keep
  // serving. read_frame bails on any length above kMaxFrameBodyBytes before it
  // sizes the frame buffer; if it still allocated 0xFFFFFFFF bytes the process
  // would OOM/crash here instead of the next status answering.
  {
    const int fd = raw_connect(socket_path);
    expect(ok, fd >= 0, "raw connect for oversized-frame test");
    if (fd >= 0) {
      const std::array<std::uint8_t, 4> huge{0xFF, 0xFF, 0xFF, 0xFF};  // 0xFFFFFFFF body length
      (void)::write(fd, huge.data(), huge.size());
      ::close(fd);  // send no body: the daemon must not wait on ~4 GB it never allocates
    }
  }
  const auto after_huge = request_with_retry(socket_path, cgraph::make_request("status"));
  expect(ok, after_huge && (*after_huge)["ok"] == true,
         "daemon survived an oversized-length frame and still answers status");

  // Wire hardening 2 (hang): a client that sends a partial header and then goes
  // silent must not wedge the single-threaded serve loop. SO_RCVTIMEO drops the
  // stalled read after a few seconds; the proof is that a normal status on a
  // fresh connection still round-trips (the loop was never permanently blocked).
  {
    const int fd = raw_connect(socket_path);
    expect(ok, fd >= 0, "raw connect for stalled-reader test");
    if (fd >= 0) {
      const std::array<std::uint8_t, 2> partial{0x10, 0x00};  // 2 of 4 header bytes, then stall
      (void)::write(fd, partial.data(), partial.size());
      // Hold the socket open, sending nothing. The daemon serves inline on one
      // thread, so while it is stuck in read_exact on the missing header bytes it
      // cannot service another request -- until SO_RCVTIMEO (a few seconds) fires
      // and it drops the stalled peer. A fresh status must therefore eventually
      // succeed: the loop RECOVERS rather than wedging forever. Retry past the
      // timeout window (well over the 5s SO_RCVTIMEO) to prove recovery.
      std::optional<nlohmann::json> concurrent;
      for (int attempt = 0; attempt < 400 && !concurrent; ++attempt) {  // ~8s budget > 5s timeout
        concurrent = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status"));
        if (!concurrent || !(*concurrent).value("ok", false)) {
          concurrent.reset();
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
      expect(ok, concurrent && (*concurrent)["ok"] == true,
             "daemon recovers and answers status after a stalled mid-frame reader is timed out");
      ::close(fd);
    }
  }
  const auto after_stall = request_with_retry(socket_path, cgraph::make_request("status"));
  expect(ok, after_stall && (*after_stall)["ok"] == true,
         "daemon serve loop survived a stalled half-frame reader");

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

  // Verified update barrier: an equal-length source rewrite with its original
  // mtime restored must still produce a different root. Live watching is off
  // for this daemon, so the explicit update below is the only synchronizer.
  const auto freshness_root = fs::temp_directory_path() / "cgraph_daemon_freshness_test";
  fs::remove_all(freshness_root);
  const auto before = std::string{"export function alpha() { return 1; }\n"};
  const auto after = std::string{"export function omega() { return 1; }\n"};
  expect(ok, before.size() == after.size(), "freshness rewrite preserves source length");
  const auto freshness_source = freshness_root / "src" / "alpha.ts";
  write_file(freshness_source, before);
  const auto freshness_socket = cgraph::unix_socket_path(cgraph::daemon_identity_for(freshness_root));
  fs::remove(freshness_socket);

  cgraph::DaemonServerOptions freshness_options = options;
  freshness_options.code_poll_interval = std::chrono::milliseconds::zero();
  int freshness_rc = -1;
  std::thread freshness_server([&] { freshness_rc = cgraph::run_daemon_server(freshness_root, freshness_options); });

  const auto old_update = request_with_retry(freshness_socket, cgraph::make_request("update", {{"path", "."}}));
  const bool old_update_has_freshness = old_update && (*old_update).contains("result") &&
                                        (*old_update)["result"].contains("freshness");
  const auto old_root = old_update_has_freshness
                            ? (*old_update)["result"]["freshness"].value("content_root", std::string{})
                            : std::string{};
  expect(ok,
         old_update && (*old_update).value("ok", false) && old_update_has_freshness &&
             (*old_update)["result"]["freshness"].value("verified", false) &&
             (*old_update)["result"]["freshness"].value("algorithm", std::string{}) == "sha256-merkle-v1" &&
             !old_root.empty() && (*old_update)["result"].value("files_hashed", std::size_t{0}) == 1 &&
             (*old_update)["result"].value("bytes_hashed", std::size_t{0}) == before.size() &&
             (*old_update)["result"].contains("files_reextracted") &&
             (*old_update)["result"].contains("files_cache_hit") && (*old_update)["result"].contains("files_removed"),
         "verified update returns root identity and verification work counts");

  if (!old_root.empty()) {
    const auto preserved_mtime = fs::last_write_time(freshness_source);
    write_file(freshness_source, after);
    fs::last_write_time(freshness_source, preserved_mtime);

    const auto new_update = request_with_retry(freshness_socket, cgraph::make_request("update", {{"path", "."}}));
    const bool new_update_has_freshness = new_update && (*new_update).contains("result") &&
                                          (*new_update)["result"].contains("freshness");
    const auto new_root = new_update_has_freshness
                              ? (*new_update)["result"]["freshness"].value("content_root", std::string{})
                              : std::string{};
    expect(ok,
           new_update && (*new_update).value("ok", false) && new_update_has_freshness &&
               (*new_update)["result"]["freshness"].value("verified", false) &&
               (*new_update)["result"].value("files_hashed", std::size_t{0}) == 1 &&
               (*new_update)["result"].value("bytes_hashed", std::size_t{0}) == after.size() &&
               !new_root.empty() && new_root != old_root,
           "preserved-mtime rewrite produces a new verified root");

    const auto pinned_new = request_with_retry(
        freshness_socket,
        cgraph::make_request("query", {{"q", "omega"}, {"expected_content_root", new_root}}));
    expect(ok,
           pinned_new && (*pinned_new).value("ok", false) && !(*pinned_new)["result"]["nodes"].empty() &&
               (*pinned_new)["result"]["freshness"].value("verified", false) &&
               (*pinned_new)["result"]["freshness"].value("content_root", std::string{}) == new_root,
           "new root pins a query to the updated snapshot");

    const auto stale_query = request_with_retry(
        freshness_socket,
        cgraph::make_request("query", {{"q", "omega"}, {"expected_content_root", old_root}}));
    expect(ok,
           stale_query && !(*stale_query).value("ok", true) && !(*stale_query).contains("result"),
           "old root is rejected without a graph result");
  }

  const auto freshness_shutdown =
      request_with_retry(freshness_socket, cgraph::make_request("shutdown"));
  expect(ok, freshness_shutdown && (*freshness_shutdown).value("ok", false), "freshness daemon shuts down cleanly");
  freshness_server.join();
  expect(ok, freshness_rc == 0, "freshness daemon exited cleanly");
  fs::remove_all(freshness_root);

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
