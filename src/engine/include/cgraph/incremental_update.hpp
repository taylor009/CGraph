#pragma once

#include "cgraph/daemon_ops.hpp"
#include "cgraph/dedup.hpp"
#include "cgraph/extractor.hpp"
#include "cgraph/file_cache.hpp"
#include "cgraph/file_watcher.hpp"
#include "cgraph/tsconfig_aliases.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cgraph {

struct IncrementalGraphIndex {
  std::unordered_map<std::string, ExtractionResult> files;
  std::unordered_map<std::string, FileCacheEntry> cache;
  std::size_t updates_since_full_dedup = 0;
  // tsconfig path aliases for the project root, loaded once on full rescan and
  // reused by every incremental rebuild so `@/...` imports keep resolving.
  std::vector<PathAlias> aliases;
};

struct IncrementalDedupPolicy {
  std::size_t full_reconcile_every = 0;
  DedupOptions options;
};

struct IncrementalUpdateResult {
  std::size_t files_reextracted = 0;
  std::size_t files_cache_hit = 0;  // reused from the warm index (no re-parse)
  std::size_t files_removed = 0;
  bool neighborhood_deduped = false;
  bool full_dedup_reconciled = false;
  bool full_rescan = false;
  std::vector<std::string> warnings;
};

[[nodiscard]] IncrementalUpdateResult full_stat_index_rescan(
    DaemonState& state,
    IncrementalGraphIndex& index,
    const std::filesystem::path& root,
    const IncrementalDedupPolicy& dedup_policy = {});

[[nodiscard]] IncrementalUpdateResult apply_incremental_code_updates(
    DaemonState& state,
    IncrementalGraphIndex& index,
    std::span<const FileWatchEvent> events,
    const IncrementalDedupPolicy& dedup_policy = {});

}  // namespace cgraph
