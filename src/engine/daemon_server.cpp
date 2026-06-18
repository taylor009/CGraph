#include "cgraph/daemon_server.hpp"

#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/incremental_update.hpp"
#include "cgraph/index_persistence.hpp"
#include "cgraph/operation_stats.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/semantic_cache.hpp"
#include "cgraph/semantic_chunk_plan.hpp"
#include "cgraph/semantic_drop.hpp"
#include "cgraph/graph_builder.hpp"
#include "cgraph/semantic_fragment_validation.hpp"
#include "cgraph/semantic_ingest.hpp"
#include "cgraph/semantic_orchestration.hpp"

#include "cgraph/file_watcher.hpp"

#include <unordered_map>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
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

namespace {

// Open a Unix-domain listening socket at socket_path (clearing any stale endpoint
// from a crashed daemon). Returns the listen fd, or -1 on failure (already logged).
// Shared by the full build-and-watch server and the static seam server.
[[nodiscard]] int open_listen_socket(const std::filesystem::path& socket_path) {
  ensure_unix_socket_dir(socket_path);
  ::unlink(socket_path.c_str());

  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::cerr << "graphd: socket() failed: " << std::strerror(errno) << '\n';
    return -1;
  }
  sockaddr_un addr{};
  if (!fill_sockaddr(addr, socket_path.string())) {
    std::cerr << "graphd: socket path too long: " << socket_path << '\n';
    ::close(listen_fd);
    return -1;
  }
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "graphd: bind() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    return -1;
  }
  if (::listen(listen_fd, 16) != 0) {
    std::cerr << "graphd: listen() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    (void)cleanup_daemon_endpoint(socket_path);
    return -1;
  }
  return listen_fd;
}

}  // namespace

