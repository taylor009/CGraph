#include "cgraph/daemon_server.hpp"

#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/incremental_update.hpp"
#include "cgraph/index_persistence.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/semantic_cache.hpp"
#include "cgraph/semantic_chunk_plan.hpp"
#include "cgraph/semantic_drop.hpp"
#include "cgraph/semantic_ingest.hpp"
#include "cgraph/semantic_orchestration.hpp"

#include "cgraph/file_watcher.hpp"

#include <unordered_map>

#include <array>
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

int run_daemon_server(const std::filesystem::path& root, DaemonServerOptions options) {
  const auto identity = daemon_identity_for(root);
  const auto socket_path = unix_socket_path(identity);
  ensure_unix_socket_dir(socket_path);
  ::unlink(socket_path.c_str());  // clear any stale endpoint from a crashed daemon

  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::cerr << "graphd: socket() failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  sockaddr_un addr{};
  if (!fill_sockaddr(addr, socket_path.string())) {
    std::cerr << "graphd: socket path too long: " << socket_path << '\n';
    ::close(listen_fd);
    return 1;
  }
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "graphd: bind() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    return 1;
  }
  if (::listen(listen_fd, 16) != 0) {
    std::cerr << "graphd: listen() failed: " << std::strerror(errno) << '\n';
    ::close(listen_fd);
    (void)cleanup_daemon_endpoint(socket_path);
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
  SemanticCache cache = read_semantic_cache(cache_path);
  SemanticFragmentDropWatcher drop_watcher(drop_dir);

  const auto refresh_enrichment_state = [&]() {
    SemanticChunkPlanOptions plan_options;
    plan_options.excluded_dirs = {drop_dir};
    if (drop_dir.has_parent_path()) {
      plan_options.excluded_dirs.push_back(drop_dir.parent_path());
    }
    const auto plan = plan_semantic_chunks(identity.project_root, cache, plan_options);
    std::size_t pending = 0;
    for (const auto& chunk : plan.chunks) {
      pending += chunk.inputs.size();
    }
    state.enrichment_pending = pending;
    state.enrichment_stale = plan.stale_inputs;
    state.enrichment_state = state.enrichment_failed > 0  ? EnrichmentState::Failed
                             : pending > 0                ? EnrichmentState::Pending
                                                          : EnrichmentState::Idle;
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

  // Writes graph.json + the file manifest atomically. Logged, never fatal: a
  // failed persist only means the next restart rebuilds rather than fast-loads.
  const auto persist_graph_and_manifest = [&]() {
    if (!persist_graph_snapshot(state, graph_path)) {
      std::cerr << "graphd: failed to persist " << graph_path << '\n';
      return;
    }
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

  const auto rescan = [&]() {
    const std::scoped_lock lock(graph_mutex);
    const auto result = full_stat_index_rescan(state, index, identity.project_root);
    ingest_all_drops();
    // Persist as soon as the graph is final (post semantic overlay) and before
    // enrichment planning, which walks the whole project and can take seconds on
    // a large repo. The Tier-1 cache should land promptly, not behind planning.
    persist_graph_and_manifest();
    refresh_enrichment_state();
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
    // Log the fast-path load immediately — before enrichment planning, which
    // walks the whole project and can take seconds.
    std::cerr << "graphd: loaded persisted graph (" << detected.size() << " files unchanged)\n";
    refresh_enrichment_state();
    return true;
  };
  state.update_handler = [&](const nlohmann::json&) { return rescan(); };

  // Build on a worker thread so the accept loop below starts serving at once.
  // rescan() locks graph_mutex; status/query answer from the empty snapshot until
  // the build publishes the real one.
  std::thread build_thread;
  if (options.build_graph_on_start) {
    build_thread = std::thread([&] {
      if (!try_load_persisted()) {
        (void)rescan();
      }
    });
  }
  (void)drop_watcher.poll(FileWatcherClock::now());  // prime: existing drops are already overlaid

  std::cerr << "graphd listening on " << socket_path << " for root " << identity.project_root << '\n';

  auto last_activity = FileWatcherClock::now();
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
      bool merged_any = false;
      for (const auto& event : events) {
        if (event.change == SemanticFragmentDropChange::Deleted) {
          continue;
        }
        merged_any = ingest_drop(event.drop, sources) || merged_any;
      }
      if (merged_any) {
        write_semantic_cache(cache, cache_path);
      }
      refresh_enrichment_state();
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
      (void)write_frame(conn, response);
    }
    ::close(conn);
  }

  if (build_thread.joinable()) {
    build_thread.join();  // captures locals by reference: join before they leave scope
  }
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
