#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

#include <filesystem>
#include <fstream>

int main() {
  namespace fs = std::filesystem;
  // A real source file so the explain snippet exercises actual file reads, not a
  // stub. Node "a" points at line 2 (the class), so the snippet must start there.
  const auto src = fs::temp_directory_path() / "cgraph-daemon-ops-test" / "alpha.cpp";
  fs::remove_all(src.parent_path());
  fs::create_directories(src.parent_path());
  std::ofstream(src, std::ios::binary)
      << "// header line\n"
      << "class Alpha {\n"
      << "  void run();\n"
      << "};\n";

  cgraph::DaemonState state;
  state.pid = 123;
  cgraph::GraphSnapshot graph;
  // "a" is the central hub (centrality 1.0, a god node); "AlphaLeaf" also matches
  // the "Alpha" query but is peripheral. Edges c -> a -> b form a chain so the
  // blast radius has two depths to walk.
  graph.nodes.push_back(cgraph::Node{
      .id = "a",
      .label = "Alpha",
      .source_file = src.generic_string(),
      .source_location = cgraph::SourceLocation{.start_line = 2, .start_column = 0, .end_line = 4, .end_column = 1},
      .kind = "class",
      .properties = {{"degree_centrality", "1.000000"}, {"god_node", "true"}}});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .kind = "function"});
  graph.nodes.push_back(cgraph::Node{
      .id = "c", .label = "AlphaLeaf", .kind = "function", .properties = {{"degree_centrality", "0.100000"}}});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});
  graph.edges.push_back(cgraph::Edge{.source = "c", .target = "a", .relation = "CALLS"});
  cgraph::publish_graph_snapshot(state, std::move(graph));

  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (!status["ok"].get<bool>() || status["result"]["node_count"] != 3) {
    return 1;
  }

  const auto query = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "Alpha"}}));
  const auto& query_result = query["result"];
  const auto& query_nodes = query_result["nodes"];
  // Both "Alpha" and "AlphaLeaf" match; total reflects the full match count.
  if (query_nodes.size() != 2 || query_result.value("total", 0U) != 2U) {
    return 1;
  }
  // Ranked by centrality: the hub "a" must lead, carrying file:line, its
  // centrality, and the god_node flag so the agent can open and prioritize it.
  const auto& hit = query_nodes[0];
  if (hit.value("source_file", std::string{}) != src.generic_string() || hit.value("line", 0U) != 2U ||
      hit.value("centrality", 0.0) < 0.99 || !hit.value("god_node", false)) {
    return 1;
  }
  if (query_nodes[1].value("label", std::string{}) != "AlphaLeaf") {
    return 1;  // peripheral node ranked below the hub
  }

  // Blast radius: what breaks if "b" changes? Its dependents are "a" (depth 1,
  // calls b) then "c" (depth 2, calls a). Ordered by depth.
  const auto impact = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "b"}, {"direction", "dependents"}, {"max_depth", 3}}));
  const auto& impact_nodes = impact["result"]["nodes"];
  if (impact["result"].value("total", 0U) != 2U || impact_nodes.size() != 2) {
    return 1;
  }
  if (impact_nodes[0].value("id", std::string{}) != "a" || impact_nodes[0].value("depth", 0) != 1 ||
      impact_nodes[1].value("id", std::string{}) != "c" || impact_nodes[1].value("depth", 0) != 2) {
    return 1;
  }
  // Depth bound is honored: max_depth 1 reaches only the direct dependent.
  const auto shallow = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "b"}, {"direction", "dependents"}, {"max_depth", 1}}));
  if (shallow["result"]["nodes"].size() != 1 || shallow["result"]["nodes"][0].value("id", std::string{}) != "a") {
    return 1;
  }
  // Opposite direction: what does "c" depend on? a (depth 1) then b (depth 2).
  const auto deps = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "c"}, {"direction", "dependencies"}, {"max_depth", 3}}));
  const auto& deps_nodes = deps["result"]["nodes"];
  if (deps_nodes.size() != 2 || deps_nodes[0].value("id", std::string{}) != "a" ||
      deps_nodes[1].value("id", std::string{}) != "b") {
    return 1;
  }

  const auto path = cgraph::handle_daemon_request(state, cgraph::make_request("path", {{"source", "a"}, {"target", "b"}}));
  if (path["result"]["path"].size() != 2) {
    return 1;
  }
  // path_nodes enriches each hop; the first hop is "a" with its label + line.
  const auto& path_nodes = path["result"]["path_nodes"];
  if (path_nodes.size() != 2 || path_nodes[0].value("label", std::string{}) != "Alpha" ||
      path_nodes[0].value("line", 0U) != 2U) {
    return 1;
  }

  const auto explain = cgraph::handle_daemon_request(state, cgraph::make_request("explain", {{"id", "a"}}));
  const auto& explain_result = explain["result"];
  if (explain_result["neighbors"].empty()) {
    return 1;
  }
  // The focal node carries a location block and a snippet read from disk; the
  // snippet must begin at the class declaration (start_line 2), not the header.
  if (explain_result.value("line", 0U) != 2U || !explain_result.contains("location") ||
      explain_result["location"].value("start_line", 0U) != 2U) {
    return 1;
  }
  const auto snippet = explain_result.value("snippet", std::string{});
  if (snippet.rfind("class Alpha {", 0) != 0 || snippet.find("header line") != std::string::npos) {
    return 1;
  }
  // The neighbor entry must describe the node on the other end (callee "Beta")
  // with a direction, so the agent can navigate without a second lookup.
  const auto& neighbor = explain_result["neighbors"][0];
  if (neighbor.value("direction", std::string{}) != "out" ||
      neighbor["node"].value("label", std::string{}) != "Beta") {
    return 1;
  }
  fs::remove_all(src.parent_path());

  const auto update = cgraph::handle_daemon_request(state, cgraph::make_request("update"));
  if (!update["result"]["accepted"].get<bool>()) {
    return 1;
  }

  const auto shutdown = cgraph::handle_daemon_request(state, cgraph::make_request("shutdown"));
  if (!shutdown["result"]["shutdown"].get<bool>() || !state.shutdown_requested) {
    return 1;
  }

  auto bad = cgraph::make_request("status");
  bad["protocol_version"] = 999;
  if (cgraph::handle_daemon_request(state, bad)["ok"].get<bool>()) {
    return 1;
  }

  return 0;
}
