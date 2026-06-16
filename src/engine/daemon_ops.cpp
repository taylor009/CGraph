#include "cgraph/daemon_ops.hpp"

#include "cgraph/fragment_json.hpp"
#include "cgraph/graph_builder.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/semantic_connectivity.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// God nodes can touch hundreds of edges; explain caps the neighbor list (most
// central first) so one call stays token-cheap.
constexpr std::size_t kDefaultExplainNeighborLimit = 100;

// How many did-you-mean candidates a missed lookup returns.
constexpr std::size_t kMaxSuggestions = 5;

// Context packing: a token budget to fill and how far around the focal symbol to
// gather candidates.
constexpr std::size_t kDefaultContextBudget = 6000;  // tokens
constexpr int kDefaultContextDepth = 2;
// Knapsack packing gathers a wider ego graph (validated sweet spot; k=4 is past
// the crossover). See research/2510.00446.
constexpr int kKnapsackContextDepth = 3;
// Hard ceiling on the knapsack DP capacity so a pathological `budget` param can
// never blow up the O(n*capacity) table.
constexpr std::size_t kMaxKnapsackCapacity = 50000;

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

// Lowercased alphanumeric terms, breaking camelCase and snake_case and keeping
// terms of length >= 3. Mirrors the offline harness's relevance value
// (research/select.py::_terms) so the knapsack scorer matches what step A validated.
[[nodiscard]] std::unordered_set<std::string> lexical_terms(std::string_view text) {
  std::unordered_set<std::string> terms;
  std::string word;
  const auto flush = [&]() {
    if (word.empty()) {
      return;
    }
    std::string sub;
    const auto push_sub = [&]() {
      if (sub.size() >= 3) {
        terms.insert(sub);
      }
      sub.clear();
    };
    for (std::size_t i = 0; i < word.size(); ++i) {
      const auto ch = static_cast<unsigned char>(word[i]);
      const bool upper = std::isupper(ch) != 0;
      const bool prev_upper = i > 0 && std::isupper(static_cast<unsigned char>(word[i - 1])) != 0;
      const bool next_lower =
          i + 1 < word.size() && std::islower(static_cast<unsigned char>(word[i + 1])) != 0;
      if (i > 0 && upper && (!prev_upper || next_lower)) {
        push_sub();
      }
      sub.push_back(static_cast<char>(std::tolower(ch)));
    }
    push_sub();
    word.clear();
  };
  for (const char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
      word.push_back(c);
    } else {
      flush();
    }
  }
  flush();
  return terms;
}

// Fraction of query terms also present in the node label (0..1).
[[nodiscard]] double query_term_overlap(const std::unordered_set<std::string>& query_terms,
                                        std::string_view label) {
  if (query_terms.empty()) {
    return 0.0;
  }
  const auto label_terms = lexical_terms(label);
  std::size_t hit = 0;
  for (const auto& term : query_terms) {
    if (label_terms.contains(term)) {
      ++hit;
    }
  }
  return static_cast<double>(hit) / static_cast<double>(query_terms.size());
}

// Knapsack item weight: token cost of the node's (capped) source slice only --
// NOT estimate_tokens(json_entry.dump()). Step A (research/2510.00446) showed the
// JSON-entry overhead (mangled id + absolute path + location keys) flattens the
// weight spread and degenerates the knapsack toward greedy; weighting by the slice
// cost is the load-bearing fix that recovers the win.
[[nodiscard]] std::size_t slice_token_cost(const Node& node) {
  if (const auto snippet = read_source_snippet(node); !snippet.text.empty()) {
    return std::max<std::size_t>(1, estimate_tokens(snippet.text));
  }
  return std::max<std::size_t>(1, estimate_tokens(node.label));
}

[[nodiscard]] std::unordered_map<std::string, const Node*> index_nodes(const GraphSnapshot& graph) {
  std::unordered_map<std::string, const Node*> by_id;
  by_id.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    by_id.emplace(node.id, &node);
  }
  return by_id;
}

[[nodiscard]] char ascii_lower(char value) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] std::string ascii_lower(std::string_view text) {
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(), [](char value) { return ascii_lower(value); });
  return lowered;
}

// Case-insensitive substring search; the needle must already be lowercase.
// Symbol queries are typed by an agent that rarely knows the exact casing.
[[nodiscard]] bool contains_ci(std::string_view haystack, std::string_view lower_needle) {
  if (lower_needle.empty()) {
    return true;
  }
  const auto it = std::search(
      haystack.begin(), haystack.end(), lower_needle.begin(), lower_needle.end(),
      [](char hay, char needle) { return ascii_lower(hay) == needle; });
  return it != haystack.end();
}

