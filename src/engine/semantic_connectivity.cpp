#include "cgraph/semantic_connectivity.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cgraph {
namespace {

[[nodiscard]] bool is_doc(std::string_view id) { return id.starts_with("doc:"); }
[[nodiscard]] bool is_concept(std::string_view id) {
  return id.starts_with("concept:") || id.starts_with("topic:");
}
[[nodiscard]] bool is_semantic(std::string_view id) { return is_doc(id) || is_concept(id); }

}  // namespace

SemanticConnectivity compute_semantic_connectivity(const GraphSnapshot& graph, std::size_t hop_bound) {
  SemanticConnectivity result;

  // Undirected adjacency over the edge set, used for reachability.
  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  for (const auto& edge : graph.edges) {
    adjacency[edge.source].push_back(edge.target);
    adjacency[edge.target].push_back(edge.source);
    // A semantic node bridging directly into a code node.
    if (is_semantic(edge.source) && !is_semantic(edge.target)) {
      ++result.doc_code_edges;
    }
  }

  // Does `start` reach any code node within hop_bound edges?
  const auto reaches_code = [&](const std::string& start) {
    std::unordered_set<std::string> visited{start};
    std::vector<std::string> frontier{start};
    for (std::size_t hop = 0; hop < hop_bound && !frontier.empty(); ++hop) {
      std::vector<std::string> next;
      for (const auto& node : frontier) {
        const auto neighbors = adjacency.find(node);
        if (neighbors == adjacency.end()) {
          continue;
        }
        for (const auto& neighbor : neighbors->second) {
          if (!is_semantic(neighbor)) {
            return true;  // reached a code node
          }
          if (visited.insert(neighbor).second) {
            next.push_back(neighbor);
          }
        }
      }
      frontier = std::move(next);
    }
    return false;
  };

  // A concept is an orphan when no edge connects it to a code node (direct).
  const auto concept_touches_code = [&](const std::string& id) {
    const auto neighbors = adjacency.find(id);
    if (neighbors == adjacency.end()) {
      return false;
    }
    for (const auto& neighbor : neighbors->second) {
      if (!is_semantic(neighbor)) {
        return true;
      }
    }
    return false;
  };

  for (const auto& node : graph.nodes) {
    if (is_doc(node.id)) {
      ++result.doc_nodes;
      if (reaches_code(node.id)) {
        ++result.connected_docs;
      }
    } else if (is_concept(node.id)) {
      ++result.concept_nodes;
      if (!concept_touches_code(node.id)) {
        ++result.orphan_concepts;
      }
    }
  }

  result.orphan_docs = result.doc_nodes - result.connected_docs;
  result.connectivity_rate =
      result.doc_nodes == 0 ? 0.0
                            : static_cast<double>(result.connected_docs) / static_cast<double>(result.doc_nodes);
  return result;
}

}  // namespace cgraph
