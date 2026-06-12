#pragma once

#include "cgraph/operation_stats.hpp"
#include "cgraph/types.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
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
  // Set at construction (~daemon start); uptime is derived from it in status,
  // never stored as a stale scalar.
  StatsClock::time_point start_time = StatsClock::now();
  EnrichmentState enrichment_state = EnrichmentState::Idle;
  std::size_t enrichment_pending = 0;
  std::size_t enrichment_running = 0;
  std::size_t enrichment_stale = 0;
  std::size_t enrichment_failed = 0;
  // Operation stats accumulated at the request-dispatch boundary: since-boot
  // lifetime totals plus a rolling recent window.
  DaemonOpStats op_stats;
  // Layer A inputs from the most recent (re)build, used to derive a modeled
  // cache-saving estimate in status. last_extract_mean_ms is 0 when the most
  // recent build extracted nothing (full cache hit), which suppresses the
  // estimate rather than fabricating one.
  double last_extract_mean_ms = 0.0;
  std::size_t last_files_cache_hit = 0;
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

// Marks an enrichment ingest as in-flight for the lifetime of the scope:
// enrichment_running carries the batch size and enrichment_state reports
// `running`, both observable by a concurrent `status` read. On scope exit the
// running count clears; the steady-state enrichment_state is recomputed by the
// next refresh. Mirrors ScopedTimer so the in-flight window is explicit at the
// call site rather than hand-managed.
class EnrichmentRunningScope {
 public:
  EnrichmentRunningScope(DaemonState& state, std::size_t batch_size) : state_(state) {
    state_.enrichment_running = batch_size;
    state_.enrichment_state = EnrichmentState::Running;
  }
  EnrichmentRunningScope(const EnrichmentRunningScope&) = delete;
  EnrichmentRunningScope& operator=(const EnrichmentRunningScope&) = delete;
  EnrichmentRunningScope(EnrichmentRunningScope&&) = delete;
  EnrichmentRunningScope& operator=(EnrichmentRunningScope&&) = delete;
  ~EnrichmentRunningScope() {
    state_.enrichment_running = 0;
    // Drop out of Running into the steady state implied by the current counts;
    // a subsequent refresh refines pending/stale, but Running never lingers.
    state_.enrichment_state = state_.enrichment_failed > 0  ? EnrichmentState::Failed
                              : state_.enrichment_pending > 0 ? EnrichmentState::Pending
                                                             : EnrichmentState::Idle;
  }

 private:
  DaemonState& state_;
};

[[nodiscard]] std::shared_ptr<const GraphSnapshot> read_graph_snapshot(const DaemonState& state);
void publish_graph_snapshot(DaemonState& state, GraphSnapshot graph);
void mutate_graph_snapshot(DaemonState& state, const std::function<void(GraphSnapshot&)>& mutator);
[[nodiscard]] nlohmann::json handle_daemon_request(DaemonState& state, const nlohmann::json& request);

}  // namespace cgraph
