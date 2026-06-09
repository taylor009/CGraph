#pragma once

#include "cgraph/types.hpp"

namespace cgraph {

struct CommunityResult {
  bool used_leiden = false;
  int cluster_count = 0;
  double quality = 0.0;
};

[[nodiscard]] CommunityResult detect_communities(GraphSnapshot& graph);
void analyze_graph(GraphSnapshot& graph);

}  // namespace cgraph
