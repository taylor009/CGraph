#include "cgraph/incremental_update.hpp"

#include "cgraph/daemon_ops.hpp"
#include "cgraph/pipeline.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

bool has_node_label(const cgraph::GraphSnapshot& graph, const std::string& label) {
  for (const auto& node : graph.nodes) {
    if (node.label == label) {
      return true;
    }
  }
  return false;
}

bool has_source(const cgraph::GraphSnapshot& graph, const std::filesystem::path& path) {
  const auto expected = path.generic_string();
  for (const auto& node : graph.nodes) {
    if (node.source_file == expected) {
      return true;
    }
  }
  return false;
}

std::unordered_set<std::string> node_labels(const cgraph::GraphSnapshot& graph) {
  std::unordered_set<std::string> labels;
  for (const auto& node : graph.nodes) {
    labels.insert(node.label);
  }
  return labels;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-incremental-update-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto alpha = root / "alpha.py";
  const auto beta = root / "beta.py";
  const auto renamed = root / "renamed.py";

  cgraph::DaemonState state;
  cgraph::IncrementalGraphIndex index;

  write_file(alpha, "class Alpha:\n    pass\n");
  write_file(beta, "class Beta:\n    pass\n");
  cgraph::FileWatchEvent created[] = {
      {.path = alpha, .change = cgraph::FileWatchChange::Created, .kind = cgraph::WatchedFileKind::Code},
      {.path = beta, .change = cgraph::FileWatchChange::Created, .kind = cgraph::WatchedFileKind::Code},
  };
  auto result = cgraph::apply_incremental_code_updates(state, index, created);
  auto graph = cgraph::read_graph_snapshot(state);
  if (result.files_reextracted != 2 || !has_node_label(*graph, "Alpha") || !has_node_label(*graph, "Beta")) {
    return 1;
  }

  const auto alpha_modified_at = std::filesystem::last_write_time(alpha);
  std::filesystem::last_write_time(alpha, alpha_modified_at + std::chrono::seconds(5));
  cgraph::FileWatchEvent touched[] = {
      {.path = alpha, .change = cgraph::FileWatchChange::Modified, .kind = cgraph::WatchedFileKind::Code},
  };
  result = cgraph::apply_incremental_code_updates(state, index, touched);
  graph = cgraph::read_graph_snapshot(state);
  if (result.files_reextracted != 0 || result.files_removed != 0 || result.neighborhood_deduped ||
      !has_node_label(*graph, "Alpha") || !has_node_label(*graph, "Beta")) {
    return 1;
  }

  write_file(alpha, "class Gamma:\n    pass\n");
  cgraph::FileWatchEvent edited[] = {
      {.path = alpha, .change = cgraph::FileWatchChange::Modified, .kind = cgraph::WatchedFileKind::Code},
  };
  result = cgraph::apply_incremental_code_updates(state, index, edited);
  graph = cgraph::read_graph_snapshot(state);
  if (result.files_reextracted != 1 || has_node_label(*graph, "Alpha") || !has_node_label(*graph, "Gamma")) {
    return 1;
  }

  std::filesystem::rename(beta, renamed);
  cgraph::FileWatchEvent rename_events[] = {
      {.path = beta, .change = cgraph::FileWatchChange::Deleted, .kind = cgraph::WatchedFileKind::Code},
      {.path = renamed, .change = cgraph::FileWatchChange::Created, .kind = cgraph::WatchedFileKind::Code},
  };
  result = cgraph::apply_incremental_code_updates(state, index, rename_events);
  graph = cgraph::read_graph_snapshot(state);
  if (result.files_removed != 1 || result.files_reextracted != 1 || has_source(*graph, beta) || !has_source(*graph, renamed)) {
    return 1;
  }

  std::filesystem::remove(renamed);
  cgraph::FileWatchEvent deleted[] = {
      {.path = renamed, .change = cgraph::FileWatchChange::Deleted, .kind = cgraph::WatchedFileKind::Code},
  };
  result = cgraph::apply_incremental_code_updates(state, index, deleted);
  graph = cgraph::read_graph_snapshot(state);
  if (result.files_removed != 1 || has_node_label(*graph, "Beta") || has_source(*graph, renamed)) {
    return 1;
  }

  // The daemon's rescan must produce the same graph as the canonical one-shot
  // pipeline: identical labels AND identical node/edge counts. The count check
  // guards the pipeline ordering — the daemon must dedup before community
  // detection just like run_one_shot, or community-bucketed dedup over-merges
  // and the counts diverge.
  const auto full_build = cgraph::run_one_shot(root);
  graph = cgraph::read_graph_snapshot(state);
  if (node_labels(*graph) != node_labels(full_build.graph)) {
    return 1;
  }
  if (graph->nodes.size() != full_build.graph.nodes.size() ||
      graph->edges.size() != full_build.graph.edges.size()) {
    return 1;
  }
  // The daemon graph carries community + centrality (computed after dedup), so
  // ranked queries work on the resident snapshot.
  bool has_centrality = false;
  for (const auto& node : graph->nodes) {
    if (node.properties.contains("degree_centrality")) {
      has_centrality = true;
      break;
    }
  }
  if (!has_centrality) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
