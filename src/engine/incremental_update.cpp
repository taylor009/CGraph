#include "cgraph/incremental_update.hpp"

#include "cgraph/analysis.hpp"
#include "cgraph/detect.hpp"
#include "cgraph/dedup.hpp"
#include "cgraph/file_cache.hpp"
#include "cgraph/file_extraction.hpp"
#include "cgraph/graph_builder.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cgraph {
namespace {

[[nodiscard]] std::string key_for(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] GraphSnapshot rebuild_graph(const IncrementalGraphIndex& index) {
  std::vector<Fragment> fragments;
  std::vector<RawCall> raw_calls;
  std::vector<RawRelation> raw_relations;
  fragments.reserve(index.files.size());

  std::vector<std::string> keys;
  keys.reserve(index.files.size());
  for (const auto& [key, _] : index.files) {
    keys.push_back(key);
  }
  std::ranges::sort(keys);

  for (const auto& key : keys) {
    const auto& result = index.files.at(key);
    fragments.push_back(result.fragment);
    raw_calls.insert(raw_calls.end(), result.raw_calls.begin(), result.raw_calls.end());
    raw_relations.insert(raw_relations.end(), result.raw_relations.begin(), result.raw_relations.end());
  }

  auto graph = merge_fragments(fragments);
  resolve_imports(graph, index.aliases);
  resolve_raw_calls(graph, raw_calls);
  resolve_raw_relations(graph, raw_relations);
  return graph;
}

// Community + centrality analysis, run AFTER dedup — exactly the order
// run_one_shot uses (merge -> resolve -> dedup -> communities -> analyze). Doing
// it here rather than inside rebuild_graph matters for two reasons:
//   * correctness/parity: dedup must see no community property, or it buckets
//     every node of a community together and runs O(k^2) fuzzy comparisons
//     within it (a ~60s cliff on a large repo); running before communities keeps
//     the daemon graph identical to the canonical one-shot graph.
//   * centrality is computed on the final, deduped node set, not a stale one.
void finalize_graph(GraphSnapshot& graph) {
  (void)detect_communities(graph);
  analyze_graph(graph);
}

}  // namespace

IncrementalUpdateResult full_stat_index_rescan(
    DaemonState& state,
    IncrementalGraphIndex& index,
    const std::filesystem::path& root,
    const IncrementalDedupPolicy& dedup_policy) {
  IncrementalUpdateResult result;
  result.full_rescan = true;

  auto detected_files = detect_project_files(root);
  std::unordered_map<std::string, ExtractionResult> rescanned;
  std::unordered_map<std::string, FileCacheEntry> rescanned_cache;
  rescanned.reserve(detected_files.size());
  rescanned_cache.reserve(detected_files.size());

  // Classify every detected file against the existing index: a stat/hash hit
  // whose extraction we already hold is reused as-is; only changed and new files
  // are re-extracted. With an empty index (cold start) everything is "new" and we
  // extract all; with a warm or disk-loaded index we re-extract only the delta.
  // A reused fragment is byte-identical to re-extracting it, so the merged graph
  // is unchanged either way.
  std::vector<DetectedFile> to_extract;
  for (const auto& file : detected_files) {
    const auto key = key_for(file.path);
    std::optional<FileCacheEntry> previous;
    if (const auto cached = index.cache.find(key); cached != index.cache.end()) {
      previous = cached->second;
    }
    const auto classification = classify_cached_file(file.path, std::move(previous));
    if (classification.state == CacheState::Deleted || !classification.current.has_value()) {
      continue;  // vanished between detect and classify; treat as removed
    }
    const bool reusable = (classification.state == CacheState::StatHit || classification.state == CacheState::HashHit);
    if (reusable) {
      if (const auto existing = index.files.find(key); existing != index.files.end()) {
        rescanned.emplace(key, std::move(existing->second));
        rescanned_cache.emplace(key, *classification.current);
        ++result.files_cache_hit;
        continue;
      }
    }
    rescanned_cache.emplace(key, *classification.current);
    to_extract.push_back(file);
  }

  // Re-extract only the changed/new files, concurrently. Time the extraction so
  // the modeled cache saving (files_reused x mean per-file extract time) can be
  // derived from real numbers in status.
  double extract_ms = 0.0;
  {
    ScopedTimer timer(&extract_ms);
    auto extractions = extract_files(to_extract);
    for (std::size_t i = 0; i < to_extract.size(); ++i) {
      auto& extraction = extractions[i];
      result.warnings.insert(result.warnings.end(), extraction.fragment.warnings.begin(), extraction.fragment.warnings.end());
      rescanned.emplace(key_for(to_extract[i].path), std::move(extraction));
      ++result.files_reextracted;
    }
  }

  for (const auto& [key, _] : index.files) {
    if (!rescanned.contains(key)) {
      ++result.files_removed;
    }
  }

  index.files = std::move(rescanned);
  index.cache = std::move(rescanned_cache);
  index.updates_since_full_dedup = 0;
  index.aliases = load_path_aliases(root);

  const std::size_t files_total = result.files_reextracted + result.files_cache_hit;

  auto graph = rebuild_graph(index);
  semantic_dedup(graph, dedup_policy.options);
  finalize_graph(graph);  // communities + centrality on the deduped graph
  graph.cache_hit_rate = cache_hit_rate(result.files_cache_hit, files_total);
  result.full_dedup_reconciled = true;

  // Record this build's Layer A inputs for the modeled cache-saving estimate.
  // Mean is left 0 (estimate suppressed) when nothing was extracted this build.
  state.last_files_cache_hit = result.files_cache_hit;
  state.last_extract_mean_ms =
      result.files_reextracted == 0 ? 0.0 : extract_ms / static_cast<double>(result.files_reextracted);

  publish_graph_snapshot(state, std::move(graph));
  return result;
}

