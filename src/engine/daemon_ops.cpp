#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace cgraph {
namespace {

// Bounds on the source slice returned with a node so a single explain stays
// token-cheap even for a large function body.
constexpr std::size_t kMaxSnippetLines = 40;
constexpr std::size_t kMaxSnippetChars = 2000;

[[nodiscard]] nlohmann::json error_response(std::string message) {
  return nlohmann::json{{"ok", false}, {"error", std::move(message)}};
}

[[nodiscard]] nlohmann::json ok_response(nlohmann::json result = nlohmann::json::object()) {
  return nlohmann::json{{"ok", true}, {"result", std::move(result)}};
}

// Compact, file-read-free descriptor: enough for an agent to open the symbol at
// the right place (source_file + 1-based line) without a follow-up lookup.
[[nodiscard]] nlohmann::json node_brief(const Node& node) {
  nlohmann::json brief{{"id", node.id}, {"label", node.label}, {"kind", node.kind}};
  if (!node.source_file.empty()) {
    brief["source_file"] = node.source_file;
  }
  if (node.source_location && node.source_location->start_line > 0) {
    brief["line"] = node.source_location->start_line;
  }
  return brief;
}

// Read the node's source lines [start_line, end_line] (1-based, inclusive),
// bounded by kMaxSnippetLines/kMaxSnippetChars. Returns the text and whether it
// was truncated; an empty text means the file or location was unavailable.
struct Snippet {
  std::string text;
  bool truncated = false;
};

[[nodiscard]] Snippet read_source_snippet(const Node& node) {
  Snippet result;
  if (node.source_file.empty() || !node.source_location || node.source_location->start_line == 0) {
    return result;
  }
  std::ifstream input(node.source_file, std::ios::binary);
  if (!input) {
    return result;
  }

  const auto start = node.source_location->start_line;
  const auto end = std::max(start, node.source_location->end_line);
  const auto last = std::min<std::uint32_t>(end, start + kMaxSnippetLines - 1);

  std::string line;
  std::uint32_t current = 0;
  while (std::getline(input, line)) {
    ++current;
    if (current < start) {
      continue;
    }
    if (current > last) {
      break;
    }
    if (result.text.size() + line.size() + 1 > kMaxSnippetChars) {
      result.truncated = true;
      break;
    }
    if (!result.text.empty()) {
      result.text.push_back('\n');
    }
    result.text += line;
  }
  if (end > last) {
    result.truncated = true;
  }
  return result;
}

[[nodiscard]] std::unordered_map<std::string, const Node*> index_nodes(const GraphSnapshot& graph) {
  std::unordered_map<std::string, const Node*> by_id;
  by_id.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    by_id.emplace(node.id, &node);
  }
  return by_id;
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
    nodes.push_back(node_brief(*node));
  }
  return {{"nodes", std::move(nodes)}};
}

[[nodiscard]] nlohmann::json explain_node(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto id = params.value("id", std::string{});
  const auto by_id = index_nodes(graph);
  for (const auto& node : graph.nodes) {
    if (node.id == id || node.label == id) {
      auto neighbors = nlohmann::json::array();
      for (const auto& edge : graph.edges) {
        const bool outgoing = edge.source == node.id;
        const bool incoming = edge.target == node.id;
        if (!outgoing && !incoming) {
          continue;
        }
        nlohmann::json entry{
            {"source", edge.source},
            {"target", edge.target},
            {"relation", edge.relation},
            {"direction", outgoing ? "out" : "in"}};
        // Attach the brief of the node on the other end so an agent can navigate
        // (open the caller/callee) without a second lookup.
        const auto& other_id = outgoing ? edge.target : edge.source;
        if (const auto it = by_id.find(other_id); it != by_id.end()) {
          entry["node"] = node_brief(*it->second);
        }
        neighbors.push_back(std::move(entry));
      }

      auto result = node_brief(node);
      if (node.source_location && node.source_location->start_line > 0) {
        result["location"] = {
            {"start_line", node.source_location->start_line},
            {"start_column", node.source_location->start_column},
            {"end_line", node.source_location->end_line},
            {"end_column", node.source_location->end_column}};
      }
      if (const auto snippet = read_source_snippet(node); !snippet.text.empty()) {
        result["snippet"] = snippet.text;
        if (snippet.truncated) {
          result["snippet_truncated"] = true;
        }
      }
      result["neighbors"] = std::move(neighbors);
      return result;
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
    return {{"path", path}, {"path_nodes", nlohmann::json::array()}};
  }

  std::vector<std::string> reversed;
  for (std::string cursor = target; !cursor.empty(); cursor = previous[cursor]) {
    reversed.push_back(cursor);
  }
  std::ranges::reverse(reversed);

  // `path` stays a bare id list for existing consumers; `path_nodes` carries the
  // label/kind/source_file/line for each hop so an agent can read the route.
  const auto by_id = index_nodes(graph);
  auto path_nodes = nlohmann::json::array();
  for (const auto& item : reversed) {
    path.push_back(item);
    if (const auto it = by_id.find(item); it != by_id.end()) {
      path_nodes.push_back(node_brief(*it->second));
    } else {
      path_nodes.push_back({{"id", item}});
    }
  }
  return {{"path", std::move(path)}, {"path_nodes", std::move(path_nodes)}};
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
    if (state.update_handler) {
      return ok_response(state.update_handler(params));
    }
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
