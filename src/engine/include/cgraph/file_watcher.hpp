#pragma once

#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace cgraph {

using FileWatcherClock = std::chrono::steady_clock;

enum class WatchedFileKind {
  Code,
  Document,
  Media,
};

enum class FileWatchChange {
  Created,
  Modified,
  Deleted,
  Overflow,
};

struct FileWatchEvent {
  std::filesystem::path path;
  FileWatchChange change = FileWatchChange::Modified;
  WatchedFileKind kind = WatchedFileKind::Code;
};

struct FileWatcherOptions {
  std::chrono::milliseconds debounce{250};
  std::size_t max_pending_events = 0;
};

class FileWatcher {
 public:
  struct FileState {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified_at{};
    WatchedFileKind kind = WatchedFileKind::Code;
  };

  FileWatcher(std::filesystem::path root, FileWatcherOptions options = {});

  [[nodiscard]] std::vector<FileWatchEvent> poll(FileWatcherClock::time_point now);

 private:
  struct PendingEvent {
    FileWatchEvent event;
    FileWatcherClock::time_point first_seen{};
  };

  std::filesystem::path root_;
  FileWatcherOptions options_;
  bool initialized_ = false;
  std::unordered_map<std::string, FileState> known_;
  std::unordered_map<std::string, PendingEvent> pending_;
};

[[nodiscard]] WatchedFileKind classify_watched_file(const std::filesystem::path& path);
[[nodiscard]] bool is_watchable_file(const std::filesystem::path& path);

}  // namespace cgraph
