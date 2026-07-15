#include "cgraph/semantic_ingest.hpp"

#include "cgraph/graph_builder.hpp"
#include "cgraph/semantic_fragment_validation.hpp"

#include <string>
#include <unordered_set>

namespace cgraph {
namespace {

// Referential-integrity guard for the SEMANTIC-INGEST path only. A schema-valid
// fragment can still carry an edge whose endpoint exists in neither the fragment
// nor the live graph; merge_fragment (the parity-locked deterministic merge)
// inserts edges unconditionally, so such a dangling edge would silently enter the
// graph. Reject the whole fragment atomically before any mutation so the host
// contract's "malformed -> rejected, graph unchanged" holds for referential
// breakage too. Returns the first offending endpoint id, or empty if every edge
// endpoint resolves. Uses node_key so the ids match exactly what merge assigns.
[[nodiscard]] std::string first_dangling_edge_endpoint(const Fragment& fragment, const GraphSnapshot& graph) {
  std::unordered_set<std::string> known;
  known.reserve(graph.nodes.size() + fragment.nodes.size());
  for (const auto& node : graph.nodes) {
    known.insert(node.id);
  }
  for (const auto& node : fragment.nodes) {
    known.insert(node_key(node));  // merge keys fragment nodes the same way
  }
  for (const auto& edge : fragment.edges) {
    if (!known.contains(edge.source)) {
      return edge.source;
    }
    if (!known.contains(edge.target)) {
      return edge.target;
    }
  }
  return {};
}

}  // namespace

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

  // Reject atomically (no mutation) if any edge endpoint resolves against neither
  // the fragment's own nodes nor the current graph snapshot. This wraps the
  // semantic path; it does not alter merge_fragment/merge_fragments used by the
  // deterministic pipeline (Graphify parity is a hard contract).
  const auto graph = read_graph_snapshot(state);
  if (const auto dangling = first_dangling_edge_endpoint(validation.fragment, *graph); !dangling.empty()) {
    result.errors.push_back("semantic fragment edge references unknown node: " + dangling);
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
