#include "cgraph/daemon_lifecycle.hpp"

#include "cgraph/daemon_ops.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

cgraph::GraphSnapshot sample_graph() {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "a", .label = "Alpha", .source_file = "a.cpp", .kind = "class",
                                     .source_location = cgraph::SourceLocation{
                                         .start_line = 10, .start_column = 1, .end_line = 25, .end_column = 2}});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .source_file = "b.cpp", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});
  graph.build_state = cgraph::BuildState::DeterministicReady;
  graph.cache_hit_rate = 0.5;
  return graph;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  const auto root = std::filesystem::temp_directory_path() / "cgraph-daemon-lifecycle-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  cgraph::DaemonState state;
  cgraph::publish_graph_snapshot(state, sample_graph());

  const auto graph_path = root / "graph.json";
  if (!cgraph::persist_graph_snapshot(state, graph_path)) {
    return 1;
  }

  cgraph::DaemonState reloaded;
  if (!cgraph::load_graph_snapshot(reloaded, graph_path)) {
    return 1;
  }
  const auto snapshot = cgraph::read_graph_snapshot(reloaded);
  if (snapshot->nodes.size() != 2 || snapshot->edges.size() != 1 ||
      snapshot->build_state != cgraph::BuildState::DeterministicReady || snapshot->cache_hit_rate != 0.5) {
    return 1;
  }

  // Regression: the source span must survive persist -> load. Without it a
  // fast-loaded daemon serves location-less, snippet-less context and starves
  // the slice-cost knapsack packer (research/2510.00446).
  const cgraph::Node* alpha = nullptr;
  for (const auto& node : snapshot->nodes) {
    if (node.id == "a") {
      alpha = &node;
    }
  }
  if (alpha == nullptr || !alpha->source_location || alpha->source_location->start_line != 10 ||
      alpha->source_location->end_line != 25 || alpha->source_location->end_column != 2) {
    return 1;
  }

  const auto endpoint = root / "graphd.sock";
  {
    std::ofstream output(endpoint);
    output << "stale endpoint";
  }
  if (!std::filesystem::exists(endpoint) || !cgraph::cleanup_daemon_endpoint(endpoint) ||
      std::filesystem::exists(endpoint) || !cgraph::cleanup_daemon_endpoint(endpoint)) {
    return 1;
  }

  cgraph::DaemonLifecycleConfig config{
      .endpoint_path = endpoint,
      .graph_path = graph_path,
      .idle_timeout = 30s,
      .persist_interval = 10s,
  };
  cgraph::DaemonLifecycleState lifecycle;
  const auto start = cgraph::DaemonClock::time_point{} + 100s;
  cgraph::record_daemon_activity(lifecycle, start);
  if (cgraph::should_shutdown_for_idle(lifecycle, config, start + 29s)) {
    return 1;
  }
  if (!cgraph::should_shutdown_for_idle(lifecycle, config, start + 30s)) {
    return 1;
  }

  cgraph::mark_graph_dirty(lifecycle, start + 1s);
  if (cgraph::persist_if_due(state, lifecycle, config, start + 9s)) {
    return 1;
  }
  // Re-marking while already dirty must NOT push the deadline out: dirty_since
  // anchors to the first unpersisted change, so a steady edit stream still
  // persists once the interval elapses.
  cgraph::mark_graph_dirty(lifecycle, start + 10s);
  if (lifecycle.dirty_since != start + 1s) {
    return 1;
  }
  if (!cgraph::persist_if_due(state, lifecycle, config, start + 11s) || lifecycle.graph_dirty) {
    return 1;
  }
  if (cgraph::persist_if_due(state, lifecycle, config, start + 12s)) {
    return 1;
  }
  // After a persist, the next mark re-anchors dirty_since.
  cgraph::mark_graph_dirty(lifecycle, start + 20s);
  if (lifecycle.dirty_since != start + 20s || !lifecycle.graph_dirty) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
