#pragma once

#include "cgraph/types.hpp"

#include <filesystem>

namespace cgraph {

struct PipelineResult {
  GraphSnapshot graph;
  std::size_t file_count = 0;
  std::vector<std::string> warnings;
};

[[nodiscard]] PipelineResult run_one_shot(const std::filesystem::path& root);
void write_exports(const GraphSnapshot& graph, const std::filesystem::path& output_dir);

}  // namespace cgraph
