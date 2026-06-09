#pragma once

#include "cgraph/file_watcher.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace cgraph {

struct SemanticFragmentDrop {
  std::filesystem::path path;
  std::size_t chunk_index = 0;
};

enum class SemanticFragmentDropChange {
  Created,
  Modified,
  Deleted,
};

struct SemanticFragmentDropEvent {
  SemanticFragmentDrop drop;
  SemanticFragmentDropChange change = SemanticFragmentDropChange::Created;
};

struct SemanticFragmentDropWatcherOptions {
  std::chrono::milliseconds debounce{250};
};

class SemanticFragmentDropWatcher {
 public:
  struct FileState {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified_at{};
    SemanticFragmentDrop drop;
  };

  SemanticFragmentDropWatcher(std::filesystem::path drop_dir, SemanticFragmentDropWatcherOptions options = {});

  [[nodiscard]] std::vector<SemanticFragmentDropEvent> poll(FileWatcherClock::time_point now);

 private:
  struct PendingEvent {
    SemanticFragmentDropEvent event;
    FileWatcherClock::time_point first_seen{};
  };

  std::filesystem::path drop_dir_;
  SemanticFragmentDropWatcherOptions options_;
  bool initialized_ = false;
  std::unordered_map<std::string, FileState> known_;
  std::unordered_map<std::string, PendingEvent> pending_;
};

[[nodiscard]] std::vector<SemanticFragmentDrop> discover_semantic_fragment_drops(
    const std::filesystem::path& drop_dir);

}  // namespace cgraph