IncrementalUpdateResult apply_incremental_code_updates(
    DaemonState& state,
    IncrementalGraphIndex& index,
    std::span<const FileWatchEvent> events,
    const IncrementalDedupPolicy& dedup_policy) {
  IncrementalUpdateResult result;
  std::vector<std::string> changed_sources;

  for (const auto& event : events) {
    if (event.change == FileWatchChange::Overflow) {
      return full_stat_index_rescan(state, index, event.path, dedup_policy);
    }
    if (event.kind != WatchedFileKind::Code) {
      continue;
    }

    const auto key = key_for(event.path);
    if (event.change == FileWatchChange::Deleted) {
      index.cache.erase(key);
      if (index.files.erase(key) > 0) {
        ++result.files_removed;
        changed_sources.push_back(key);
      }
      continue;
    }

    const auto language = detect_language(event.path);
    if (language == DetectedLanguage::Unknown || !std::filesystem::exists(event.path)) {
      if (language == DetectedLanguage::Unknown && index.files.contains(key)) {
        changed_sources.push_back(key);
        continue;
      }
      if (!std::filesystem::exists(event.path) && index.files.contains(key)) {
        changed_sources.push_back(key);
        continue;
      }
      index.cache.erase(key);
      if (index.files.erase(key) > 0) {
        ++result.files_removed;
        changed_sources.push_back(key);
      }
      continue;
    }

    std::optional<FileCacheEntry> previous_cache;
    if (const auto cached = index.cache.find(key); cached != index.cache.end()) {
      previous_cache = cached->second;
    }
    auto cache_classification = classify_cached_file(event.path, std::move(previous_cache));
    if (cache_classification.current.has_value()) {
      index.cache[key] = *cache_classification.current;
    }
    if (index.files.contains(key) &&
        (cache_classification.state == CacheState::StatHit || cache_classification.state == CacheState::HashHit)) {
      continue;
    }

    auto extraction = extract_detected_file(DetectedFile{.path = event.path, .language = language});
    result.warnings.insert(result.warnings.end(), extraction.fragment.warnings.begin(), extraction.fragment.warnings.end());
    index.files[key] = std::move(extraction);
    ++result.files_reextracted;
    changed_sources.push_back(key);
  }

  auto graph = rebuild_graph(index);
  if (!changed_sources.empty()) {
    semantic_dedup_neighborhood(graph, changed_sources, dedup_policy.options);
    result.neighborhood_deduped = true;
    ++index.updates_since_full_dedup;
  }
  if (dedup_policy.full_reconcile_every > 0 && index.updates_since_full_dedup >= dedup_policy.full_reconcile_every) {
    semantic_dedup(graph, dedup_policy.options);
    result.full_dedup_reconciled = true;
    index.updates_since_full_dedup = 0;
  }
  finalize_graph(graph);  // communities + centrality after dedup (matches run_one_shot order)
  publish_graph_snapshot(state, std::move(graph));
  return result;
}

}  // namespace cgraph
