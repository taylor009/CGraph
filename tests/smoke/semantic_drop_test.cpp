#include "cgraph/semantic_drop.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

bool has_drop(const std::vector<cgraph::SemanticFragmentDrop>& drops, std::size_t chunk_index) {
  for (const auto& drop : drops) {
    if (drop.chunk_index == chunk_index) {
      return true;
    }
  }
  return false;
}

bool has_event(
    const std::vector<cgraph::SemanticFragmentDropEvent>& events,
    std::size_t chunk_index,
    cgraph::SemanticFragmentDropChange change) {
  for (const auto& event : events) {
    if (event.drop.chunk_index == chunk_index && event.change == change) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  const auto root = std::filesystem::temp_directory_path() / "cgraph-semantic-drop-test";
  const auto drop_dir = root / "graphify-out" / "semantic-drop";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(drop_dir);

  cgraph::SemanticFragmentDropWatcher watcher(drop_dir, cgraph::SemanticFragmentDropWatcherOptions{.debounce = 0ms});
  const auto start = cgraph::FileWatcherClock::time_point{} + 100s;
  if (!watcher.poll(start).empty()) {
    return 1;
  }

  write_file(drop_dir / "chunk_02.json", "{\"nodes\":[],\"edges\":[]}\n");
  write_file(drop_dir / "chunk_01.json", "{\"nodes\":[],\"edges\":[]}\n");
  write_file(drop_dir / "chunk_alpha.json", "{}\n");
  write_file(drop_dir / "notes.json", "{}\n");

  const auto discovered = cgraph::discover_semantic_fragment_drops(drop_dir);
  if (discovered.size() != 2 || discovered[0].chunk_index != 1 || discovered[1].chunk_index != 2 ||
      !has_drop(discovered, 1) || !has_drop(discovered, 2)) {
    return 1;
  }

  auto events = watcher.poll(start + 1ms);
  if (events.size() != 2 || !has_event(events, 1, cgraph::SemanticFragmentDropChange::Created) ||
      !has_event(events, 2, cgraph::SemanticFragmentDropChange::Created)) {
    return 1;
  }

  write_file(drop_dir / "chunk_01.json", "{\"nodes\":[{\"id\":\"x\"}],\"edges\":[]}\n");
  events = watcher.poll(start + 2ms);
  if (events.size() != 1 || !has_event(events, 1, cgraph::SemanticFragmentDropChange::Modified)) {
    return 1;
  }

  std::filesystem::remove(drop_dir / "chunk_02.json");
  events = watcher.poll(start + 3ms);
  if (events.size() != 1 || !has_event(events, 2, cgraph::SemanticFragmentDropChange::Deleted)) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