// A static, read-only daemon serving a pre-fused seam graph: load graph.json,
// publish the snapshot, and answer the read ops via handle_daemon_request -- no
// build, no watcher, no persistence, no enrichment. `update` reloads graph.json
// (re-fuse -> update refreshes); writes are rejected (no memory_dir). Selected by
// graphd when the root carries a seam marker (see is_seam_directory).
int run_static_seam_server(const std::filesystem::path& root, DaemonServerOptions options) {
  const auto identity = daemon_identity_for(root);
  const auto socket_path = unix_socket_path(identity);
  const int listen_fd = open_listen_socket(socket_path);
  if (listen_fd < 0) {
    return 1;
  }

  DaemonState state;
  state.pid = ::getpid();
  const auto graph_path = root / "graph.json";
  if (!load_graph_snapshot(state, graph_path)) {
    std::cerr << "graphd: failed to load seam graph: " << graph_path << '\n';
    ::close(listen_fd);
    (void)cleanup_daemon_endpoint(socket_path);
    return 1;
  }
  state.update_handler = [&state, graph_path](const nlohmann::json&) -> nlohmann::json {
    if (load_graph_snapshot(state, graph_path)) {
      const auto graph = read_graph_snapshot(state);
      return {{"reloaded", true}, {"nodes", graph->nodes.size()}, {"edges", graph->edges.size()}};
    }
    return {{"reloaded", false}};
  };
  std::cerr << "graphd: serving static seam graph " << graph_path << " ("
            << read_graph_snapshot(state)->nodes.size() << " nodes)\n";

  auto last_activity = FileWatcherClock::now();
  while (!state.shutdown_requested) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd, &read_set);
    timeval timeout{};
    timeout.tv_sec = 1;
    const int ready = ::select(listen_fd + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (ready == 0) {
      if (FileWatcherClock::now() - last_activity >= options.idle_timeout) {
        std::cerr << "graphd: idle timeout, shutting down\n";
        break;
      }
      continue;
    }
    const int conn = ::accept(listen_fd, nullptr, nullptr);
    if (conn < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    last_activity = FileWatcherClock::now();
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

int run_daemon_server(const std::filesystem::path& root, DaemonServerOptions options) {
  const auto identity = daemon_identity_for(root);
  const auto socket_path = unix_socket_path(identity);
  const int listen_fd = open_listen_socket(socket_path);
  if (listen_fd < 0) {
    return 1;
  }

  DaemonState state;
  state.pid = ::getpid();

  // Serve immediately. Publish an empty graph so status/query answer right away
  // (build_state Empty, node_count 0) while the initial build runs on a worker
  // thread — otherwise a large repo's multi-minute cold build blocks every
  // connection, including a trivial `status`, until it finishes.
  publish_graph_snapshot(state, GraphSnapshot{});

  // Serializes graph-mutating work (the initial build, `update` rescans, and
  // live fragment ingestion) so the build thread and the serve loop never race
  // on the file index or semantic cache. Read-only ops (status/query/explain/
  // path/context) take only the snapshot lock and stay responsive during a build.
  std::mutex graph_mutex;

  // Semantic enrichment: the daemon watches a drop directory for host-computed
  // chunk_NN.json fragments, merges valid ones into the live snapshot through
  // the single-writer path, and surfaces progress via the enrichment_* status
  // fields. Source attribution for each fragment comes from the plan manifest.
  const auto drop_dir = options.drop_dir.empty()
                            ? default_semantic_drop_dir(identity.project_root / "cgraph-out")
                            : options.drop_dir;
  const auto cache_path = drop_dir / "semantic-cache.json";
  // Session-memory checkpoint sidecars live here, separate from semantic-drop, so
  // the two host-authored layers are overlaid and managed independently.
  const auto memory_dir = identity.project_root / "cgraph-out" / "memory";
  SemanticCache cache = read_semantic_cache(cache_path);
  // Stat cache for the enrichment plan walk: lets a refresh reuse stored hashes
  // for unchanged docs/media instead of re-reading and re-hashing every file.
  // Persisted next to the semantic cache so a restart stays cheap.
  const auto stat_index_path = drop_dir / "semantic-stat-index.json";
  SemanticStatIndex stat_index = read_semantic_stat_index(stat_index_path);
  SemanticFragmentDropWatcher drop_watcher(drop_dir);

  // Enrichment planning (plan_semantic_chunks) walks the whole project and
  // hashes every doc/media file — seconds on a large repo. It only produces
  // informational status counts, so it must not block the build/update response.
  // It runs on a dedicated worker: callers signal request_refresh() (cheap) and
  // return; the worker snapshots the cache under graph_mutex, plans OFF-lock
  // (never blocking builds), then writes the counts back under graph_mutex.
  std::mutex refresh_mutex;
  std::condition_variable refresh_cv;
  bool refresh_requested = false;
  bool refresh_stop = false;

  const auto run_enrichment_refresh = [&]() {
    SemanticCache snapshot;
    SemanticStatIndex index_snapshot;
    {
      const std::scoped_lock lock(graph_mutex);
      snapshot = cache;              // quick copies; release before the slow walk
      index_snapshot = stat_index;
    }
    SemanticChunkPlanOptions plan_options;
    plan_options.excluded_dirs = {drop_dir};
    if (drop_dir.has_parent_path()) {
      plan_options.excluded_dirs.push_back(drop_dir.parent_path());
    }
    // The plan reuses stored hashes for unchanged files (StatHit) and updates the
    // snapshot index in place with what it saw this pass.
    const auto plan = plan_semantic_chunks(identity.project_root, snapshot, plan_options, &index_snapshot);
    std::size_t pending = 0;
    for (const auto& chunk : plan.chunks) {
      pending += chunk.inputs.size();
    }
    {
      const std::scoped_lock lock(graph_mutex);
      stat_index = index_snapshot;  // publish the refreshed stat entries
      ++state.enrichment_plans_run;
      state.enrichment_pending = pending;
      state.enrichment_stale = plan.stale_inputs;
      state.enrichment_state = state.enrichment_failed > 0  ? EnrichmentState::Failed
                               : pending > 0                ? EnrichmentState::Pending
                                                            : EnrichmentState::Idle;
    }
    // Persist only when something was newly hashed (the index meaningfully
    // changed); a pure stat-hit pass leaves the file untouched.
    if (plan.files_hashed > 0) {
      write_semantic_stat_index(index_snapshot, stat_index_path);
    }
  };

  const auto request_refresh = [&]() {
    {
      const std::scoped_lock lock(refresh_mutex);
      refresh_requested = true;
    }
    refresh_cv.notify_one();
  };

  // Merges one dropped fragment, caching it against its manifest source(s).
  const auto ingest_drop = [&](const SemanticFragmentDrop& drop,
                               const std::unordered_map<std::size_t, std::vector<std::filesystem::path>>& sources) {
    const auto entry = sources.find(drop.chunk_index);
    const std::filesystem::path source =
        (entry != sources.end() && !entry->second.empty()) ? entry->second.front() : drop.path;
    const auto result = ingest_semantic_fragment(state, cache, source, drop.path);
    if (!result.merged) {
      ++state.enrichment_failed;
      return false;
    }
    if (entry != sources.end()) {
      for (std::size_t i = 1; i < entry->second.size(); ++i) {
        cache.upsert(make_semantic_cache_record(entry->second[i], drop.path, SemanticCacheState::Valid));
      }
    }
    return true;
  };

  // Re-overlays every present fragment. Run after a deterministic rescan (which
  // rebuilds from code only and would otherwise drop merged semantic nodes).
  const auto ingest_all_drops = [&]() {
    const auto sources = load_chunk_sources(drop_dir);
    bool merged_any = false;
    for (const auto& drop : discover_semantic_fragment_drops(drop_dir)) {
      merged_any = ingest_drop(drop, sources) || merged_any;
    }
    if (merged_any) {
      write_semantic_cache(cache, cache_path);
    }
  };

  // Re-overlays every session-memory checkpoint sidecar (cgraph-out/memory/*.json).
  // Memory nodes are snapshot-only and a rebuild (from index.files) drops them; the
  // sidecars are the durable source of truth, re-merged here after every rebuild so
  // checkpoints survive restarts, incremental edits, and full rescans. merge_fragment
  // is first-occurrence-wins, so re-applying an already-present checkpoint is a no-op.
  const auto ingest_all_memory = [&]() {
    std::error_code ec;
    std::size_t applied = 0;
    if (!std::filesystem::exists(memory_dir, ec)) {
      state.last_memory_overlay_count = 0;
      return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(memory_dir, ec)) {
      if (ec) {
        break;
      }
      if (entry.path().extension() != ".json") {
        continue;
      }
      auto validation = validate_semantic_fragment_file(entry.path());
      if (!validation.valid) {
        continue;
      }
      mutate_graph_snapshot(state, [&](GraphSnapshot& graph) { merge_fragment(graph, validation.fragment); });
      ++applied;
    }
    state.last_memory_overlay_count = applied;  // observability: size of the last re-overlay pass
  };

  // The daemon owns the incremental file index: startup and every `update` op
  // rebuild the graph the same way (a full stat-index rescan), then re-overlay
  // semantic fragments, so `update .` keeps the resident snapshot current
  // without discarding enrichment. Wired through the injectable handler so all
  // op dispatch stays in handle_daemon_request.
  // Persisted artifacts under the project output dir. After every full rescan we
  // write graph.json plus a version-stamped manifest of the file set it was built
  // from, so a later restart with an unchanged tree can serve straight from disk.
  const auto out_dir = identity.project_root / "cgraph-out";
  const auto graph_path = out_dir / "graph.json";
  const auto manifest_path = out_dir / "index-manifest.json";

  IncrementalGraphIndex index;
  // Whether index.files holds extractions for the whole tree. A full rescan
  // hydrates it; the Tier-1 fast path (persisted graph load) does NOT — it only
  // publishes the snapshot. Incremental updates rebuild the graph from
  // index.files, so applying one against a non-hydrated index would replace the
  // full graph with just the changed files.
  std::atomic<bool> index_hydrated{false};

  // Periodic persistence of incremental state: watcher/fragment changes mark the
  // graph dirty, and the serve loop re-persists graph.json + manifest once the
  // change has been memory-only for persist_interval (and again on exit), so a
  // crash or idle shutdown never loses more than that window.
  DaemonLifecycleState lifecycle;
  DaemonLifecycleConfig lifecycle_config;
  lifecycle_config.endpoint_path = socket_path;
  lifecycle_config.graph_path = graph_path;
  lifecycle_config.idle_timeout = options.idle_timeout;
  lifecycle_config.persist_interval = options.persist_interval;

  // Writes the file manifest the persisted graph was built from. Logged, never
  // fatal: a failed persist only means the next restart rebuilds, not fast-loads.
  const auto persist_manifest = [&]() {
    IndexManifest manifest;
    manifest.version_key = index_version_key();
    manifest.files.reserve(index.cache.size());
    for (const auto& [_, entry] : index.cache) {
      manifest.files.push_back(entry);
    }
    if (!write_index_manifest(manifest, manifest_path)) {
      std::cerr << "graphd: failed to persist " << manifest_path << '\n';
    }
  };

  // Writes graph.json + the file manifest atomically.
  const auto persist_graph_and_manifest = [&]() {
    if (!persist_graph_snapshot(state, graph_path)) {
      std::cerr << "graphd: failed to persist " << graph_path << '\n';
      return;
    }
    persist_manifest();
  };

  const auto rescan = [&]() {
    const std::scoped_lock lock(graph_mutex);
    const auto result = full_stat_index_rescan(state, index, identity.project_root);
    index_hydrated.store(true);
    ingest_all_drops();
    ingest_all_memory();  // re-overlay session-memory checkpoints dropped by the code-only rebuild
    // Persist as soon as the graph is final (post semantic overlay) and before
    // enrichment planning, which walks the whole project and can take seconds on
    // a large repo. The Tier-1 cache should land promptly, not behind planning.
    persist_graph_and_manifest();
    // No enrichment re-plan here: a rescan is a code-only operation and does not
    // change the doc/media set. The plan runs once at startup and thereafter only
    // on doc/media watcher events, so an `update .` no longer walks the doc tree.
    const auto graph = read_graph_snapshot(state);
    return nlohmann::json{
        {"accepted", true},
        {"full_rescan", result.full_rescan},
        {"files_reextracted", result.files_reextracted},
        {"files_removed", result.files_removed},
        {"node_count", graph->nodes.size()},
        {"edge_count", graph->edges.size()},
    };
  };

  // Tier-1 fast path: if the persisted manifest's version key still matches this
  // binary and every detected file is a stat/hash hit against it (no files added
  // or removed), load the persisted graph and overlay semantic drops instead of
  // re-extracting. Returns false (rebuild needed) on any miss.
  const auto try_load_persisted = [&]() -> bool {
    const auto manifest = read_index_manifest(manifest_path);
    if (!manifest || manifest->version_key != index_version_key()) {
      return false;
    }
    const auto detected = detect_project_files(identity.project_root);
    if (!tree_matches_manifest(*manifest, detected)) {
      return false;
    }
    const std::scoped_lock lock(graph_mutex);
    if (!load_graph_snapshot(state, graph_path)) {
      return false;
    }
    ingest_all_drops();
    ingest_all_memory();  // checkpoints re-overlaid from sidecars; graph.json holds none
    // Log the fast-path load immediately — before enrichment planning, which
    // walks the whole project and can take seconds.
    std::cerr << "graphd: loaded persisted graph (" << detected.size() << " files unchanged)\n";
    return true;
  };
  state.update_handler = [&](const nlohmann::json&) {
    auto result = rescan();
    // rescan persisted graph + manifest itself; nothing is memory-only now.
    // Runs on the serve-loop thread, the same thread that marks/persists.
    lifecycle.graph_dirty = false;
    return result;
  };
  // Session-memory checkpoint bodies are written under cgraph-out/memory by the
  // `remember` op; the node points at the markdown via source_file.
  state.memory_dir = memory_dir;

  // Live code watching: the serve loop polls the project tree on its own cadence
  // and folds changed files into the graph incrementally. The watcher is primed
  // (baseline snapshot, no events) on the build thread right before the initial
  // build, so changes that land while the build runs still surface as events on
  // the first post-build poll; the loop only polls once the baseline graph
  // exists (initial_build_done), so an event can never rebuild from a
  // half-populated index.
  const bool watch_code = options.code_poll_interval.count() > 0;
  FileWatcher code_watcher(
      identity.project_root,
      FileWatcherOptions{.debounce = options.watch_debounce, .max_pending_events = options.watch_max_pending});
  std::atomic<bool> initial_build_done{false};
  state.watching = watch_code && options.build_graph_on_start;

  // Build on a worker thread so the accept loop below starts serving at once.
  // rescan() locks graph_mutex; status/query answer from the empty snapshot until
  // the build publishes the real one.
  std::thread build_thread;
  if (options.build_graph_on_start) {
    build_thread = std::thread([&] {
      if (watch_code) {
        (void)code_watcher.poll(FileWatcherClock::now());  // prime: baseline only, no events
      }
      if (!try_load_persisted()) {
        (void)rescan();
      }
      // Plan enrichment exactly once after the initial build/load to populate the
      // pending/stale counts. Thereafter only doc/media changes (and drop ingests)
      // re-plan; a code-only rescan does not.
      request_refresh();
      initial_build_done.store(true);  // hands the watcher to the serve loop
    });
  }

  // Enrichment-refresh worker: coalesces refresh requests (many builds/drops in a
  // burst trigger one re-plan) and runs the slow walk off the build/serve path.
  std::thread enrichment_worker([&] {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(refresh_mutex);
        refresh_cv.wait(lock, [&] { return refresh_requested || refresh_stop; });
        if (refresh_stop) {
          return;
        }
        refresh_requested = false;
      }
      run_enrichment_refresh();
    }
  });

  (void)drop_watcher.poll(FileWatcherClock::now());  // prime: existing drops are already overlaid

  std::cerr << "graphd listening on " << socket_path << " for root " << identity.project_root << '\n';

  auto last_activity = FileWatcherClock::now();
  auto last_code_poll = FileWatcherClock::now();
  while (!state.shutdown_requested) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd, &read_set);
    timeval timeout{};
    timeout.tv_usec = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(options.drop_poll_interval).count() % 1'000'000);
    timeout.tv_sec = static_cast<time_t>(
        std::chrono::duration_cast<std::chrono::seconds>(options.drop_poll_interval).count());

    const int ready = ::select(listen_fd + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "graphd: select() failed: " << std::strerror(errno) << '\n';
      break;
    }

    // Ingest any debounced fragment drops discovered since the last tick.
    if (const auto events = drop_watcher.poll(FileWatcherClock::now()); !events.empty()) {
      const std::scoped_lock lock(graph_mutex);  // serialize with the build/rescan thread
      const auto sources = load_chunk_sources(drop_dir);
      // Mark the batch in-flight so a concurrent `status` reports enrichment as
      // running; the scope clears the running count on exit and request_refresh
      // recomputes the steady state below.
      const EnrichmentRunningScope running(state, events.size());
      bool merged_any = false;
      for (const auto& event : events) {
        if (event.change == SemanticFragmentDropChange::Deleted) {
          continue;
        }
        merged_any = ingest_drop(event.drop, sources) || merged_any;
      }
      if (merged_any) {
        write_semantic_cache(cache, cache_path);
        mark_graph_dirty(lifecycle, DaemonClock::now());  // overlay is memory-only until persisted
      }
      request_refresh();
    }

    // Fold live code changes into the graph. Each watcher poll walks the project
    // tree, so it runs on its own (slower) cadence than the drop poll, and only
    // once the initial build has published a baseline.
    if (watch_code && initial_build_done.load() &&
        FileWatcherClock::now() - last_code_poll >= options.code_poll_interval) {
      last_code_poll = FileWatcherClock::now();
      const auto events = code_watcher.poll(last_code_poll);
      bool code_changed = false;
      bool docs_changed = false;
      for (const auto& event : events) {
        if (event.change == FileWatchChange::Overflow || event.kind == WatchedFileKind::Code) {
          code_changed = true;
        } else {
          docs_changed = true;
        }
      }
      if (code_changed) {
        // Neighborhood dedup keeps each incremental update fast, but it skips
        // the fuzzy-duplicate merges a full pass makes elsewhere in the graph,
        // so the node set drifts above the canonical build's. Reconciling with
        // a full dedup every Nth update bounds that drift; an explicit `update`
        // op or a restart rescan also reconverges it.
        constexpr std::size_t kFullDedupReconcileEvery = 5;
        const std::scoped_lock lock(graph_mutex);
        IncrementalUpdateResult result;
        if (index_hydrated.load()) {
          result = apply_incremental_code_updates(
              state, index, events, IncrementalDedupPolicy{.full_reconcile_every = kFullDedupReconcileEvery});
        } else {
          // First edit after a fast-load restart: hydrate the index with one
          // full rescan (a per-file rebuild here would wipe the graph), then
          // subsequent events go incremental.
          result = full_stat_index_rescan(state, index, identity.project_root);
          index_hydrated.store(true);
        }
        ingest_all_drops();   // the rebuild is code-only; re-overlay semantic fragments
        ingest_all_memory();  // and re-overlay session-memory checkpoints
        ++state.incremental_updates;
        mark_graph_dirty(lifecycle, DaemonClock::now());
        last_activity = FileWatcherClock::now();  // active editing keeps the daemon alive
        std::cerr << "graphd: incremental update (" << result.files_reextracted << " re-extracted, "
                  << result.files_removed << " removed" << (result.full_rescan ? ", full rescan" : "") << ")\n";
      }
      if (docs_changed) {
        request_refresh();  // doc/media changes only move the enrichment plan
      }
    }

    // Re-persist graph + manifest once incremental changes have aged past the
    // persist interval, so a crash loses at most that window.
    if (persist_if_due(state, lifecycle, lifecycle_config, DaemonClock::now())) {
      const std::scoped_lock lock(graph_mutex);
      persist_manifest();
      std::cerr << "graphd: persisted incremental graph state\n";
    }

    if (ready == 0) {
      if (FileWatcherClock::now() - last_activity >= options.idle_timeout) {
        std::cerr << "graphd: idle timeout, shutting down\n";
        break;
      }
      continue;  // no connection this tick; keep polling drops
    }

    const int conn = ::accept(listen_fd, nullptr, nullptr);
    if (conn < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    last_activity = FileWatcherClock::now();
    // The thin client uses one connection per request: read it, answer it, close.
    if (const auto request = read_frame(conn)) {
      const auto response = handle_daemon_request(state, *request);
      // A successful `remember` mutates the snapshot; mark it dirty so the
      // checkpoint node is persisted into graph.json and survives a restart.
      if (request->value("op", std::string{}) == "remember" && response.value("ok", false)) {
        mark_graph_dirty(lifecycle, DaemonClock::now());
      }
      (void)write_frame(conn, response);
    }
    ::close(conn);
  }

  if (build_thread.joinable()) {
    build_thread.join();  // captures locals by reference: join before they leave scope
  }
  // Flush any incremental state still memory-only (the persist interval had not
  // elapsed) so an idle timeout or shutdown op never drops applied changes.
  if (lifecycle.graph_dirty) {
    const std::scoped_lock lock(graph_mutex);
    persist_graph_and_manifest();
    std::cerr << "graphd: persisted incremental graph state on exit\n";
  }
  // Best-effort durable op-stats flush: append this lifetime's op-stats to a JSONL
  // ledger so query activity survives idle-shutdown and aggregates across restarts.
  // Gated on >=1 substantive op so idle status-only spawns write nothing. The wall
  // clock is read once here; boot is derived from the monotonic uptime, so the live
  // daemon stayed purely monotonic. Never blocks, throws, or fails the shutdown.
  if (state.op_stats.has_substantive_ops()) {
    const auto shutdown_wall = WallClock::now();
    const auto boot_wall = shutdown_wall - std::chrono::duration_cast<WallClock::duration>(
                                               StatsClock::now() - state.start_time);
    if (!append_op_stats_ledger(out_dir / "op-stats-ledger.jsonl",
                                op_stats_ledger_line(state.op_stats, boot_wall, shutdown_wall))) {
      std::cerr << "graphd: op-stats ledger flush failed (non-fatal)\n";
    }
  }
  {
    const std::scoped_lock lock(refresh_mutex);
    refresh_stop = true;
  }
  refresh_cv.notify_one();
  enrichment_worker.join();  // also captures locals by reference; join before scope exit
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
