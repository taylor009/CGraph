#include "cgraph/semantic_chunk_plan.hpp"

#include "cgraph/file_cache.hpp"
#include "cgraph/file_watcher.hpp"
#include "cgraph/path_ignore.hpp"

#include <algorithm>

namespace cgraph {
namespace {

[[nodiscard]] bool is_excluded_dir(
    const std::filesystem::path& dir,
    const std::vector<std::filesystem::path>& excluded) {
  std::error_code ec;
  for (const auto& candidate : excluded) {
    if (std::filesystem::equivalent(dir, candidate, ec)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<std::filesystem::path> collect_semantic_paths(
    const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& excluded_dirs) {
  std::vector<std::filesystem::path> paths;
  const auto canonical_root = std::filesystem::weakly_canonical(root);
  const auto gitignore_patterns = read_root_gitignore(canonical_root);
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      canonical_root,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;

  for (; !error && iterator != end; iterator.increment(error)) {
    const auto& entry = *iterator;
    const auto name = entry.path().filename().generic_string();
    if (entry.is_directory(error)) {
      if (is_skipped_directory(name) || is_excluded_dir(entry.path(), excluded_dirs) ||
          matches_simple_gitignore(canonical_root, entry.path(), gitignore_patterns)) {
        iterator.disable_recursion_pending();
      }
      continue;
    }
    if (!entry.is_regular_file(error) || !is_watchable_file(entry.path())) {
      continue;
    }
    if (matches_simple_gitignore(canonical_root, entry.path(), gitignore_patterns)) {
      continue;
    }
    if (classify_watched_file(entry.path()) == WatchedFileKind::Code) {
      continue;
    }
    paths.push_back(entry.path());
  }

  std::ranges::sort(paths, [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return lhs.generic_string() < rhs.generic_string();
  });
  return paths;
}

[[nodiscard]] SemanticInputKind semantic_kind_for(const std::filesystem::path& path) {
  return classify_watched_file(path) == WatchedFileKind::Media ? SemanticInputKind::Media : SemanticInputKind::Document;
}

[[nodiscard]] bool is_valid_cached_record(const SemanticCacheRecord& record) {
  return record.state == SemanticCacheState::Valid && std::filesystem::exists(record.fragment_path);
}

[[nodiscard]] bool has_stale_record_for_source(
    const SemanticCache& cache,
    const std::filesystem::path& path,
    std::string_view current_hash) {
  for (const auto& record : cache.records()) {
    if (record.source_path == path && record.content_hash != current_hash) {
      return true;
    }
  }
  return false;
}

void flush_chunk(SemanticChunkPlan& plan, SemanticChunk& chunk, std::uintmax_t& chunk_bytes) {
  if (chunk.inputs.empty()) {
    return;
  }
  chunk.index = plan.chunks.size();
  plan.chunks.push_back(std::move(chunk));
  chunk = SemanticChunk{};
  chunk_bytes = 0;
}

}  // namespace

SemanticChunkPlan plan_semantic_chunks(
    const std::filesystem::path& root,
    const SemanticCache& cache,
    SemanticChunkPlanOptions options) {
  SemanticChunkPlan plan;
  SemanticChunk current_chunk;
  std::uintmax_t current_bytes = 0;

  for (const auto& path : collect_semantic_paths(root, options.excluded_dirs)) {
    const auto hash = sha256_file_hex(path);
    const auto cached = cache.find_by_content_hash(hash);
    if (cached.has_value() && is_valid_cached_record(*cached)) {
      ++plan.cache_hits;
      continue;
    }
    if (cached.has_value() || has_stale_record_for_source(cache, path, hash)) {
      ++plan.stale_inputs;
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    const auto input = SemanticInput{
        .path = path,
        .kind = semantic_kind_for(path),
        .content_hash = hash,
        .size = error ? 0 : size,
    };
    error.clear();

    const auto would_exceed_file_count =
        options.max_files_per_chunk > 0 && current_chunk.inputs.size() >= options.max_files_per_chunk;
    const auto would_exceed_bytes =
        options.max_bytes_per_chunk > 0 && !current_chunk.inputs.empty() &&
        current_bytes + input.size > options.max_bytes_per_chunk;
    if (would_exceed_file_count || would_exceed_bytes) {
      flush_chunk(plan, current_chunk, current_bytes);
    }

    current_bytes += input.size;
    current_chunk.inputs.push_back(input);
  }

  flush_chunk(plan, current_chunk, current_bytes);
  return plan;
}

}  // namespace cgraph