[[nodiscard]] std::vector<const Node*> matching_nodes(const GraphSnapshot& graph, const std::string& needle) {
  const auto lower_needle = ascii_lower(needle);
  std::vector<const Node*> matches;
  for (const auto& node : graph.nodes) {
    if (is_memory_node_id(node.id)) {
      continue;  // session-memory checkpoints are not code matches; recall surfaces them
    }
    if (contains_ci(node.id, lower_needle) || contains_ci(node.label, lower_needle)) {
      matches.push_back(&node);
    }
  }
  return matches;
}

// Levenshtein distance, abandoned (returning cap + 1) as soon as it must exceed
// `cap` — suggestion scoring only cares about near misses.
[[nodiscard]] std::size_t bounded_edit_distance(std::string_view a, std::string_view b, std::size_t cap) {
  if (a.size() > b.size()) {
    std::swap(a, b);
  }
  if (b.size() - a.size() > cap) {
    return cap + 1;
  }
  std::vector<std::size_t> row(a.size() + 1);
  for (std::size_t i = 0; i <= a.size(); ++i) {
    row[i] = i;
  }
  for (std::size_t j = 1; j <= b.size(); ++j) {
    std::size_t diagonal = row[0];
    row[0] = j;
    std::size_t row_min = row[0];
    for (std::size_t i = 1; i <= a.size(); ++i) {
      const std::size_t substitution = diagonal + (a[i - 1] == b[j - 1] ? 0 : 1);
      diagonal = row[i];
      row[i] = std::min({row[i] + 1, row[i - 1] + 1, substitution});
      row_min = std::min(row_min, row[i]);
    }
    if (row_min > cap) {
      return cap + 1;
    }
  }
  return row.back();
}

// The leading symbol token of a label: everything before the first '(' or
// whitespace. Labels often carry full signatures ("merge_fragments(GraphSnapshot&
// graph, ...)"); agents pass bare names ("merge_fragments").
[[nodiscard]] std::string_view label_symbol(const Node& node) {
  const std::string_view label{node.label};
  const auto cut = label.find_first_of("( \t\n");
  return cut == std::string_view::npos ? label : label.substr(0, cut);
}

// Closest labels to a missed lookup, so an agent can self-correct (wrong
// casing, typo, label-vs-id confusion) instead of concluding the symbol does
// not exist and falling back to grep.
[[nodiscard]] nlohmann::json suggest_similar(const GraphSnapshot& graph, const std::string& needle) {
  auto suggestions = nlohmann::json::array();
  if (needle.empty()) {
    return suggestions;
  }
  const auto lower_needle = ascii_lower(needle);
  const auto cap = std::max<std::size_t>(2, lower_needle.size() / 3);

  std::vector<std::pair<std::size_t, const Node*>> scored;
  for (const auto& node : graph.nodes) {
    auto distance = bounded_edit_distance(lower_needle, ascii_lower(label_symbol(node)), cap);
    // The trailing segment of the id (after the last separator) is usually the
    // bare symbol name; match against it too so path-qualified ids still rank.
    if (distance > cap) {
      const auto tail_start = node.id.find_last_of(":/.");
      if (tail_start != std::string::npos && tail_start + 1 < node.id.size()) {
        const auto tail = std::string_view{node.id}.substr(tail_start + 1);
        distance = bounded_edit_distance(lower_needle, ascii_lower(tail), cap);
      }
    }
    if (distance <= cap) {
      scored.emplace_back(distance, &node);
    }
  }
  std::ranges::sort(scored, [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;  // closer first
    }
    const auto lc = node_centrality(*lhs.second);
    const auto rc = node_centrality(*rhs.second);
    if (lc != rc) {
      return lc > rc;  // more important first
    }
    return lhs.second->label < rhs.second->label;
  });
  if (scored.size() > kMaxSuggestions) {
    scored.resize(kMaxSuggestions);
  }
  for (const auto& [distance, node] : scored) {
    suggestions.push_back(node_brief(*node));
  }
  return suggestions;
}

// Resolve a node by exact id, exact label, or bare symbol name (the label's
// leading token, case-insensitive) — every id-taking op accepts any of these,
// so an agent can pass a symbol name without a prior query round-trip. When a
// bare name is ambiguous, the highest-centrality match wins; the response
// echoes the resolved id so the agent sees which one.
[[nodiscard]] const Node* resolve_node(
    const GraphSnapshot& graph,
    const std::unordered_map<std::string, const Node*>& by_id,
    const std::string& key) {
  if (const auto it = by_id.find(key); it != by_id.end()) {
    return it->second;
  }
  for (const auto& node : graph.nodes) {
    if (node.label == key) {
      return &node;
    }
  }
  const auto lower_key = ascii_lower(key);
  const Node* best = nullptr;
  for (const auto& node : graph.nodes) {
    if (ascii_lower(label_symbol(node)) != lower_key) {
      continue;
    }
    if (best == nullptr || node_centrality(node) > node_centrality(*best) ||
        (node_centrality(node) == node_centrality(*best) && node.label < best->label)) {
      best = &node;
    }
  }
  return best;
}

