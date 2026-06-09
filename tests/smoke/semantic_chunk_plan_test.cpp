#include "cgraph/semantic_chunk_plan.hpp"

#include "cgraph/semantic_cache.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

std::unordered_set<std::string> planned_paths(const cgraph::SemanticChunkPlan& plan) {
  std::unordered_set<std::string> paths;
  for (const auto& chunk : plan.chunks) {
    for (const auto& input : chunk.inputs) {
      paths.insert(input.path.generic_string());
    }
  }
  return paths;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-semantic-chunk-plan-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto cached_doc = root / "docs" / "cached.md";
  const auto changed_doc = root / "docs" / "changed.md";
  const auto new_doc = root / "docs" / "new.md";
  const auto media = root / "media" / "diagram.png";
  const auto code = root / "src" / "main.py";
  const auto ignored = root / "build" / "generated.md";
  const auto cached_fragment = root / "graphify-out" / "semantic" / "chunk_00.json";
  const auto changed_fragment = root / "graphify-out" / "semantic" / "chunk_old.json";

  write_file(cached_doc, "# Cached\nNo work needed\n");
  write_file(changed_doc, "# Changed\nBefore\n");
  write_file(new_doc, "# New\nNeeds work\n");
  write_file(media, "png bytes");
  write_file(code, "class CodeOnly:\n    pass\n");
  write_file(ignored, "# ignored\n");
  write_file(cached_fragment, "{\"nodes\":[],\"edges\":[]}\n");
  write_file(changed_fragment, "{\"nodes\":[],\"edges\":[]}\n");

  cgraph::SemanticCache cache;
  cache.upsert(cgraph::make_semantic_cache_record(cached_doc, cached_fragment));
  cache.upsert(cgraph::make_semantic_cache_record(changed_doc, changed_fragment));
  write_file(changed_doc, "# Changed\nAfter\n");

  const auto plan = cgraph::plan_semantic_chunks(
      root,
      cache,
      cgraph::SemanticChunkPlanOptions{.max_files_per_chunk = 2, .max_bytes_per_chunk = 1024});
  const auto paths = planned_paths(plan);
  if (plan.cache_hits != 1 || plan.chunks.size() != 2 || paths.size() != 3) {
    return 1;
  }
  if (paths.contains(cached_doc.generic_string()) || paths.contains(code.generic_string()) ||
      paths.contains(ignored.generic_string())) {
    return 1;
  }
  if (!paths.contains(changed_doc.generic_string()) || !paths.contains(new_doc.generic_string()) ||
      !paths.contains(media.generic_string())) {
    return 1;
  }
  for (const auto& chunk : plan.chunks) {
    if (chunk.inputs.empty() || chunk.inputs.size() > 2) {
      return 1;
    }
  }

  std::filesystem::remove_all(root);
  return 0;
}
