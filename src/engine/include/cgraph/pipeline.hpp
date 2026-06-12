#pragma once

#include "cgraph/operation_stats.hpp"
#include "cgraph/types.hpp"

#include <filesystem>

namespace cgraph {

struct PipelineResult {
  GraphSnapshot graph;
  std::size_t file_count = 0;
  std::vector<std::string> warnings;
  BuildStats stats;
};

[[nodiscard]] PipelineResult run_one_shot(const std::filesystem::path& root);
void write_exports(const GraphSnapshot& graph, const std::filesystem::path& output_dir);

}  // namespace cgraph
