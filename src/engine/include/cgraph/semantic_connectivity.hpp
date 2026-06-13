#pragma once

#include "cgraph/types.hpp"

#include <cstddef>

namespace cgraph {

// Structural connectivity of the host-authored semantic layer to the code graph.
// Answers "is enrichment landing connected, or are docs floating islands?" —
// purely from node ids and edges, no judgement of semantic correctness.
struct SemanticConnectivity {
  std::size_t doc_nodes = 0;        // ids prefixed doc:
  std::size_t concept_nodes = 0;    // ids prefixed concept: or topic:
  std::size_t connected_docs = 0;   // doc nodes that reach a code node within hop_bound
  std::size_t orphan_docs = 0;      // doc_nodes - connected_docs
  std::size_t orphan_concepts = 0;  // concept nodes with no edge to a code node
  std::size_t doc_code_edges = 0;   // edges from a semantic node into a code node
  double connectivity_rate = 0.0;   // connected_docs / doc_nodes (0 when no docs)
};

// A node is semantic iff its id is namespaced doc:/concept:/topic: (the
// cgraph-enrich contract); everything else is a code node. A document counts as
// connected when it reaches a code node within `hop_bound` edges, so a doc
// linked to code through a concept (doc -> concept -> code) still counts.
[[nodiscard]] SemanticConnectivity compute_semantic_connectivity(
    const GraphSnapshot& graph,
    std::size_t hop_bound = 2);

}  // namespace cgraph
