#include "cgraph/semantic_drop.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace cgraph {
namespace {

[[nodiscard]] std::optional<std::size_t> parse_chunk_index(const std::filesystem::path& path) {
  const auto name = path.filename().generic_string();
  constexpr std::string_view prefix = "chunk_";
  constexpr std::string_view suffix = ".json";
  if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() == prefix.size() + suffix.size()) {
    return std::nullopt;
  }

  std::size_t value = 0;
  for (std::size_t index = prefix.size(); index < name.size() - suffix.size(); ++index) {
    const auto digit = name[index];
    if (digit < '0' || digit > '9') {
      return std::nullopt;
    }
    value = (value * 10U) + static_cast<std::size_t>(digit - '0');
  }
  return value;
}

[[nodiscard]] std::string key_for(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::unordered_map<std::string, SemanticFragmentDropWatcher::FileState> scan_drops(
    const std::filesystem::path& drop_dir) {
  std::unordered_map<std::string, SemanticFragmentDropWatcher::FileState> drops;
  std::error_code error;
  std::filesystem::directory_iterator iterator(drop_dir, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::directory_iterator end;

  for (; !error && iterator != end; iterator.increment(error)) {
    const auto& entry = *iterator;
    if (!entry.is_regular_file(error)) {
      continue;
    }
    const auto chunk_index = parse_chunk_index(entry.path());
    if (!chunk_index.has_value()) {
      continue;
    }

    SemanticFragmentDropWatcher::FileState state;
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
    state.drop = SemanticFragmentDrop{.path = entry.path(), .chunk_index = *chunk_index};
    drops.emplace(key_for(entry.path()), std::move(state));
  }
  return drops;
}

}  // namespace

SemanticFragmentDropWatcher::SemanticFragmentDropWatcher(
    std::filesystem::path drop_dir,
    SemanticFragmentDropWatcherOptions options)
    : drop_dir_(std::move(drop_dir)), options_(options) {}

std::vector<SemanticFragmentDropEvent> SemanticFragmentDropWatcher::poll(FileWatcherClock::time_point now) {
  const auto current = scan_drops(drop_dir_);
  if (!initialized_) {
    known_ = current;
    initialized_ = true;
    return {};
  }

  for (const auto& [key, state] : current) {
    const auto previous = known_.find(key);
    if (previous == known_.end()) {
      pending_[key] = PendingEvent{
          .event = SemanticFragmentDropEvent{.drop = state.drop, .change = SemanticFragmentDropChange::Created},
          .first_seen = now,
      };
      continue;
    }
    if (previous->second.size != state.size || previous->second.modified_at != state.modified_at) {
      pending_[key] = PendingEvent{
          .event = SemanticFragmentDropEvent{.drop = state.drop, .change = SemanticFragmentDropChange::Modified},
          .first_seen = now,
      };
    }
  }

  for (const auto& [key, state] : known_) {
    if (current.contains(key)) {
      continue;
    }
    pending_[key] = PendingEvent{
        .event = SemanticFragmentDropEvent{.drop = state.drop, .change = SemanticFragmentDropChange::Deleted},
        .first_seen = now,
    };
  }

  known_ = current;

  std::vector<SemanticFragmentDropEvent> ready;
  for (auto iterator = pending_.begin(); iterator != pending_.end();) {
    if (now - iterator->second.first_seen >= options_.debounce) {
      ready.push_back(std::move(iterator->second.event));
      iterator = pending_.erase(iterator);
    } else {
      ++iterator;
    }
  }

  std::ranges::sort(ready, [](const SemanticFragmentDropEvent& lhs, const SemanticFragmentDropEvent& rhs) {
    return lhs.drop.chunk_index < rhs.drop.chunk_index;
  });
  return ready;
}

std::vector<SemanticFragmentDrop> discover_semantic_fragment_drops(const std::filesystem::path& drop_dir) {
  std::vector<SemanticFragmentDrop> drops;
  const auto scanned = scan_drops(drop_dir);
  drops.reserve(scanned.size());
  for (const auto& [_, state] : scanned) {
    drops.push_back(state.drop);
  }
  std::ranges::sort(drops, [](const SemanticFragmentDrop& lhs, const SemanticFragmentDrop& rhs) {
    if (lhs.chunk_index != rhs.chunk_index) {
      return lhs.chunk_index < rhs.chunk_index;
    }
    return lhs.path.generic_string() < rhs.path.generic_string();
  });
  return drops;
}

}  // namespace cgraph
