#include "cgraph/file_watcher.hpp"

#include "cgraph/detect.hpp"
#include "cgraph/path_ignore.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cgraph {
namespace {

[[nodiscard]] std::string lower_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
      return static_cast<char>(ch - 'A' + 'a');
    }
    return static_cast<char>(ch);
  });
  return value;
}

[[nodiscard]] bool is_document_extension(std::string_view extension) {
  static const std::unordered_set<std::string_view> docs = {
      ".md",
      ".markdown",
      ".mdx",
      ".rst",
      ".txt",
      ".adoc",
      ".org",
      ".pdf",
      ".doc",
      ".docx",
  };
  return docs.contains(extension);
}

[[nodiscard]] bool is_media_extension(std::string_view extension) {
  static const std::unordered_set<std::string_view> media = {
      ".png",
      ".jpg",
      ".jpeg",
      ".gif",
      ".webp",
      ".svg",
      ".mp3",
      ".wav",
      ".m4a",
      ".mp4",
      ".mov",
      ".webm",
  };
  return media.contains(extension);
}

[[nodiscard]] std::string key_for(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

// Walk the watched tree with the same ignore rules as detect_project_files
// (shared skip-list + root .gitignore), so the watcher never emits an event for
// a file the deterministic pipeline would not index — otherwise an incremental
// update could add nodes a full rescan later drops.
[[nodiscard]] std::unordered_map<std::string, FileWatcher::FileState> scan_files(const std::filesystem::path& root) {
  std::unordered_map<std::string, FileWatcher::FileState> files;
  const auto gitignore_patterns = read_root_gitignore(root);
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;

  for (; !error && iterator != end; iterator.increment(error)) {
    const auto& entry = *iterator;
    const auto name = entry.path().filename().generic_string();
    if (entry.is_directory(error)) {
      if (is_skipped_directory(name) || matches_simple_gitignore(root, entry.path(), gitignore_patterns)) {
        iterator.disable_recursion_pending();
      }
      continue;
    }
    if (!entry.is_regular_file(error) || !is_watchable_file(entry.path())) {
      continue;
    }
    if (matches_simple_gitignore(root, entry.path(), gitignore_patterns)) {
      continue;
    }

    FileWatcher::FileState state;
    state.size = entry.file_size(error);
    if (error) {
      error.clear();
      continue;
    }
    state.modified_at = entry.last_write_time(error);
    if (error) {
      error.clear();
      continue;
    }
    state.kind = classify_watched_file(entry.path());
    files.emplace(key_for(entry.path()), state);
  }
  return files;
}

}  // namespace

FileWatcher::FileWatcher(std::filesystem::path root, FileWatcherOptions options)
    : root_(std::move(root)), options_(options) {}

std::vector<FileWatchEvent> FileWatcher::poll(FileWatcherClock::time_point now) {
  const auto current = scan_files(root_);
  if (!initialized_) {
    known_ = current;
    initialized_ = true;
    return {};
  }

  for (const auto& [key, state] : current) {
    const auto previous = known_.find(key);
    if (previous == known_.end()) {
      pending_[key] = PendingEvent{
          .event = FileWatchEvent{.path = std::filesystem::path(key), .change = FileWatchChange::Created, .kind = state.kind},
          .first_seen = now,
      };
      continue;
    }
    if (previous->second.size != state.size || previous->second.modified_at != state.modified_at) {
      pending_[key] = PendingEvent{
          .event = FileWatchEvent{.path = std::filesystem::path(key), .change = FileWatchChange::Modified, .kind = state.kind},
          .first_seen = now,
      };
    }
  }

  for (const auto& [key, state] : known_) {
    if (current.contains(key)) {
      continue;
    }
    pending_[key] = PendingEvent{
        .event = FileWatchEvent{.path = std::filesystem::path(key), .change = FileWatchChange::Deleted, .kind = state.kind},
        .first_seen = now,
    };
  }

  known_ = current;
  if (options_.max_pending_events > 0 && pending_.size() > options_.max_pending_events) {
    pending_.clear();
    return {FileWatchEvent{
        .path = root_,
        .change = FileWatchChange::Overflow,
        .kind = WatchedFileKind::Code,
    }};
  }

  std::vector<FileWatchEvent> ready;
  for (auto iterator = pending_.begin(); iterator != pending_.end();) {
    if (now - iterator->second.first_seen >= options_.debounce) {
      ready.push_back(std::move(iterator->second.event));
      iterator = pending_.erase(iterator);
    } else {
      ++iterator;
    }
  }

  std::ranges::sort(ready, [](const FileWatchEvent& lhs, const FileWatchEvent& rhs) {
    return lhs.path.generic_string() < rhs.path.generic_string();
  });
  return ready;
}

WatchedFileKind classify_watched_file(const std::filesystem::path& path) {
  if (detect_language(path) != DetectedLanguage::Unknown) {
    return WatchedFileKind::Code;
  }
  const auto extension = lower_ascii(path.extension().generic_string());
  if (is_media_extension(extension)) {
    return WatchedFileKind::Media;
  }
  return WatchedFileKind::Document;
}

bool is_watchable_file(const std::filesystem::path& path) {
  if (detect_language(path) != DetectedLanguage::Unknown) {
    return true;
  }
  const auto extension = lower_ascii(path.extension().generic_string());
  return is_document_extension(extension) || is_media_extension(extension);
}

}  // namespace cgraph
