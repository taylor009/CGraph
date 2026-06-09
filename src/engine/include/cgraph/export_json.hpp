#pragma once

#include "cgraph/types.hpp"

#include <nlohmann/json.hpp>

namespace cgraph {

[[nodiscard]] nlohmann::json to_node_link_json(const GraphSnapshot& graph);
[[nodiscard]] std::string export_graph_html(const GraphSnapshot& graph);
[[nodiscard]] std::string export_graph_svg(const GraphSnapshot& graph);
[[nodiscard]] std::string export_obsidian_markdown(const GraphSnapshot& graph);
[[nodiscard]] std::string export_neo4j_cypher(const GraphSnapshot& graph);
[[nodiscard]] std::string export_call_flow_html(const GraphSnapshot& graph);

}  // namespace cgraph
