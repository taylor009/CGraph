#include "cgraph/incremental_update.hpp"

#include "cgraph/daemon_ops.hpp"

#include <filesystem>

namespace {

cgraph::ExtractionResult fragment_for(const char* file, const char* id, const char* label) {
  cgraph::ExtractionResult result;
  result.fragment.nodes.push_back(cgraph::Node{.id = id, .label = label, .source_file = file, .kind = "class"});
  return result;
}

bool has_node(const cgraph::GraphSnapshot& graph, const char* id) {
  for (const auto& node : graph.nodes) {
    if (node.id == id) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  cgraph::DaemonState state;
  cgraph::IncrementalGraphIndex index;
  index.files.emplace("alpha.cpp", fragment_for("alpha.cpp", "alpha", "PaymentService"));
  index.files.emplace("alpha_renamed.cpp", fragment_for("alpha_renamed.cpp", "alpha_renamed", "Payment Service"));
  index.files.emplace("legacy_a.cpp", fragment_for("legacy_a.cpp", "legacy_a", "CustomerRepository"));
  index.files.emplace("legacy_b.cpp", fragment_for("legacy_b.cpp", "legacy_b", "Customer Repository"));

  cgraph::IncrementalDedupPolicy policy{
      .full_reconcile_every = 2,
  };

  const cgraph::FileWatchEvent first_update[] = {
      {.path = "alpha_renamed.cpp", .change = cgraph::FileWatchChange::Modified, .kind = cgraph::WatchedFileKind::Code},
  };
  auto result = cgraph::apply_incremental_code_updates(state, index, first_update, policy);
  auto graph = cgraph::read_graph_snapshot(state);
  if (!result.neighborhood_deduped || result.full_dedup_reconciled || graph->nodes.size() != 3 ||
      !has_node(*graph, "alpha") || has_node(*graph, "alpha_renamed") ||
      !has_node(*graph, "legacy_a") || !has_node(*graph, "legacy_b")) {
    return 1;
  }

  const cgraph::FileWatchEvent second_update[] = {
      {.path = "legacy_b.cpp", .change = cgraph::FileWatchChange::Modified, .kind = cgraph::WatchedFileKind::Code},
  };
  result = cgraph::apply_incremental_code_updates(state, index, second_update, policy);
  graph = cgraph::read_graph_snapshot(state);
  if (!result.neighborhood_deduped || !result.full_dedup_reconciled || graph->nodes.size() != 2 ||
      !has_node(*graph, "alpha") || !has_node(*graph, "legacy_a") ||
      has_node(*graph, "alpha_renamed") || has_node(*graph, "legacy_b")) {
    return 1;
  }

  return 0;
}
