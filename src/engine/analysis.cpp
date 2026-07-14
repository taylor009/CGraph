#include "cgraph/analysis.hpp"

#include <igraph/igraph.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cgraph {
namespace {

struct IGraphDeleter {
  void operator()(igraph_t* graph) const noexcept {
    if (graph != nullptr) {
      igraph_destroy(graph);
      delete graph;
    }
  }
};

struct VectorIntDeleter {
  void operator()(igraph_vector_int_t* vector) const noexcept {
    if (vector != nullptr) {
      igraph_vector_int_destroy(vector);
      delete vector;
    }
  }
};

using IGraphPtr = std::unique_ptr<igraph_t, IGraphDeleter>;
using VectorIntPtr = std::unique_ptr<igraph_vector_int_t, VectorIntDeleter>;

[[nodiscard]] std::unordered_map<std::string, igraph_int_t> node_indices(const GraphSnapshot& snapshot) {
  std::unordered_map<std::string, igraph_int_t> indices;
  for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
    indices.emplace(snapshot.nodes[index].id, static_cast<igraph_int_t>(index));
  }
  return indices;
}

[[nodiscard]] igraph_vector_int_t edge_vector(const GraphSnapshot& snapshot, const std::unordered_map<std::string, igraph_int_t>& indices) {
  igraph_vector_int_t edges;
  igraph_vector_int_init(&edges, 0);
  for (const auto& edge : snapshot.edges) {
    const auto source = indices.find(edge.source);
    const auto target = indices.find(edge.target);
    if (source == indices.end() || target == indices.end()) {
      continue;
    }
    igraph_vector_int_push_back(&edges, source->second);
    igraph_vector_int_push_back(&edges, target->second);
  }
  return edges;
}

[[nodiscard]] IGraphPtr make_igraph(const GraphSnapshot& snapshot) {
  const auto indices = node_indices(snapshot);
  auto edges = edge_vector(snapshot, indices);

  auto graph = IGraphPtr(new igraph_t{});
  const auto error = igraph_create(
      graph.get(),
      &edges,
      static_cast<igraph_int_t>(snapshot.nodes.size()),
      IGRAPH_UNDIRECTED);
  igraph_vector_int_destroy(&edges);

  if (error != IGRAPH_SUCCESS) {
    return nullptr;
  }
  return graph;
}

[[nodiscard]] VectorIntPtr make_membership(std::size_t size) {
  auto membership = VectorIntPtr(new igraph_vector_int_t{});
  if (igraph_vector_int_init(membership.get(), static_cast<igraph_int_t>(size)) != IGRAPH_SUCCESS) {
    return nullptr;
  }
  return membership;
}

void write_membership(GraphSnapshot& snapshot, const igraph_vector_int_t& membership) {
  for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
    snapshot.nodes[index].properties["community"] =
        std::to_string(static_cast<long long>(VECTOR(membership)[static_cast<igraph_int_t>(index)]));
  }
}

}  // namespace

CommunityResult detect_communities(GraphSnapshot& graph) {
  CommunityResult result;
  if (graph.nodes.empty()) {
    return result;
  }

  auto igraph = make_igraph(graph);
  auto membership = make_membership(graph.nodes.size());
  if (igraph == nullptr || membership == nullptr) {
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
      graph.nodes[index].properties["community"] = std::to_string(index);
    }
    result.cluster_count = static_cast<int>(graph.nodes.size());
    return result;
  }

  igraph_int_t cluster_count = 0;
  igraph_real_t quality = 0.0;
  auto error = igraph_community_leiden_simple(
      igraph.get(),
      nullptr,
      IGRAPH_LEIDEN_OBJECTIVE_MODULARITY,
      1.0,
      0.01,
      false,
      2,
      membership.get(),
      &cluster_count,
      &quality);

  if (error == IGRAPH_SUCCESS) {
    result.used_leiden = true;
  } else {
    error = igraph_community_multilevel(igraph.get(), nullptr, 1.0, membership.get(), nullptr, nullptr);
    if (error != IGRAPH_SUCCESS) {
      for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        VECTOR(*membership)[static_cast<igraph_int_t>(index)] = static_cast<igraph_int_t>(index);
      }
    }
    std::vector<igraph_int_t> communities;
    communities.reserve(graph.nodes.size());
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
      communities.push_back(VECTOR(*membership)[static_cast<igraph_int_t>(index)]);
    }
    std::ranges::sort(communities);
    result.cluster_count = static_cast<int>(std::ranges::unique(communities).begin() - communities.begin());
  }

  if (result.used_leiden) {
    result.cluster_count = static_cast<int>(cluster_count);
    result.quality = static_cast<double>(quality);
  }
  write_membership(graph, *membership);
  return result;
}

void analyze_graph(GraphSnapshot& graph) {
  if (graph.nodes.empty()) {
    return;
  }

  std::unordered_map<std::string, std::size_t> node_index;
  for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
    node_index.emplace(graph.nodes[index].id, index);
  }

  std::vector<double> degree(graph.nodes.size(), 0.0);
  for (auto& edge : graph.edges) {
    // Session-memory edges (e.g. a checkpoint's `concerns` edge to a code node)
    // must not inflate code-node degree, or memory would shift code centrality.
    if (is_memory_node_id(edge.source) || is_memory_node_id(edge.target)) {
      continue;
    }
    const auto source = node_index.find(edge.source);
    const auto target = node_index.find(edge.target);
    if (source == node_index.end() || target == node_index.end()) {
      continue;
    }

    degree[source->second] += 1.0;
    degree[target->second] += 1.0;

    const auto source_community = graph.nodes[source->second].properties.find("community");
    const auto target_community = graph.nodes[target->second].properties.find("community");
    if (source_community != graph.nodes[source->second].properties.end() &&
        target_community != graph.nodes[target->second].properties.end() &&
        source_community->second != target_community->second) {
      edge.properties["cross_community"] = "true";
      edge.properties["surprise"] = "cross-community";
    }
  }

  const auto max_degree = *std::ranges::max_element(degree);
  std::vector<std::size_t> rank_order(graph.nodes.size());
  std::iota(rank_order.begin(), rank_order.end(), 0);
  std::ranges::sort(rank_order, [&degree](std::size_t lhs, std::size_t rhs) {
    if (degree[lhs] == degree[rhs]) {
      return lhs < rhs;
    }
    return degree[lhs] > degree[rhs];
  });

  for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
    // Memory nodes are inert to code analysis: they receive no centrality, so
    // they never appear in centrality-ranked retrieval or alter the scale.
    if (is_memory_node_id(graph.nodes[index].id)) {
      continue;
    }
    const auto centrality = max_degree > 0.0 ? degree[index] / max_degree : 0.0;
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << centrality;
    graph.nodes[index].properties["degree_centrality"] = value.str();
  }

  const auto limit = std::min<std::size_t>(10, rank_order.size());
  for (std::size_t rank = 0; rank < limit; ++rank) {
    auto& node = graph.nodes[rank_order[rank]];
    if (is_memory_node_id(node.id)) {
      continue;  // a memory node is never a god node
    }
    node.properties["god_node_rank"] = std::to_string(rank + 1);
    if (rank < 3) {
      node.properties["god_node"] = "true";
    }
  }
}

}  // namespace cgraph
