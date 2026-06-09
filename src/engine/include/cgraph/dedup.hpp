#pragma once

#include "cgraph/types.hpp"

#include <span>
#include <string>

namespace cgraph {

struct DedupOptions {
  double entropy_floor = 2.5;
  double jaro_winkler_threshold = 0.92;
  double same_community_threshold = 0.88;
};

[[nodiscard]] double shannon_entropy(std::string_view value);
[[nodiscard]] double jaro_winkler_similarity(std::string_view lhs, std::string_view rhs);
void semantic_dedup(GraphSnapshot& graph, const DedupOptions& options = {});
void semantic_dedup_neighborhood(
    GraphSnapshot& graph,
    std::span<const std::string> changed_source_files,
    const DedupOptions& options = {});

}  // namespace cgraph
