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
  graph.nodes.push_back(cgraph::Node{
      .id = "a",
      .label = "Alpha",
      .source_file = src.generic_string(),
      .source_location = cgraph::SourceLocation{.start_line = 2, .start_column = 0, .end_line = 4, .end_column = 1},
      .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});
  cgraph::publish_graph_snapshot(state, std::move(graph));

  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (!status["ok"].get<bool>() || status["result"]["node_count"] != 2) {
    return 1;
  }

  const auto query = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "Alpha"}}));
  const auto& query_nodes = query["result"]["nodes"];
  if (query_nodes.empty()) {
    return 1;
  }
  // Query results must carry file:line so the agent can open the symbol directly.
  const auto& hit = query_nodes[0];
  if (hit.value("source_file", std::string{}) != src.generic_string() || hit.value("line", 0U) != 2U) {
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
