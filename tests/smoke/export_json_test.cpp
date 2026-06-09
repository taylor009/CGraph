#include "cgraph/export_json.hpp"

int main() {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "a", .label = "Alpha", .source_file = "a.cpp", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .source_file = "b.cpp", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});

  const auto json = cgraph::to_node_link_json(graph);
  if (json["directed"] != true || json["multigraph"] != false) {
    return 1;
  }
  if (!json["nodes"].is_array() || json["nodes"].size() != 2) {
    return 1;
  }
  if (!json["links"].is_array() || json["links"].size() != 1) {
    return 1;
  }
  if (json["links"][0]["source"] != "a" || json["links"][0]["target"] != "b") {
    return 1;
  }

  const auto html = cgraph::export_graph_html(graph);
  if (html.find("<html") == std::string::npos ||
      html.find("Alpha") == std::string::npos ||
      html.find("<canvas id=\"graph-canvas\"") == std::string::npos ||
      html.find("getContext(\"2d\")") == std::string::npos ||
      html.find("const graphData = ") == std::string::npos ||
      html.find("Search nodes") == std::string::npos ||
      html.find("node-details") == std::string::npos ||
      html.find("draggingNodeId") == std::string::npos ||
      html.find("communityFor(") == std::string::npos ||
      html.find("highlightIdsFor(") == std::string::npos ||
      html.find("<svg id=\"graph\"") != std::string::npos ||
      html.find("<ul>") != std::string::npos) {
    return 1;
  }

  const auto svg = cgraph::export_graph_svg(graph);
  if (svg.find("<svg") == std::string::npos || svg.find("<circle") == std::string::npos) {
    return 1;
  }

  const auto obsidian = cgraph::export_obsidian_markdown(graph);
  if (obsidian.find("# Alpha") == std::string::npos || obsidian.find("[[b]]") == std::string::npos) {
    return 1;
  }

  const auto cypher = cgraph::export_neo4j_cypher(graph);
  if (cypher.find("MERGE (n:Symbol") == std::string::npos || cypher.find("CALLS") == std::string::npos) {
    return 1;
  }

  const auto call_flow = cgraph::export_call_flow_html(graph);
  if (call_flow.find("a calls b") == std::string::npos) {
    return 1;
  }

  return 0;
}
