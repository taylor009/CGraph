#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

#include <algorithm>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace cgraph {
namespace {

[[nodiscard]] nlohmann::json error_response(std::string message) {
  return nlohmann::json{{"ok", false}, {"error", std::move(message)}};
}

[[nodiscard]] nlohmann::json ok_response(nlohmann::json result = nlohmann::json::object()) {
  return nlohmann::json{{"ok", true}, {"result", std::move(result)}};
}

[[nodiscard]] std::vector<const Node*> matching_nodes(const GraphSnapshot& graph, const std::string& needle) {
  std::vector<const Node*> matches;
  for (const auto& node : graph.nodes) {
    if (node.id.find(needle) != std::string::npos || node.label.find(needle) != std::string::npos) {
      matches.push_back(&node);
    }
  }
  return matches;
}

[[nodiscard]] nlohmann::json query_graph(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto needle = params.value("q", params.value("query", std::string{}));
  auto nodes = nlohmann::json::array();
  for (const auto* node : matching_nodes(graph, needle)) {
    nodes.push_back({{"id", node->id}, {"label", node->label}, {"kind", node->kind}});
  }
  return {{"nodes", std::move(nodes)}};
}

[[nodiscard]] nlohmann::json explain_node(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto id = params.value("id", std::string{});
  for (const auto& node : graph.nodes) {
    if (node.id == id || node.label == id) {
      auto neighbors = nlohmann::json::array();
      for (const auto& edge : graph.edges) {
        if (edge.source == node.id || edge.target == node.id) {
          neighbors.push_back({{"source", edge.source}, {"target", edge.target}, {"relation", edge.relation}});
        }
      }
      return {{"id", node.id}, {"label", node.label}, {"kind", node.kind}, {"neighbors", std::move(neighbors)}};
    }
  }
  return {{"id", id}, {"neighbors", nlohmann::json::array()}};
}

[[nodiscard]] nlohmann::json shortest_path(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto source = params.value("source", std::string{});
  const auto target = params.value("target", std::string{});
  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  for (const auto& edge : graph.edges) {
    adjacency[edge.source].push_back(edge.target);
    adjacency[edge.target].push_back(edge.source);
  }

  std::queue<std::string> queue;
  std::unordered_map<std::string, std::string> previous;
  queue.push(source);
  previous[source] = {};

  while (!queue.empty()) {
    const auto current = queue.front();
    queue.pop();
    if (current == target) {
      break;
    }
    for (const auto& next : adjacency[current]) {
      if (previous.contains(next)) {
        continue;
      }
      previous[next] = current;
      queue.push(next);
    }
  }

  auto path = nlohmann::json::array();
  if (!previous.contains(target)) {
    return {{"path", path}};
  }

  std::vector<std::string> reversed;
  for (std::string cursor = target; !cursor.empty(); cursor = previous[cursor]) {
    reversed.push_back(cursor);
  }
  std::ranges::reverse(reversed);
  for (const auto& item : reversed) {
    path.push_back(item);
  }
  return {{"path", std::move(path)}};
}

[[nodiscard]] nlohmann::json status(const DaemonState& state, const GraphSnapshot& graph) {
  const auto enrichment_state = [](EnrichmentState value) {
    switch (value) {
      case EnrichmentState::Idle:
        return "idle";
      case EnrichmentState::Pending:
        return "pending";
      case EnrichmentState::Running:
        return "running";
      case EnrichmentState::Stale:
        return "stale";
      case EnrichmentState::Failed:
        return "failed";
    }
    return "failed";
  };

  return {
      {"pid", state.pid},
      {"uptime_seconds", state.uptime_seconds},
      {"node_count", graph.nodes.size()},
      {"edge_count", graph.edges.size()},
      {"build_state", static_cast<int>(graph.build_state)},
      {"cache_hit_rate", graph.cache_hit_rate},
      {"enrichment_state", enrichment_state(state.enrichment_state)},
      {"enrichment_pending", state.enrichment_pending},
      {"enrichment_running", state.enrichment_running},
      {"enrichment_stale", state.enrichment_stale},
      {"enrichment_failed", state.enrichment_failed},
  };
}

}  // namespace

std::shared_ptr<const GraphSnapshot> read_graph_snapshot(const DaemonState& state) {
  std::scoped_lock guard(state.snapshot_mutex);
  return state.graph_snapshot;
}

void publish_graph_snapshot(DaemonState& state, GraphSnapshot graph) {
  auto snapshot = std::make_shared<const GraphSnapshot>(std::move(graph));
  std::scoped_lock guard(state.snapshot_mutex);
  state.graph_snapshot = std::move(snapshot);
}

void mutate_graph_snapshot(DaemonState& state, const std::function<void(GraphSnapshot&)>& mutator) {
  std::scoped_lock writer_guard(state.writer_mutex);
  auto graph = *read_graph_snapshot(state);
  mutator(graph);
  publish_graph_snapshot(state, std::move(graph));
}

nlohmann::json handle_daemon_request(DaemonState& state, const nlohmann::json& request) {
  if (!protocol_version_matches(request)) {
    return error_response("protocol version mismatch");
  }
  const auto op = request.value("op", std::string{});
  const auto params = request.value("params", nlohmann::json::object());
  const auto graph = read_graph_snapshot(state);

  if (op == "query") {
    return ok_response(query_graph(*graph, params));
  }
  if (op == "path") {
    return ok_response(shortest_path(*graph, params));
  }
  if (op == "explain") {
    return ok_response(explain_node(*graph, params));
  }
  if (op == "update") {
    return ok_response({{"accepted", true}});
  }
  if (op == "status") {
    return ok_response(status(state, *graph));
  }
  if (op == "shutdown") {
    state.shutdown_requested = true;
    return ok_response({{"shutdown", true}});
  }
  return error_response("unknown op: " + op);
}

}  // namespace cgraph
