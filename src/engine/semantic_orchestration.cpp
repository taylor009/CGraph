#include "cgraph/semantic_orchestration.hpp"

#include "cgraph/daemon_ops.hpp"
#include "cgraph/pipeline.hpp"
#include "cgraph/semantic_cache.hpp"
#include "cgraph/semantic_drop.hpp"
#include "cgraph/semantic_ingest.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace cgraph {
namespace {

namespace fs = std::filesystem;

constexpr const char* kManifestName = "plan.json";
constexpr const char* kCacheName = "semantic-cache.json";
constexpr const char* kStatIndexName = "semantic-stat-index.json";

[[nodiscard]] const char* kind_name(SemanticInputKind kind) {
  return kind == SemanticInputKind::Media ? "media" : "document";
}

}  // namespace

std::filesystem::path default_semantic_drop_dir(const std::filesystem::path& output_dir) {
  return output_dir / "semantic-drop";
}

std::string fragment_filename_for_chunk(std::size_t chunk_index) {
  std::ostringstream name;
  name << "chunk_" << std::setw(2) << std::setfill('0') << chunk_index << ".json";
  return name.str();
}

std::unordered_map<std::size_t, std::vector<fs::path>> load_chunk_sources(const fs::path& drop_dir) {
  std::unordered_map<std::size_t, std::vector<fs::path>> sources_by_chunk;
  std::ifstream manifest_file(drop_dir / kManifestName, std::ios::binary);
  if (!manifest_file) {
    return sources_by_chunk;
  }
  const auto manifest = nlohmann::json::parse(manifest_file, nullptr, false);
  if (manifest.is_discarded()) {
    return sources_by_chunk;
  }
  for (const auto& chunk : manifest.value("chunks", nlohmann::json::array())) {
    const auto index = chunk.value("index", std::size_t{0});
    for (const auto& input : chunk.value("inputs", nlohmann::json::array())) {
      if (const auto path = input.value("path", std::string{}); !path.empty()) {
        sources_by_chunk[index].emplace_back(path);
      }
    }
  }
  return sources_by_chunk;
}

EnrichmentPlanResult plan_enrichment(const std::filesystem::path& root, const std::filesystem::path& drop_dir) {
  EnrichmentPlanResult result;
  result.drop_dir = drop_dir;
  std::error_code ec;
  fs::create_directories(drop_dir, ec);

  const auto cache = read_semantic_cache(drop_dir / kCacheName);
  // Never scan the drop directory or the export output it lives under, so the
  // tool does not try to enrich its own exports or re-ingest dropped fragments.
  SemanticChunkPlanOptions options;
  options.excluded_dirs = {drop_dir};
  if (drop_dir.has_parent_path()) {
    options.excluded_dirs.push_back(drop_dir.parent_path());
  }
  // Stat cache persisted in the drop dir, so repeated one-shot plans reuse hashes
  // for unchanged docs/media instead of re-reading every file each run.
  auto stat_index = read_semantic_stat_index(drop_dir / kStatIndexName);
  result.plan = plan_semantic_chunks(root, cache, options, &stat_index);
  write_semantic_stat_index(stat_index, drop_dir / kStatIndexName);

  // Fragment filenames are derived from a chunk index. The plan re-numbers from
  // zero every pass (only uncached chunks appear), so naming files by the
  // plan-relative index would reuse chunk_00.json across passes and overwrite an
  // earlier pass's fragment — silently dropping its nodes from the graph on the
  // next rebuild, even though the cache still marks the sources enriched. Offset
  // new fragment names past the fragments already dropped so the drop directory
  // accumulates across passes and ingest stays additive.
  const auto fragment_offset = discover_semantic_fragment_drops(drop_dir).size();

  // Manifest: enough for a host to know what to enrich and where to drop each
  // resulting fragment.
  nlohmann::json manifest;
  manifest["cache_hits"] = result.plan.cache_hits;
  manifest["stale_inputs"] = result.plan.stale_inputs;
  manifest["chunks"] = nlohmann::json::array();
  for (const auto& chunk : result.plan.chunks) {
    const auto fragment_index = fragment_offset + chunk.index;
    nlohmann::json inputs = nlohmann::json::array();
    for (const auto& input : chunk.inputs) {
      inputs.push_back({
          {"path", input.path.generic_string()},
          {"kind", kind_name(input.kind)},
          {"content_hash", input.content_hash},
          {"size", input.size},
      });
      ++result.inputs_to_enrich;
    }
    manifest["chunks"].push_back({
        {"index", fragment_index},
        {"fragment", fragment_filename_for_chunk(fragment_index)},
        {"inputs", std::move(inputs)},
    });
  }

  result.manifest_path = drop_dir / kManifestName;
  std::ofstream(result.manifest_path, std::ios::binary) << manifest.dump(2) << '\n';
  return result;
}

EnrichmentIngestResult ingest_enrichment(const std::filesystem::path& root, const std::filesystem::path& drop_dir) {
  EnrichmentIngestResult result;

  // Deterministic base graph published into a daemon state so fragments merge
  // through the same single-writer path the live daemon would use.
  auto pipeline = run_one_shot(root);
  result.deterministic_nodes = pipeline.graph.nodes.size();
  DaemonState state;
  publish_graph_snapshot(state, std::move(pipeline.graph));

  auto cache = read_semantic_cache(drop_dir / kCacheName);

  // Map chunk index -> its source input paths, recovered from the manifest, so
  // each merged fragment is cached against the document(s) it enriches.
  const auto sources_by_chunk = load_chunk_sources(drop_dir);

  for (const auto& drop : discover_semantic_fragment_drops(drop_dir)) {
    const auto sources = sources_by_chunk.find(drop.chunk_index);
    // Attribute the fragment to its first manifest source; fall back to the
    // fragment path itself when no manifest entry exists.
    const fs::path source = (sources != sources_by_chunk.end() && !sources->second.empty())
                                ? sources->second.front()
                                : drop.path;

    auto ingest = ingest_semantic_fragment(state, cache, source, drop.path);
    if (ingest.merged) {
      ++result.fragments_ingested;
      // Cache any additional documents this chunk covered against the same fragment.
      if (sources != sources_by_chunk.end()) {
        for (std::size_t i = 1; i < sources->second.size(); ++i) {
          cache.upsert(make_semantic_cache_record(sources->second[i], drop.path, SemanticCacheState::Valid));
        }
      }
    } else {
      ++result.fragments_rejected;
      for (auto& error : ingest.errors) {
        result.errors.push_back(drop.path.filename().generic_string() + ": " + std::move(error));
      }
    }
  }

  write_semantic_cache(cache, drop_dir / kCacheName);
  result.graph = *read_graph_snapshot(state);
  return result;
}

}  // namespace cgraph
