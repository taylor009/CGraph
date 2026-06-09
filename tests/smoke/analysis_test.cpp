#include "cgraph/analysis.hpp"

int main() {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "a", .label = "A"});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "B"});
  graph.nodes.push_back(cgraph::Node{.id = "c", .label = "C"});
  graph.nodes.push_back(cgraph::Node{.id = "d", .label = "D"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "LINKS"});
  graph.edges.push_back(cgraph::Edge{.source = "c", .target = "d", .relation = "LINKS"});

  const auto result = cgraph::detect_communities(graph);
  if (result.cluster_count < 1) {
    return 1;
  }
  for (const auto& node : graph.nodes) {
    if (!node.properties.contains("community")) {
      return 1;
    }
  }

  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "c", .relation = "CROSSES"});
  cgraph::analyze_graph(graph);
  bool saw_god_node = false;
  for (const auto& node : graph.nodes) {
    if (!node.properties.contains("degree_centrality")) {
      return 1;
    }
    if (node.properties.contains("god_node")) {
      saw_god_node = true;
    }
  }
  if (!saw_god_node) {
    return 1;
  }
  bool saw_cross_community = false;
  for (const auto& edge : graph.edges) {
    if (edge.properties.contains("cross_community")) {
      saw_cross_community = true;
    }
  }
  if (!saw_cross_community) {
    return 1;
  }

  cgraph::GraphSnapshot empty;
  const auto empty_result = cgraph::detect_communities(empty);
  if (empty_result.cluster_count != 0) {
    return 1;
  }

  return 0;
}
