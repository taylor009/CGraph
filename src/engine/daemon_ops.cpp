#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cgraph {
namespace {

// Bounds on the source slice returned with a node so a single explain stays
// token-cheap even for a large function body.
constexpr std::size_t kMaxSnippetLines = 40;
constexpr std::size_t kMaxSnippetChars = 2000;

// Default caps so a broad query or a wide blast radius stays bounded. 0 means
// "no limit" when a caller passes it explicitly.
constexpr std::size_t kDefaultQueryLimit = 50;
constexpr std::size_t kDefaultImpactLimit = 200;
constexpr int kDefaultImpactDepth = 3;

// Context packing: a token budget to fill and how far around the focal symbol to
// gather candidates.
constexpr std::size_t kDefaultContextBudget = 6000;  // tokens
constexpr int kDefaultContextDepth = 2;

// Rough token estimate: ~4 characters per token. Good enough to pack a context
// bundle under a budget without pulling in a real tokenizer.
[[nodiscard]] std::size_t estimate_tokens(const std::string& text) {
  return (text.size() + 3) / 4;
}

[[nodiscard]] nlohmann::json error_response(std::string message) {
  return nlohmann::json{{"ok", false}, {"error", std::move(message)}};
}

[[nodiscard]] nlohmann::json ok_response(nlohmann::json result = nlohmann::json::object()) {
  return nlohmann::json{{"ok", true}, {"result", std::move(result)}};
}

// Degree centrality (normalized 0..1) computed by analyze_graph and stored on
// the node; 0 when absent (e.g. a snapshot that has not been analyzed).
[[nodiscard]] double node_centrality(const Node& node) {
  const auto it = node.properties.find("degree_centrality");
  if (it == node.properties.end()) {
    return 0.0;
  }
  const char* begin = it->second.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  return end == begin ? 0.0 : value;
}

// Compact, file-read-free descriptor: enough for an agent to open the symbol at
// the right place (source_file + 1-based line) and judge its importance
// (centrality, god_node) without a follow-up lookup.
[[nodiscard]] nlohmann::json node_brief(const Node& node) {
  nlohmann::json brief{{"id", node.id}, {"label", node.label}, {"kind", node.kind}};
  if (!node.source_file.empty()) {
    brief["source_file"] = node.source_file;
  }
  if (node.source_location && node.source_location->start_line > 0) {
    brief["line"] = node.source_location->start_line;
  }
  if (node.properties.contains("degree_centrality")) {
    brief["centrality"] = node_centrality(node);
  }
  if (node.properties.contains("god_node")) {
    brief["god_node"] = true;
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

// Enrich a brief with the focal node's full location block and an on-disk source
// snippet (bounded by read_source_snippet). Used wherever a single node is the
// subject and the code itself is worth returning.
[[nodiscard]] nlohmann::json with_source(nlohmann::json brief, const Node& node) {
  if (node.source_location && node.source_location->start_line > 0) {
    brief["location"] = {
        {"start_line", node.source_location->start_line},
        {"start_column", node.source_location->start_column},
        {"end_line", node.source_location->end_line},
        {"end_column", node.source_location->end_column}};
  }
  if (const auto snippet = read_source_snippet(node); !snippet.text.empty()) {
    brief["snippet"] = snippet.text;
    if (snippet.truncated) {
      brief["snippet_truncated"] = true;
    }
  }
  return brief;
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
  auto matches = matching_nodes(graph, needle);

  // Rank by importance so the most central (most-connected / god) nodes lead —
  // an agent reading the top few results lands on the symbols that matter.
  std::ranges::sort(matches, [](const Node* lhs, const Node* rhs) {
    const auto lc = node_centrality(*lhs);
    const auto rc = node_centrality(*rhs);
    if (lc != rc) {
      return lc > rc;
    }
    return lhs->label < rhs->label;  // stable, deterministic tiebreak
  });

  const auto total = matches.size();
  const auto limit = params.value("limit", kDefaultQueryLimit);
  if (limit > 0 && matches.size() > limit) {
    matches.resize(limit);
  }

  auto nodes = nlohmann::json::array();
  for (const auto* node : matches) {
    nodes.push_back(node_brief(*node));
  }
  const auto returned = nodes.size();  // capture before the move below
  return {{"nodes", std::move(nodes)}, {"total", total}, {"returned", returned}};
}

// Transitive blast radius: BFS from a node along directed edges. `dependents`
// follows edges *into* the node (callers, importers, subclasses — what breaks if
// you change it); `dependencies` follows edges *out* (what it relies on); `both`
// unions the two. Optionally filtered to one relation, bounded by depth, and
// capped. Results are ordered by depth, then by centrality within a depth.
[[nodiscard]] nlohmann::json impact_radius(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto id = params.value("id", std::string{});
  const auto direction = params.value("direction", std::string{"dependents"});
  const auto relation = params.value("relation", std::string{});
  const auto max_depth = std::max(0, params.value("max_depth", kDefaultImpactDepth));
  const auto limit = params.value("limit", kDefaultImpactLimit);

  const auto by_id = index_nodes(graph);
  if (!by_id.contains(id)) {
    return {{"id", id}, {"direction", direction}, {"max_depth", max_depth},
            {"total", 0}, {"returned", 0}, {"nodes", nlohmann::json::array()}};
  }

  const bool want_dependents = direction == "dependents" || direction == "both";
  const bool want_dependencies = direction == "dependencies" || direction == "both";

  // Adjacency in the requested direction(s), filtered by relation if given.
  struct Link {
    std::string to;
    std::string relation;
  };
  std::unordered_map<std::string, std::vector<Link>> adjacency;
  for (const auto& edge : graph.edges) {
    if (!relation.empty() && edge.relation != relation) {
      continue;
    }
    if (want_dependents) {
      adjacency[edge.target].push_back({edge.source, edge.relation});  // who points at target
    }
    if (want_dependencies) {
      adjacency[edge.source].push_back({edge.target, edge.relation});  // what source points to
    }
  }

  struct Reached {
    int depth = 0;
    std::string via;
  };
  std::unordered_map<std::string, Reached> reached;
  std::queue<std::string> frontier;
  reached[id] = {0, {}};
  frontier.push(id);
  while (!frontier.empty()) {
    const auto current = frontier.front();
    frontier.pop();
    const auto depth = reached[current].depth;
    if (depth >= max_depth) {
      continue;
    }
    const auto links = adjacency.find(current);
    if (links == adjacency.end()) {
      continue;
    }
    for (const auto& link : links->second) {
      if (reached.contains(link.to)) {
        continue;  // first (shortest) path wins
      }
      reached[link.to] = {depth + 1, link.relation};
      frontier.push(link.to);
    }
  }

  // Drop the seed itself; order by (depth asc, centrality desc).
  std::vector<const Node*> hits;
  for (const auto& [node_id, info] : reached) {
    if (node_id == id) {
      continue;
    }
    if (const auto it = by_id.find(node_id); it != by_id.end()) {
      hits.push_back(it->second);
    }
  }
  std::ranges::sort(hits, [&](const Node* lhs, const Node* rhs) {
    const auto ld = reached[lhs->id].depth;
    const auto rd = reached[rhs->id].depth;
    if (ld != rd) {
      return ld < rd;
    }
    const auto lc = node_centrality(*lhs);
    const auto rc = node_centrality(*rhs);
    if (lc != rc) {
      return lc > rc;
    }
    return lhs->label < rhs->label;
  });

  const auto total = hits.size();
  const bool truncated = limit > 0 && hits.size() > limit;
  if (truncated) {
    hits.resize(limit);
  }

  auto nodes = nlohmann::json::array();
  for (const auto* node : hits) {
    auto brief = node_brief(*node);
    brief["depth"] = reached[node->id].depth;
    if (!reached[node->id].via.empty()) {
      brief["via"] = reached[node->id].via;
    }
    nodes.push_back(std::move(brief));
  }

  nlohmann::json result{
      {"id", id}, {"direction", direction}, {"max_depth", max_depth},
      {"total", total}, {"returned", nodes.size()}, {"nodes", std::move(nodes)}};
  if (truncated) {
    result["truncated"] = true;
  }
  return result;
}

// Token-budgeted context bundle. Given a focal symbol (by id, label, or the
// highest-centrality match for a query) and a token budget, gather the
// surrounding neighborhood (undirected BFS to max_depth), rank it by
// (depth asc, centrality desc), and greedily pack source snippets until the
// budget fills. A candidate whose full snippet would overflow degrades to a
// brief-only entry (so the agent still learns the symbol exists and where);
// anything that no longer fits is counted in `omitted`. The focal node is always
// included with its snippet, even if it alone exceeds the budget.
[[nodiscard]] nlohmann::json pack_context(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto budget = params.value("budget", kDefaultContextBudget);
  const auto max_depth = std::max(0, params.value("max_depth", kDefaultContextDepth));
  const auto by_id = index_nodes(graph);

  // Resolve the focal node: exact id, then label, then the top-ranked match for
  // a free-text query.
  const Node* focal = nullptr;
  if (const auto id = params.value("id", std::string{}); !id.empty()) {
    if (const auto it = by_id.find(id); it != by_id.end()) {
      focal = it->second;
    } else {
      for (const auto& node : graph.nodes) {
        if (node.label == id) {
          focal = &node;
          break;
        }
      }
    }
  }
  if (focal == nullptr) {
    if (const auto needle = params.value("q", params.value("query", std::string{})); !needle.empty()) {
      for (const auto* match : matching_nodes(graph, needle)) {
        if (focal == nullptr || node_centrality(*match) > node_centrality(*focal)) {
          focal = match;
        }
      }
    }
  }
  if (focal == nullptr) {
    return {{"focus", nullptr}, {"budget", budget}, {"tokens_used", 0},
            {"included", nlohmann::json::array()}, {"omitted", 0}};
  }

  // Undirected neighborhood: callers, callees, container, and siblings reachable
  // within max_depth are all relevant context for understanding the focal symbol.
  struct Reached {
    int depth = 0;
    std::string via;
  };
  std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> adjacency;
  for (const auto& edge : graph.edges) {
    adjacency[edge.source].push_back({edge.target, edge.relation});
    adjacency[edge.target].push_back({edge.source, edge.relation});
  }
  std::unordered_map<std::string, Reached> reached;
  std::queue<std::string> frontier;
  reached[focal->id] = {0, {}};
  frontier.push(focal->id);
  while (!frontier.empty()) {
    const auto current = frontier.front();
    frontier.pop();
    const auto depth = reached[current].depth;
    if (depth >= max_depth) {
      continue;
    }
    const auto links = adjacency.find(current);
    if (links == adjacency.end()) {
      continue;
    }
    for (const auto& [to, relation] : links->second) {
      if (reached.contains(to)) {
        continue;
      }
      reached[to] = {depth + 1, relation};
      frontier.push(to);
    }
  }

  std::vector<const Node*> candidates;
  for (const auto& [node_id, info] : reached) {
    if (node_id == focal->id) {
      continue;
    }
    if (const auto it = by_id.find(node_id); it != by_id.end()) {
      candidates.push_back(it->second);
    }
  }
  std::ranges::sort(candidates, [&](const Node* lhs, const Node* rhs) {
    const auto ld = reached[lhs->id].depth;
    const auto rd = reached[rhs->id].depth;
    if (ld != rd) {
      return ld < rd;  // nearer first
    }
    const auto lc = node_centrality(*lhs);
    const auto rc = node_centrality(*rhs);
    if (lc != rc) {
      return lc > rc;  // more important first
    }
    return lhs->label < rhs->label;
  });

  // The focal node always leads, with its snippet.
  auto focus = with_source(node_brief(*focal), *focal);
  std::size_t used = estimate_tokens(focus.dump());

  auto included = nlohmann::json::array();
  std::size_t omitted = 0;
  for (const auto* node : candidates) {
    const auto depth = reached[node->id].depth;
    const auto& via = reached[node->id].via;

    auto full = with_source(node_brief(*node), *node);
    full["depth"] = depth;
    if (!via.empty()) {
      full["via"] = via;
    }
    const auto full_cost = estimate_tokens(full.dump());
    if (used + full_cost <= budget) {
      used += full_cost;
      included.push_back(std::move(full));
      continue;
    }

    // Full snippet overflows: keep a brief-only entry if it still fits.
    auto brief = node_brief(*node);
    brief["depth"] = depth;
    if (!via.empty()) {
      brief["via"] = via;
    }
    brief["snippet_omitted"] = true;
    const auto brief_cost = estimate_tokens(brief.dump());
    if (used + brief_cost <= budget) {
      used += brief_cost;
      included.push_back(std::move(brief));
    } else {
      ++omitted;
    }
  }

  nlohmann::json result{
      {"focus", std::move(focus)},
      {"budget", budget},
      {"tokens_used", used},
      {"included", std::move(included)},
      {"omitted", omitted}};
  if (omitted > 0 || used > budget) {
    result["truncated"] = true;
  }
  return result;
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

      auto result = with_source(node_brief(node), node);
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
  if (op == "impact") {
    return ok_response(impact_radius(*graph, params));
  }
  if (op == "context") {
    return ok_response(pack_context(*graph, params));
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
