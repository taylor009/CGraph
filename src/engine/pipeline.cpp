#include "cgraph/pipeline.hpp"

#include "cgraph/analysis.hpp"
#include "cgraph/dedup.hpp"
#include "cgraph/detect.hpp"
#include "cgraph/export_json.hpp"
#include "cgraph/file_extraction.hpp"
#include "cgraph/graph_builder.hpp"

#include <fstream>
#include <vector>

namespace cgraph {
namespace {

void write_text(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

}  // namespace

PipelineResult run_one_shot(const std::filesystem::path& root) {
  PipelineResult result;
  const auto files = detect_project_files(root);
  result.file_count = files.size();

  std::vector<Fragment> fragments;
  std::vector<RawCall> raw_calls;
  std::vector<RawRelation> raw_relations;
  fragments.reserve(files.size());

  // Extract concurrently; the results come back in detection order, so merging
  // them below is identical to the serial path (parity preserved).
  auto extractions = extract_files(files);
  for (auto& extraction : extractions) {
    result.warnings.insert(result.warnings.end(), extraction.fragment.warnings.begin(), extraction.fragment.warnings.end());
    raw_calls.insert(raw_calls.end(), extraction.raw_calls.begin(), extraction.raw_calls.end());
    raw_relations.insert(raw_relations.end(), extraction.raw_relations.begin(), extraction.raw_relations.end());
    fragments.push_back(std::move(extraction.fragment));
  }

  result.graph = merge_fragments(fragments);
  const auto aliases = load_path_aliases(root);
  resolve_imports(result.graph, aliases);
  resolve_raw_calls(result.graph, raw_calls);
  resolve_raw_relations(result.graph, raw_relations);
  semantic_dedup(result.graph);
  const auto community_result = detect_communities(result.graph);
  if (community_result.cluster_count == 0 && !result.graph.nodes.empty()) {
    result.warnings.push_back("community detection produced no clusters");
  }
  analyze_graph(result.graph);
  return result;
}

void write_exports(const GraphSnapshot& graph, const std::filesystem::path& output_dir) {
  std::filesystem::create_directories(output_dir);
  write_text(output_dir / "graph.json", to_node_link_json(graph).dump(2));
  write_text(output_dir / "graph.html", export_graph_html(graph));
  write_text(output_dir / "graph.svg", export_graph_svg(graph));
  write_text(output_dir / "obsidian.md", export_obsidian_markdown(graph));
  write_text(output_dir / "cypher.txt", export_neo4j_cypher(graph));
  write_text(output_dir / "call-flow.html", export_call_flow_html(graph));
}

}  // namespace cgraph
