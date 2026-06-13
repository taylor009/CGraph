#pragma once

#include "cgraph/types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cgraph {

// A real code-node id (and its label, as evidence) that a document mentions.
// The plan hands these to the authoring host so a doc->code edge is the easy
// path; cgraph never assigns the relation.
struct CandidateLink {
  std::string node_id;
  std::string label;
};

// One symbol name -> the code nodes that share it, plus whether any of them is a
// type-like kind (so a capitalized single-word mention like `Fragment` counts).
struct SymbolEntry {
  std::vector<CandidateLink> nodes;
  bool type_like = false;
};

struct SymbolIndex {
  std::unordered_map<std::string, SymbolEntry> by_name;
};

// Build name -> nodes from a code graph's node labels (leading identifier of each
// label). Pure; no I/O.
[[nodiscard]] SymbolIndex build_symbol_index(const GraphSnapshot& graph);

// Scan document text for mentions of indexed symbols and return high-precision
// candidate links: compound identifiers (snake_case / internal CamelCase) and
// capitalized type names are kept; bare lowercase word collisions are dropped.
// Candidates are ranked by specificity (rarer names first) and capped.
[[nodiscard]] std::vector<CandidateLink> compute_candidate_links(
    std::string_view doc_text,
    const SymbolIndex& index,
    std::size_t max_links = 10);

}  // namespace cgraph
