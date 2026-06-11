#pragma once

#include "cgraph/types.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <mutex>

namespace cgraph {

enum class EnrichmentState {
  Idle,
  Pending,
  Running,
  Stale,
  Failed,
};

struct DaemonState {
  std::shared_ptr<const GraphSnapshot> graph_snapshot = std::make_shared<GraphSnapshot>();
  mutable std::mutex snapshot_mutex;
  mutable std::mutex writer_mutex;
  bool shutdown_requested = false;
  int pid = 0;
  double uptime_seconds = 0.0;
  EnrichmentState enrichment_state = EnrichmentState::Idle;
  std::size_t enrichment_pending = 0;
  std::size_t enrichment_running = 0;
  std::size_t enrichment_stale = 0;
  std::size_t enrichment_failed = 0;
  // Live code watching: whether the serve loop folds file changes into the
  // graph automatically, and how many incremental updates it has applied.
  bool watching = false;
  std::size_t incremental_updates = 0;
  // Performs a deterministic rebuild for an `update` op and returns its result
  // payload. Injected by the running daemon (which owns the file index and
  // project root); when unset, `update` is accepted as a no-op so in-process
  // callers and tests need no rebuild wiring.
  std::function<nlohmann::json(const nlohmann::json& params)> update_handler;
};

[[nodiscard]] std::shared_ptr<const GraphSnapshot> read_graph_snapshot(const DaemonState& state);
void publish_graph_snapshot(DaemonState& state, GraphSnapshot graph);
void mutate_graph_snapshot(DaemonState& state, const std::function<void(GraphSnapshot&)>& mutator);
[[nodiscard]] nlohmann::json handle_daemon_request(DaemonState& state, const nlohmann::json& request);

}  // namespace cgraph