[[nodiscard]] nlohmann::json query_graph(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto needle = params.value("q", params.value("query", std::string{}));
  auto matches = matching_nodes(graph, needle);

  // Optional narrowing: `kind` (exact, e.g. "function") and `file` (substring
  // of the source path), both case-insensitive.
  if (const auto kind = ascii_lower(params.value("kind", std::string{})); !kind.empty()) {
    std::erase_if(matches, [&](const Node* node) { return ascii_lower(node->kind) != kind; });
  }
  if (const auto file = ascii_lower(params.value("file", std::string{})); !file.empty()) {
    std::erase_if(matches, [&](const Node* node) { return !contains_ci(node->source_file, file); });
  }

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
  nlohmann::json result{{"nodes", std::move(nodes)}, {"total", total}, {"returned", returned}};
  if (total == 0) {
    result["suggestions"] = suggest_similar(graph, needle);
  }
  return result;
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
  const auto* seed = resolve_node(graph, by_id, id);
  if (seed == nullptr) {
    return {{"id", id}, {"found", false}, {"direction", direction}, {"max_depth", max_depth},
            {"total", 0}, {"returned", 0}, {"nodes", nlohmann::json::array()},
            {"suggestions", suggest_similar(graph, id)}};
  }
  // The canonical id (the requested key may have been a label).
  const auto& seed_id = seed->id;

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
  reached[seed_id] = {0, {}};
  frontier.push(seed_id);
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
    if (node_id == seed_id) {
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

  // Echo the canonical id (the request may have used the label).
  nlohmann::json result{
      {"id", seed_id}, {"direction", direction}, {"max_depth", max_depth},
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
  // Packing strategy: "greedy" (default, historical behavior) or "knapsack" (the
  // step-A-validated 0/1 knapsack fill). Knapsack gathers a wider ego graph by
  // default. The flag is the rollback boundary: greedy is untouched.
  const auto packing = params.value("packing", std::string{"greedy"});
  // Adaptive relevance-gated gather (flag-gated, default "fixed" = unchanged): keeps
  // the full 2-hop core but expands past depth 1 only along query-relevant nodes,
  // reaching beyond 2 hops without the full 3-hop fan-out. Implies the knapsack fill.
  const auto gather = params.value("gather", std::string{"fixed"});
  const bool adaptive = gather == "adaptive";
  const double gather_theta = std::clamp(params.value("gather_theta", 0.05), 0.0, 1.0);
  const bool use_knapsack = packing == "knapsack" || adaptive;
  const int default_depth = use_knapsack ? kKnapsackContextDepth : kDefaultContextDepth;
  const auto max_depth = std::max(0, params.value("max_depth", default_depth));
  const auto by_id = index_nodes(graph);

  // Resolve the focal node: exact id, then label, then the top-ranked match for
  // a free-text query.
  const auto id = params.value("id", std::string{});
  const auto needle = params.value("q", params.value("query", std::string{}));
  const Node* focal = id.empty() ? nullptr : resolve_node(graph, by_id, id);
  if (focal == nullptr && !needle.empty()) {
    for (const auto* match : matching_nodes(graph, needle)) {
      if (focal == nullptr || node_centrality(*match) > node_centrality(*focal)) {
        focal = match;
      }
    }
  }
  if (focal == nullptr) {
    return {{"focus", nullptr}, {"budget", budget}, {"tokens_used", 0},
            {"packing", use_knapsack ? "knapsack" : "greedy"}, {"gather", adaptive ? "adaptive" : "fixed"},
            {"included", nlohmann::json::array()}, {"omitted", 0},
            {"suggestions", suggest_similar(graph, id.empty() ? needle : id)}};
  }

  // Query terms for the adaptive gather gate and the knapsack relevance value.
  const auto query_terms = lexical_terms(needle);

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
  // Adaptive reach accounting: how many frontier nodes the relevance gate rejected
  // past the 2-hop core. Reported on the response so callers/telemetry can see
  // whether the gate actually narrowed the third hop.
  int gated_at_core = 0;
  while (!frontier.empty()) {
    const auto current = frontier.front();
    frontier.pop();
    const auto depth = reached[current].depth;
    if (depth >= max_depth) {
      continue;
    }
    // Adaptive gather: past the 2-hop core, expand a node only when it is relevant
    // to the query, so the third hop follows relevant paths, not the whole ball.
    if (adaptive && depth >= 2) {
      const auto node_it = by_id.find(current);
      if (node_it == by_id.end() ||
          query_term_overlap(query_terms, node_it->second->label) < gather_theta) {
        ++gated_at_core;
        continue;
      }
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
  int expanded_past_core = 0;  // candidates reached beyond the 2-hop core (depth >= 3)
  for (const auto& [node_id, info] : reached) {
    if (node_id == focal->id) {
      continue;
    }
    if (is_memory_node_id(node_id)) {
      continue;  // memory checkpoints never enter code context, even when a concerns edge makes them adjacent
    }
    if (const auto it = by_id.find(node_id); it != by_id.end()) {
      candidates.push_back(it->second);
      if (info.depth >= 3) {
        ++expanded_past_core;
      }
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

  // Knapsack packing path (flag-gated; greedy below is the default and unchanged).
  // 0/1 knapsack over candidates: weight = source-slice token cost (the load-bearing
  // step-A fix), value = relevance (nearer hops + query-term overlap). The focal is
  // always included; the knapsack fills the remaining budget. No brief-degradation
  // here -- selection is whole-or-nothing, matching the validated harness.
  if (use_knapsack) {
    std::vector<std::size_t> weight(candidates.size());
    std::vector<double> value(candidates.size());
    std::size_t total_weight = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const auto* node = candidates[i];
      weight[i] = slice_token_cost(*node);
      const auto depth = reached[node->id].depth;
      value[i] = 1.0 / (1.0 + static_cast<double>(depth)) + query_term_overlap(query_terms, node->label);
      total_weight += weight[i];
    }

    const std::size_t focal_cost = slice_token_cost(*focal);
    const std::size_t raw_capacity = budget > focal_cost ? budget - focal_cost : 0;
    // DP-capacity guard: never allocate beyond what the candidates can fill, and
    // clamp pathological budgets so the O(n*capacity) table stays bounded.
    const std::size_t capacity = std::min({raw_capacity, total_weight, kMaxKnapsackCapacity});

    std::vector<const Node*> chosen;
    std::size_t used = focal_cost;
    if (capacity > 0 && !candidates.empty()) {
      const std::size_t n = candidates.size();
      // dp[i][c] = max total value using the first i candidates within weight c.
      std::vector<std::vector<double>> dp(n + 1, std::vector<double>(capacity + 1, 0.0));
      for (std::size_t i = 1; i <= n; ++i) {
        const auto w = weight[i - 1];
        const auto v = value[i - 1];
        for (std::size_t c = 0; c <= capacity; ++c) {
          dp[i][c] = dp[i - 1][c];
          if (w <= c) {
            const double take = dp[i - 1][c - w] + v;
            if (take > dp[i][c]) {
              dp[i][c] = take;
            }
          }
        }
      }
      // Backtrack to recover the chosen items (size_t-safe reverse iteration).
      std::size_t c = capacity;
      for (std::size_t i = n; i-- > 0;) {
        if (dp[i + 1][c] != dp[i][c]) {
          chosen.push_back(candidates[i]);
          used += weight[i];
          c -= weight[i];
        }
      }
    }

    // Emit nearest-first for a stable, readable bundle (selection is the DP above).
    std::ranges::sort(chosen, [&](const Node* lhs, const Node* rhs) {
      const auto ld = reached[lhs->id].depth;
      const auto rd = reached[rhs->id].depth;
      if (ld != rd) {
        return ld < rd;
      }
      return lhs->label < rhs->label;
    });

    auto included = nlohmann::json::array();
    for (const auto* node : chosen) {
      auto full = with_source(node_brief(*node), *node);
      full["depth"] = reached[node->id].depth;
      if (const auto& via = reached[node->id].via; !via.empty()) {
        full["via"] = via;
      }
      included.push_back(std::move(full));
    }
    const std::size_t omitted = candidates.size() - chosen.size();
    nlohmann::json result{
        {"focus", std::move(focus)},
        {"budget", budget},
        {"tokens_used", used},
        {"packing", "knapsack"},
        {"gather", adaptive ? "adaptive" : "fixed"},
        {"included", std::move(included)},
        {"omitted", omitted}};
    if (omitted > 0) {
      result["truncated"] = true;
    }
    // Adaptive reach summary: did the relevance gate actually expand the third hop,
    // and how much did it prune? expanded_past_core == 0 is the honest "collapsed
    // to the 2-hop core" signal. Present only for adaptive gather.
    if (adaptive) {
      result["reach"] = {{"candidates", candidates.size()},
                         {"expanded_past_core", expanded_past_core},
                         {"gated_at_core", gated_at_core}};
    }
    return result;
  }

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
      {"packing", "greedy"},
      {"gather", "fixed"},
      {"included", std::move(included)},
      {"omitted", omitted}};
  if (omitted > 0 || used > budget) {
    result["truncated"] = true;
  }
  return result;
}

[[nodiscard]] nlohmann::json explain_node(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto id = params.value("id", std::string{});
  // "in" keeps only edges into the node (callers/importers), "out" only edges
  // it points at (callees/imports); default is both.
  const auto direction = params.value("direction", std::string{"both"});
  // Optional typed traversal: when set, keep only edges of this relation, using
  // the same exact match as impact_radius (no case-folding) so the two ops share
  // one filter dialect. find-callers = direction:in + relation:CALLS, etc.
  const auto relation = params.value("relation", std::string{});
  const auto limit = params.value("limit", kDefaultExplainNeighborLimit);

  const auto by_id = index_nodes(graph);
  const auto* node = resolve_node(graph, by_id, id);
  if (node == nullptr) {
    return {{"id", id}, {"found", false}, {"neighbors", nlohmann::json::array()},
            {"suggestions", suggest_similar(graph, id)}};
  }

  struct NeighborEntry {
    nlohmann::json entry;
    double centrality = 0.0;
  };
  std::vector<NeighborEntry> neighbors;
  for (const auto& edge : graph.edges) {
    const bool outgoing = edge.source == node->id;
    const bool incoming = edge.target == node->id;
    if (!outgoing && !incoming) {
      continue;
    }
    if ((direction == "out" && !outgoing) || (direction == "in" && !incoming)) {
      continue;
    }
    if (!relation.empty() && edge.relation != relation) {
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
    double centrality = 0.0;
    if (const auto it = by_id.find(other_id); it != by_id.end()) {
      entry["node"] = node_brief(*it->second);
      centrality = node_centrality(*it->second);
    }
    neighbors.push_back({std::move(entry), centrality});
  }

  // Most important neighbors first, so a capped list still shows what matters
  // when the node is a heavily-connected hub.
  std::ranges::stable_sort(neighbors, [](const NeighborEntry& lhs, const NeighborEntry& rhs) {
    return lhs.centrality > rhs.centrality;
  });
  const auto neighbor_count = neighbors.size();
  const bool truncated = limit > 0 && neighbors.size() > limit;
  if (truncated) {
    neighbors.resize(limit);
  }

  auto entries = nlohmann::json::array();
  for (auto& neighbor : neighbors) {
    entries.push_back(std::move(neighbor.entry));
  }
  auto result = with_source(node_brief(*node), *node);
  result["neighbor_count"] = neighbor_count;
  result["neighbors"] = std::move(entries);
  if (truncated) {
    result["truncated"] = true;
  }
  return result;
}

[[nodiscard]] nlohmann::json shortest_path(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto by_id_nodes = index_nodes(graph);
  const auto resolve_endpoint = [&](const std::string& key) {
    const auto* node = resolve_node(graph, by_id_nodes, key);
    return node == nullptr ? key : node->id;
  };
  // Endpoints accept labels too; flag the missing one(s) with suggestions so an
  // empty path is distinguishable from "no route exists".
  const auto source_key = params.value("source", std::string{});
  const auto target_key = params.value("target", std::string{});
  const auto source = resolve_endpoint(source_key);
  const auto target = resolve_endpoint(target_key);
  nlohmann::json missing = nlohmann::json::object();
  if (!by_id_nodes.contains(source)) {
    missing["source_found"] = false;
    missing["source_suggestions"] = suggest_similar(graph, source_key);
  }
  if (!by_id_nodes.contains(target)) {
    missing["target_found"] = false;
    missing["target_suggestions"] = suggest_similar(graph, target_key);
  }
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
    nlohmann::json result{{"path", path}, {"path_nodes", nlohmann::json::array()}};
    result.update(missing);
    return result;
  }

  std::vector<std::string> reversed;
  for (std::string cursor = target; !cursor.empty(); cursor = previous[cursor]) {
    reversed.push_back(cursor);
  }
  std::ranges::reverse(reversed);

  // `path` stays a bare id list for existing consumers; `path_nodes` carries the
  // label/kind/source_file/line for each hop so an agent can read the route.
  auto path_nodes = nlohmann::json::array();
  for (const auto& item : reversed) {
    path.push_back(item);
    if (const auto it = by_id_nodes.find(item); it != by_id_nodes.end()) {
      path_nodes.push_back(node_brief(*it->second));
    } else {
      path_nodes.push_back({{"id", item}});
    }
  }
  nlohmann::json result{{"path", std::move(path)}, {"path_nodes", std::move(path_nodes)}};
  result.update(missing);
  return result;
}

// Agent-facing build-state label. "building" covers the Empty snapshot the
// daemon serves while the initial build runs on its worker thread.
[[nodiscard]] const char* build_state_label(BuildState value) {
  switch (value) {
    case BuildState::Empty:
      return "building";
    case BuildState::DeterministicReady:
      return "ready";
    case BuildState::Enriching:
      return "enriching";
    case BuildState::Idle:
      return "idle";
    case BuildState::Failed:
      return "failed";
  }
  return "failed";
}

// While the initial build is still running, every read op serves the empty
// snapshot; stamp those results so an agent can tell "no match" from "not
// built yet" instead of silently falling back to grep.
[[nodiscard]] nlohmann::json annotate_build_state(nlohmann::json result, const GraphSnapshot& graph) {
  if (graph.build_state == BuildState::Empty) {
    result["graph_state"] = "building";
    result["note"] = "graph build in progress; results may be empty or incomplete - retry shortly or poll status";
  }
  return result;
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

  const std::chrono::duration<double> uptime = StatsClock::now() - state.start_time;

  nlohmann::json payload{
      {"pid", state.pid},
      {"uptime_seconds", uptime.count()},
      {"node_count", graph.nodes.size()},
      {"edge_count", graph.edges.size()},
      {"build_state", build_state_label(graph.build_state)},
      {"cache_hit_rate", graph.cache_hit_rate},
      {"enrichment_state", enrichment_state(state.enrichment_state)},
      {"enrichment_pending", state.enrichment_pending},
      {"enrichment_running", state.enrichment_running},
      {"enrichment_stale", state.enrichment_stale},
      {"enrichment_failed", state.enrichment_failed},
      {"enrichment_plans_run", state.enrichment_plans_run},
      {"watching", state.watching},
      {"incremental_updates", state.incremental_updates},
      {"ops", op_stats_json(state.op_stats)},
  };
  // Modeled cache saving = files_reused x mean(per-file extract time) from the
  // most recent build's Layer A timings. Omitted (never fabricated) when there
  // was no reuse or no per-file mean to model from.
  if (state.last_files_cache_hit > 0 && state.last_extract_mean_ms > 0.0) {
    payload["cache_saved_ms_estimate"] =
        static_cast<double>(state.last_files_cache_hit) * state.last_extract_mean_ms;
  }

  // Semantic connectivity: how well the host-authored layer connects to code.
  // Computed from the current snapshot so it reflects live enrichment.
  const auto connectivity = compute_semantic_connectivity(graph);
  payload["semantic"] = {
      {"doc_nodes", connectivity.doc_nodes},
      {"concept_nodes", connectivity.concept_nodes},
      {"connected_docs", connectivity.connected_docs},
      {"orphan_docs", connectivity.orphan_docs},
      {"orphan_concepts", connectivity.orphan_concepts},
      {"doc_code_edges", connectivity.doc_code_edges},
      {"connectivity_rate", connectivity.connectivity_rate},
  };
  return payload;
}

// --- Session memory: checkpoint write (remember) + recall --------------------
// Checkpoints are agent-authored notes that survive /clear (graphd is external
// to Claude's context). The body is a markdown file under memory_dir; the node
// points at it via source_file so the existing snippet machinery surfaces it.
// See graph-session-memory.

// ~4k tokens; large enough for a useful summary, capped so recall stays bounded.
constexpr std::size_t kMaxCheckpointBodyChars = 16384;

// A filesystem-safe slug: keep [a-z0-9], collapse every other run to a single
// '-'. This strips path separators and dots, so a title can never traverse out
// of memory_dir.
[[nodiscard]] std::string slugify(std::string_view title) {
  std::string slug;
  for (const char ch : title) {
    const auto uc = static_cast<unsigned char>(ch);
    if (std::isalnum(uc) != 0) {
      slug.push_back(static_cast<char>(std::tolower(uc)));
    } else if (!slug.empty() && slug.back() != '-') {
      slug.push_back('-');
    }
  }
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  if (slug.empty()) {
    slug = "checkpoint";
  }
  if (slug.size() > 60) {
    slug.resize(60);
  }
  return slug;
}

[[nodiscard]] nlohmann::json remember_checkpoint(DaemonState& state, const nlohmann::json& params) {
  if (state.memory_dir.empty()) {
    return error_response("session memory is not enabled on this daemon");
  }
  const auto title = params.value("title", std::string{});
  const auto body = params.value("body", std::string{});
  if (title.empty()) {
    return error_response("remember requires a non-empty title");
  }
  if (body.size() > kMaxCheckpointBodyChars) {
    return error_response("checkpoint body exceeds " + std::to_string(kMaxCheckpointBodyChars) + " chars");
  }

  // Resolve touches against the current snapshot BEFORE mutating; an unresolved
  // entry yields no edge (and is reported), never a dangling target.
  const auto graph = read_graph_snapshot(state);
  const auto by_id = index_nodes(*graph);
  std::vector<std::string> resolved;
  auto unresolved = nlohmann::json::array();
  for (const auto& touch : params.value("touches", nlohmann::json::array())) {
    if (!touch.is_string()) {
      continue;
    }
    const auto key = touch.get<std::string>();
    if (const auto* node = resolve_node(*graph, by_id, key); node != nullptr) {
      resolved.push_back(node->id);
    } else {
      unresolved.push_back(key);
    }
  }

  // Wall-clock millis stamp the id, created_at, and recency ordering (a write
  // boundary, like the ledger flush — the running substrate stays monotonic).
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const auto ts = std::to_string(ms);
  const auto id = "memory:checkpoint:" + ts;
  const auto filename = ts + "-" + slugify(title) + ".md";

  std::error_code ec;
  std::filesystem::create_directories(state.memory_dir, ec);
  const auto path = state.memory_dir / filename;
  // Defensive sandbox: the resolved path must stay inside memory_dir.
  const auto canon_dir = std::filesystem::weakly_canonical(state.memory_dir, ec).generic_string();
  const auto canon_path = std::filesystem::weakly_canonical(path, ec).generic_string();
  if (canon_dir.empty() || canon_path.rfind(canon_dir, 0) != 0) {
    return error_response("checkpoint path escapes the memory directory");
  }
  const auto content = "# " + title + "\n\n" + body + "\n";
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      return error_response("failed to write checkpoint body: " + path.generic_string());
    }
    out << content;
  }
  // Span the whole body so the existing snippet machinery (read_source_snippet)
  // surfaces it on recall / graph_context. Bounded by kMaxSnippetLines downstream.
  const auto line_count = static_cast<std::uint32_t>(std::count(content.begin(), content.end(), '\n'));

  std::string tags;
  for (const auto& tag : params.value("tags", nlohmann::json::array())) {
    if (!tag.is_string()) {
      continue;
    }
    if (!tags.empty()) {
      tags += ",";
    }
    tags += tag.get<std::string>();
  }

  Node node;
  node.id = id;
  node.label = title;
  node.source_file = path.generic_string();
  node.source_location =
      SourceLocation{.start_line = 1, .start_column = 0, .end_line = std::max<std::uint32_t>(1, line_count), .end_column = 0};
  node.kind = "checkpoint";
  node.confidence = Confidence::Inferred;
  node.properties["created_at"] = ts;
  if (!tags.empty()) {
    node.properties["tags"] = tags;
  }

  Fragment fragment;
  fragment.nodes.push_back(node);
  for (const auto& target : resolved) {
    fragment.edges.push_back(
        Edge{.source = id, .target = target, .relation = "concerns", .confidence = Confidence::Inferred});
  }

  // Durable sidecar: the fragment beside the body is the source of truth for this
  // checkpoint. It is re-overlaid after every graph rebuild (see ingest_all_memory),
  // so the checkpoint survives restarts, incremental edits, and full rescans -- the
  // live snapshot node below is only the immediate, in-session copy.
  const auto sidecar = std::filesystem::path(path).replace_extension(".json");
  {
    std::ofstream out(sidecar, std::ios::binary);
    if (!out) {
      return error_response("failed to write checkpoint sidecar: " + sidecar.generic_string());
    }
    out << to_json(fragment).dump(2) << '\n';
  }

  mutate_graph_snapshot(state, [&](GraphSnapshot& current) { merge_fragment(current, fragment); });

  return ok_response({
      {"id", id},
      {"label", title},
      {"source_file", node.source_file},
      {"created_at", ts},
      {"concerns", resolved.size()},
      {"unresolved", std::move(unresolved)},
      {"written", true},
  });
}

[[nodiscard]] nlohmann::json recall_checkpoints(const GraphSnapshot& graph, const nlohmann::json& params) {
  const auto query = ascii_lower(params.value("query", params.value("q", std::string{})));
  const auto limit = params.value("limit", std::size_t{10});

  const auto by_id = index_nodes(graph);
  std::vector<const Node*> checkpoints;
  for (const auto& node : graph.nodes) {
    if (!is_memory_node_id(node.id) || node.kind != "checkpoint") {
      continue;
    }
    if (!query.empty()) {
      const auto tags = node.properties.find("tags");
      const bool hit = contains_ci(node.label, query) ||
                       (tags != node.properties.end() && contains_ci(tags->second, query));
      if (!hit) {
        continue;
      }
    }
    checkpoints.push_back(&node);
  }

  // Newest-first by created_at millis (longer string = larger number; equal
  // lengths compare lexically = numerically), id as a deterministic tiebreak.
  const auto created_at = [](const Node* node) {
    const auto it = node->properties.find("created_at");
    return it == node->properties.end() ? std::string{} : it->second;
  };
  std::ranges::sort(checkpoints, [&](const Node* lhs, const Node* rhs) {
    const auto lv = created_at(lhs);
    const auto rv = created_at(rhs);
    if (lv.size() != rv.size()) {
      return lv.size() > rv.size();
    }
    if (lv != rv) {
      return lv > rv;
    }
    return lhs->id > rhs->id;
  });

  const auto total = checkpoints.size();
  if (limit > 0 && checkpoints.size() > limit) {
    checkpoints.resize(limit);
  }

  std::unordered_map<std::string, std::vector<std::string>> concerns;
  for (const auto& edge : graph.edges) {
    if (edge.relation == "concerns" && is_memory_node_id(edge.source)) {
      concerns[edge.source].push_back(edge.target);
    }
  }

  auto items = nlohmann::json::array();
  for (const auto* checkpoint : checkpoints) {
    auto entry = with_source(node_brief(*checkpoint), *checkpoint);  // body snippet from source_file
    entry["created_at"] = created_at(checkpoint);
    if (const auto tags = checkpoint->properties.find("tags"); tags != checkpoint->properties.end()) {
      entry["tags"] = tags->second;
    }
    auto links = nlohmann::json::array();
    if (const auto it = concerns.find(checkpoint->id); it != concerns.end()) {
      for (const auto& target : it->second) {
        if (const auto node = by_id.find(target); node != by_id.end()) {
          links.push_back(node_brief(*node->second));
        }
      }
    }
    entry["concerns"] = std::move(links);
    items.push_back(std::move(entry));
  }
  return {{"checkpoints", std::move(items)}, {"total", total}, {"returned", items.size()}};
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
  auto params = request.value("params", nlohmann::json::object());
  if (!params.is_object()) {
    params = nlohmann::json::object();  // tolerate a null/absent params; ops read it with .value()
  }
  const auto graph = read_graph_snapshot(state);

  const auto known_op = daemon_op_from_string(op);
  if (!known_op) {
    return error_response("unknown op: " + op);
  }

  // Time the op at the dispatch boundary and record into op_stats. A query with
  // zero total matches is the "zero hit" signal that distinguishes useful work
  // from a daemon that is answering but finding nothing.
  double latency_ms = 0.0;
  bool zero_hit = false;
  nlohmann::json response;
  {
    ScopedTimer timer(&latency_ms);
    switch (*known_op) {
      case DaemonOp::Query: {
        auto result = query_graph(*graph, params);
        zero_hit = result.value("total", std::size_t{0}) == 0;
        response = ok_response(annotate_build_state(std::move(result), *graph));
        break;
      }
      case DaemonOp::Path:
        response = ok_response(annotate_build_state(shortest_path(*graph, params), *graph));
        break;
      case DaemonOp::Explain:
        response = ok_response(annotate_build_state(explain_node(*graph, params), *graph));
        break;
      case DaemonOp::Impact:
        response = ok_response(annotate_build_state(impact_radius(*graph, params), *graph));
        break;
      case DaemonOp::Context: {
        auto result = pack_context(*graph, params);
        // Zero-hit for context = the focal node did not resolve (the id/query
        // matched nothing). A resolved focus is useful context even if a tight
        // budget left no neighbors room, so focal-only is NOT a zero hit.
        zero_hit = result.value("focus", nlohmann::json()).is_null();
        response = ok_response(annotate_build_state(std::move(result), *graph));
        break;
      }
      case DaemonOp::Update:
        response = state.update_handler ? ok_response(state.update_handler(params))
                                        : ok_response({{"accepted", true}});
        break;
      case DaemonOp::Status:
        response = ok_response(status(state, *graph));
        break;
      case DaemonOp::Shutdown:
        state.shutdown_requested = true;
        response = ok_response({{"shutdown", true}});
        break;
      case DaemonOp::Remember:
        // remember_checkpoint returns a full ok/error envelope (it can reject a
        // disabled daemon, oversize body, or path escape), so it is not wrapped.
        response = remember_checkpoint(state, params);
        break;
      case DaemonOp::Recall: {
        auto result = recall_checkpoints(*graph, params);
        zero_hit = result.value("total", std::size_t{0}) == 0;
        response = ok_response(annotate_build_state(std::move(result), *graph));
        break;
      }
      case DaemonOp::Count:
        break;  // unreachable: daemon_op_from_string never yields Count
    }
  }
  // A context call served with gather="adaptive" is counted distinctly so the
  // durable ledger can report adaptive adoption (pre-flip telemetry).
  const bool adaptive_context_call =
      *known_op == DaemonOp::Context && params.value("gather", std::string{}) == "adaptive";
  state.op_stats.record(*known_op, latency_ms, zero_hit, adaptive_context_call);
  return response;
}

}  // namespace cgraph
