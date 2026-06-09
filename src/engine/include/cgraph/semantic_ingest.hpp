#pragma once

#include "cgraph/daemon_ops.hpp"
#include "cgraph/semantic_cache.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cgraph {

struct SemanticIngestResult {
  bool merged = false;
  bool cache_updated = false;
  std::vector<std::string> errors;
};

[[nodiscard]] SemanticIngestResult ingest_semantic_fragment(
    DaemonState& state,
    SemanticCache& cache,
    const std::filesystem::path& source_path,
    const std::filesystem::path& fragment_path);

}  // namespace cgraph
