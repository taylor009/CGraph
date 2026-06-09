#include "cgraph/export_json.hpp"

#include <nlohmann/json.hpp>

namespace {

bool accepts_node_link(const nlohmann::json& value) {
  if (!value.is_object() || !value.contains("nodes") || !value.contains("links")) {
    return false;
  }
  if (!value["nodes"].is_array() || !value["links"].is_array()) {
    return false;
  }
  for (const auto& node : value["nodes"]) {
    if (!node.contains("id") || !node["id"].is_string()) {
      return false;
    }
  }
  for (const auto& edge : value["links"]) {
    if (!edge.contains("source") || !edge.contains("target") || !edge.contains("relation")) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "service", .label = "Service", .source_file = "service.py", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "helper", .label = "helper", .source_file = "service.py", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "service", .target = "helper", .relation = "CALLS"});

  const auto serialized = cgraph::to_node_link_json(graph).dump();
  const auto parsed = nlohmann::json::parse(serialized);
  return accepts_node_link(parsed) ? 0 : 1;
}
