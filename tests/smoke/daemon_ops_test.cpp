#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

int main() {
  cgraph::DaemonState state;
  state.pid = 123;
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "a", .label = "Alpha", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});
  cgraph::publish_graph_snapshot(state, std::move(graph));

  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (!status["ok"].get<bool>() || status["result"]["node_count"] != 2) {
    return 1;
  }

  const auto query = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "Alpha"}}));
  if (query["result"]["nodes"].empty()) {
    return 1;
  }

  const auto path = cgraph::handle_daemon_request(state, cgraph::make_request("path", {{"source", "a"}, {"target", "b"}}));
  if (path["result"]["path"].size() != 2) {
    return 1;
  }

  const auto explain = cgraph::handle_daemon_request(state, cgraph::make_request("explain", {{"id", "a"}}));
  if (explain["result"]["neighbors"].empty()) {
    return 1;
  }

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
