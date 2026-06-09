#pragma once

#include "cgraph/semantic_cache.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cgraph {

enum class SemanticInputKind {
  Document,
  Media,
};

struct SemanticInput {
  std::filesystem::path path;
  SemanticInputKind kind = SemanticInputKind::Document;
  std::string content_hash;
  std::uintmax_t size = 0;
};

struct SemanticChunk {
  std::size_t index = 0;
  std::vector<SemanticInput> inputs;
};

struct SemanticChunkPlan {
  std::vector<SemanticChunk> chunks;
  std::size_t cache_hits = 0;
  std::size_t stale_inputs = 0;
};

struct SemanticChunkPlanOptions {
  std::size_t max_files_per_chunk = 8;
  std::uintmax_t max_bytes_per_chunk = 1024 * 1024;
};

[[nodiscard]] SemanticChunkPlan plan_semantic_chunks(
    const std::filesystem::path& root,
    const SemanticCache& cache,
    SemanticChunkPlanOptions options = {});

}  // namespace cgraph
