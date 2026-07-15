#include "cgraph/semantic_ingest.hpp"

#include "cgraph/daemon_ops.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/semantic_chunk_plan.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

bool has_node_label(const cgraph::GraphSnapshot& graph, const std::string& label) {
  for (const auto& node : graph.nodes) {
    if (node.label == label) {
      return true;
    }
  }
  return false;
}

bool has_edge(const cgraph::GraphSnapshot& graph, const std::string& source, const std::string& relation, const std::string& target) {
  for (const auto& edge : graph.edges) {
    if (edge.source == source && edge.relation == relation && edge.target == target) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  // weakly_canonical: the planner canonicalizes root internally, so planned
  // input paths are canonical; build the comparison root the same way.
  const auto root =
      std::filesystem::weakly_canonical(std::filesystem::temp_directory_path() / "cgraph-semantic-ingest-test");
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto source = root / "docs" / "guide.md";
  const auto valid_fragment = root / "graphify-out" / "semantic-drop" / "chunk_01.json";
  const auto invalid_fragment = root / "graphify-out" / "semantic-drop" / "chunk_02.json";
  write_file(source, "# Guide\nMentions service\n");
  write_file(
      valid_fragment,
      R"({
        "nodes": [
          {"id": "doc:guide", "label": "Guide", "type": "document", "source_file": "docs/guide.md"},
          {"id": "concept:service", "label": "Service", "type": "concept", "source_file": "docs/guide.md"}
        ],
        "edges": [
          {"source": "doc:guide", "target": "concept:service", "relation": "MENTIONS"}
        ],
        "hyperedges": []
      })");
  write_file(invalid_fragment, R"({"nodes":[{"id":"missing-label"}],"edges":[]})");

  cgraph::DaemonState state;
  cgraph::GraphSnapshot initial;
  initial.nodes.push_back(cgraph::Node{.id = "code:service", .label = "CodeService", .kind = "class"});
  initial.build_state = cgraph::BuildState::DeterministicReady;
  cgraph::publish_graph_snapshot(state, std::move(initial));

  cgraph::SemanticCache cache;
  auto result = cgraph::ingest_semantic_fragment(state, cache, source, valid_fragment);
  auto graph = cgraph::read_graph_snapshot(state);
  const auto cached = cache.find_for_file(source);
  if (!result.merged || !result.cache_updated || !result.errors.empty() || graph->nodes.size() != 3 ||
      !has_node_label(*graph, "CodeService") || !has_node_label(*graph, "Guide") ||
      !has_node_label(*graph, "Service") || !has_edge(*graph, "doc:guide", "MENTIONS", "concept:service") ||
      !cached.has_value() || cached->fragment_path != valid_fragment || cached->state != cgraph::SemanticCacheState::Valid) {
    return 1;
  }

  result = cgraph::ingest_semantic_fragment(state, cache, source, invalid_fragment);
  graph = cgraph::read_graph_snapshot(state);
  const auto cached_after_invalid = cache.find_for_file(source);
  if (result.merged || result.cache_updated || result.errors.empty() || graph->nodes.size() != 3 ||
      !cached_after_invalid.has_value() || cached_after_invalid->fragment_path != valid_fragment) {
    return 1;
  }

  // Referential integrity on the semantic path: a schema-valid fragment whose
  // edge points at a node that exists in NEITHER the fragment nor the live graph
  // must be rejected atomically -- no merge, graph unchanged, counted like any
  // other validation failure. Without the check, merge_fragment would insert the
  // dangling edge unconditionally (it never validates endpoints). The fragment
  // below declares node "doc:orphan" and an edge to "concept:ghost" (undeclared,
  // absent from the graph): the edge is the referential break.
  const auto dangling_fragment = root / "graphify-out" / "semantic-drop" / "chunk_03.json";
  write_file(
      dangling_fragment,
      R"({
        "nodes": [
          {"id": "doc:orphan", "label": "Orphan", "type": "document", "source_file": "docs/guide.md"}
        ],
        "edges": [
          {"source": "doc:orphan", "target": "concept:ghost", "relation": "MENTIONS"}
        ],
        "hyperedges": []
      })");
  const auto nodes_before_dangling = graph->nodes.size();
  const auto edges_before_dangling = graph->edges.size();
  result = cgraph::ingest_semantic_fragment(state, cache, source, dangling_fragment);
  graph = cgraph::read_graph_snapshot(state);
  if (result.merged || result.cache_updated || result.errors.empty() ||
      graph->nodes.size() != nodes_before_dangling || graph->edges.size() != edges_before_dangling ||
      has_node_label(*graph, "Orphan")) {
    return 1;  // dangling-edge fragment must not mutate the graph at all
  }

  // Control: the SAME fragment shape is accepted once its edge target is a node
  // that already exists in the live graph. Here the edge points at "code:service"
  // (present since setup), so referential integrity holds and it merges. This
  // proves the check rejects only genuine dangling endpoints, not every cross-
  // fragment edge into the existing graph.
  const auto connected_fragment = root / "graphify-out" / "semantic-drop" / "chunk_04.json";
  write_file(
      connected_fragment,
      R"({
        "nodes": [
          {"id": "doc:connected", "label": "Connected", "type": "document", "source_file": "docs/guide.md"}
        ],
        "edges": [
          {"source": "doc:connected", "target": "code:service", "relation": "MENTIONS"}
        ],
        "hyperedges": []
      })");
  result = cgraph::ingest_semantic_fragment(state, cache, source, connected_fragment);
  graph = cgraph::read_graph_snapshot(state);
  if (!result.merged || !result.errors.empty() || !has_node_label(*graph, "Connected") ||
      !has_edge(*graph, "doc:connected", "MENTIONS", "code:service")) {
    return 1;  // an edge into an existing graph node must be accepted
  }

  const auto cache_hit_plan = cgraph::plan_semantic_chunks(root, cache);
  if (!cache_hit_plan.chunks.empty() || cache_hit_plan.cache_hits != 1) {
    return 1;
  }

  write_file(source, "# Guide\nChanged source\n");
  if (!cgraph::invalidate_semantic_cache_for_file(cache, source)) {
    return 1;
  }
  const auto records = cache.records();
  if (records.size() != 1 || records.front().state != cgraph::SemanticCacheState::Stale) {
    return 1;
  }
  const auto stale_plan = cgraph::plan_semantic_chunks(root, cache);
  if (stale_plan.chunks.size() != 1 || stale_plan.stale_inputs != 1 ||
      stale_plan.chunks.front().inputs.front().path != source) {
    return 1;
  }

  state.enrichment_state = cgraph::EnrichmentState::Stale;
  state.enrichment_stale = stale_plan.stale_inputs;
  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (status["result"]["enrichment_state"] != "stale" || status["result"]["enrichment_stale"] != 1) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
