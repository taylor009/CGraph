#include "cgraph/semantic_ingest.hpp"

#include "cgraph/graph_builder.hpp"
#include "cgraph/semantic_fragment_validation.hpp"

namespace cgraph {

SemanticIngestResult ingest_semantic_fragment(
    DaemonState& state,
    SemanticCache& cache,
    const std::filesystem::path& source_path,
    const std::filesystem::path& fragment_path) {
  SemanticIngestResult result;
  auto validation = validate_semantic_fragment_file(fragment_path);
  if (!validation.valid) {
    result.errors = std::move(validation.errors);
    return result;
  }

  mutate_graph_snapshot(state, [&](GraphSnapshot& graph) {
    merge_fragment(graph, validation.fragment);
  });
  result.merged = true;

  cache.upsert(make_semantic_cache_record(source_path, fragment_path, SemanticCacheState::Valid));
  result.cache_updated = true;
  return result;
}

}  // namespace cgraph
