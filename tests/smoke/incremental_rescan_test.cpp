#include "cgraph/incremental_update.hpp"

#include "cgraph/daemon_ops.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

bool has_event(
    const std::vector<cgraph::FileWatchEvent>& events,
    const std::filesystem::path& path,
    cgraph::FileWatchChange change) {
  for (const auto& event : events) {
    if (event.path == path && event.change == change) {
      return true;
    }
  }
  return false;
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
  const auto canonical_expected = std::filesystem::weakly_canonical(path).generic_string();
  for (const auto& node : graph.nodes) {
    if (node.source_file == expected || node.source_file == canonical_expected) {
      return true;
    }
  }
  return false;
}

int assert_watcher_overflow() {
  using namespace std::chrono_literals;

  const auto root = std::filesystem::temp_directory_path() / "cgraph-incremental-rescan-overflow-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  cgraph::FileWatcher watcher(root, cgraph::FileWatcherOptions{.debounce = 0ms, .max_pending_events = 2});
  const auto start = cgraph::FileWatcherClock::time_point{} + 100s;
  if (!watcher.poll(start).empty()) {
    return 1;
  }

  write_file(root / "one.py", "class One:\n    pass\n");
  write_file(root / "two.py", "class Two:\n    pass\n");
  write_file(root / "three.py", "class Three:\n    pass\n");

  const auto events = watcher.poll(start + 1ms);
  if (events.size() != 1 || !has_event(events, root, cgraph::FileWatchChange::Overflow)) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}

int assert_explicit_rescan_replaces_stat_index() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-incremental-explicit-rescan-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto keep = root / "keep.py";
  const auto stale = root / "stale.py";
  const auto fresh = root / "fresh.py";

  write_file(keep, "class Keep:\n    pass\n");
  write_file(stale, "class Stale:\n    pass\n");

  cgraph::DaemonState state;
  cgraph::IncrementalGraphIndex index;
  auto result = cgraph::full_stat_index_rescan(state, index, root);
  auto graph = cgraph::read_graph_snapshot(state);
  if (!result.full_rescan || result.files_reextracted != 2 || result.files_removed != 0 ||
      !has_node_label(*graph, "Keep") || !has_node_label(*graph, "Stale")) {
    return 1;
  }

  std::filesystem::remove(stale);
  write_file(fresh, "class Fresh:\n    pass\n");

  // A full rescan reuses unchanged files (keep.py) from the existing index and
  // re-extracts only the new one (fresh.py): files_reextracted == 1, not 2. The
  // resulting graph is identical to re-extracting everything.
  result = cgraph::full_stat_index_rescan(state, index, root);
  graph = cgraph::read_graph_snapshot(state);
  if (!result.full_rescan || result.files_reextracted != 1 || result.files_removed != 1 ||
      !has_node_label(*graph, "Keep") || !has_node_label(*graph, "Fresh") ||
      has_node_label(*graph, "Stale") || has_source(*graph, stale)) {
    return 1;
  }

  // keep.py was reused, fresh.py re-extracted: one hit of two files -> 0.5, in
  // (0,1]. The previously-dead cache_hit_rate now carries a real value, and the
  // modeled-saving inputs are recorded (a hit plus a positive per-file mean).
  if (result.files_cache_hit != 1 || graph->cache_hit_rate != 0.5) {
    return 2;
  }
  if (state.last_files_cache_hit != 1 || state.last_extract_mean_ms <= 0.0) {
    return 3;
  }

  std::filesystem::remove_all(root);
  return 0;
}

int assert_overflow_update_uses_full_rescan() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-incremental-overflow-update-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto source = root / "source.py";
  write_file(source, "class FromOverflow:\n    pass\n");

  cgraph::DaemonState state;
  cgraph::IncrementalGraphIndex index;
  const cgraph::FileWatchEvent events[] = {
      {.path = root, .change = cgraph::FileWatchChange::Overflow, .kind = cgraph::WatchedFileKind::Code},
  };

  const auto result = cgraph::apply_incremental_code_updates(state, index, events);
  const auto graph = cgraph::read_graph_snapshot(state);
  if (!result.full_rescan || result.files_reextracted != 1 || !has_node_label(*graph, "FromOverflow") ||
      !has_source(*graph, source)) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}

}  // namespace

int main() {
  if (assert_watcher_overflow() != 0) {
    return 1;
  }
  if (assert_explicit_rescan_replaces_stat_index() != 0) {
    return 1;
  }
  if (assert_overflow_update_uses_full_rescan() != 0) {
    return 1;
  }
  return 0;
}
