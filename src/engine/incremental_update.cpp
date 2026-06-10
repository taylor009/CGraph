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
  // Run the same community + centrality analysis the one-shot pipeline does, so
  // the resident daemon's graph carries degree_centrality / god_node / community
  // — required for ranked query results and importance-aware blast radius.
  detect_communities(graph);
  analyze_graph(graph);
  return graph;
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

  // Extract all files concurrently; the index maps are keyed by file and
  // rebuild_graph re-sorts keys, so result order does not affect the graph.
  auto extractions = extract_files(detected_files);
  for (std::size_t i = 0; i < detected_files.size(); ++i) {
    auto& extraction = extractions[i];
    result.warnings.insert(result.warnings.end(), extraction.fragment.warnings.begin(), extraction.fragment.warnings.end());
    const auto key = key_for(detected_files[i].path);
    rescanned_cache.emplace(key, read_file_cache_entry(detected_files[i].path));
    rescanned.emplace(key, std::move(extraction));
    ++result.files_reextracted;
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

  auto graph = rebuild_graph(index);
  semantic_dedup(graph, dedup_policy.options);
  result.full_dedup_reconciled = true;
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
  publish_graph_snapshot(state, std::move(graph));
  return result;
}

}  // namespace cgraph
