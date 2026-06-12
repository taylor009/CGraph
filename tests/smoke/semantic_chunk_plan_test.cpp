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
  // Canonicalize the root: the planner canonicalizes internally (to match the
  // code scanner and to resolve the /tmp -> /private/tmp symlink on macOS), so
  // expected paths must be built from the canonical root to compare equal.
  const auto root =
      std::filesystem::weakly_canonical(std::filesystem::temp_directory_path() / "cgraph-semantic-chunk-plan-test");
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto cached_doc = root / "docs" / "cached.md";
  const auto changed_doc = root / "docs" / "changed.md";
  const auto new_doc = root / "docs" / "new.md";
  const auto media = root / "media" / "diagram.png";
  const auto code = root / "src" / "main.py";
  const auto ignored = root / "build" / "generated.md";
  const auto gitignored = root / "vendored" / "toolchain.md";
  const auto cached_fragment = root / "graphify-out" / "semantic" / "chunk_00.json";
  const auto changed_fragment = root / "graphify-out" / "semantic" / "chunk_old.json";

  // A root .gitignore must be honored by the planner exactly as the code scanner
  // honors it — otherwise a vendored, git-ignored tree (this repo's own .vcpkg)
  // floods the plan with thousands of irrelevant docs. `build/` is already in the
  // always-skip set; `/vendored/` exercises a gitignore-only exclusion.
  write_file(root / ".gitignore", "/vendored/\n");

  write_file(cached_doc, "# Cached\nNo work needed\n");
  write_file(changed_doc, "# Changed\nBefore\n");
  write_file(new_doc, "# New\nNeeds work\n");
  write_file(media, "png bytes");
  write_file(code, "class CodeOnly:\n    pass\n");
  write_file(ignored, "# ignored\n");
  write_file(gitignored, "# vendored doc, must be skipped via .gitignore\n");
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
      paths.contains(ignored.generic_string()) || paths.contains(gitignored.generic_string())) {
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

  // --- Stat cache: unchanged files are not re-hashed across plans ------------
  // Plan over a clean tree (empty cache, fresh index): every file is hashed.
  const auto stat_root = std::filesystem::weakly_canonical(
      std::filesystem::temp_directory_path() / "cgraph-semantic-statcache-test");
  std::filesystem::remove_all(stat_root);
  write_file(stat_root / "a.md", "# A\n");
  write_file(stat_root / "b.md", "# B\n");
  write_file(stat_root / "c.md", "# C\n");

  cgraph::SemanticCache empty_cache;
  cgraph::SemanticStatIndex index;
  const auto cold = cgraph::plan_semantic_chunks(stat_root, empty_cache, {}, &index);
  if (cold.files_hashed != 3 || cold.files_stat_reused != 0 || index.size() != 3) {
    return 2;  // cold plan hashes everything
  }

  // Second plan, nothing changed: all three reuse their stored hash, none read.
  const auto warm = cgraph::plan_semantic_chunks(stat_root, empty_cache, {}, &index);
  if (warm.files_hashed != 0 || warm.files_stat_reused != 3) {
    return 3;  // unchanged files must not be re-hashed
  }
  // Plan output identical whether hashed or reused.
  if (planned_paths(warm) != planned_paths(cold) || warm.chunks.size() != cold.chunks.size() ||
      warm.cache_hits != cold.cache_hits || warm.stale_inputs != cold.stale_inputs) {
    return 4;
  }

  // Change one file's content (bumping size/mtime): only it is re-hashed.
  write_file(stat_root / "b.md", "# B changed, now longer\n");
  const auto after_edit = cgraph::plan_semantic_chunks(stat_root, empty_cache, {}, &index);
  if (after_edit.files_hashed != 1 || after_edit.files_stat_reused != 2) {
    return 5;
  }

  // --- Persistence: index round-trips and a reloaded index reuses all --------
  const auto index_path = stat_root / "stat-index.json";
  cgraph::write_semantic_stat_index(index, index_path);
  auto reloaded = cgraph::read_semantic_stat_index(index_path);
  if (reloaded.size() != index.size()) {
    return 6;
  }
  const auto after_reload = cgraph::plan_semantic_chunks(stat_root, empty_cache, {}, &reloaded);
  if (after_reload.files_hashed != 0 || after_reload.files_stat_reused != 3) {
    return 7;  // a restart over an unchanged tree re-hashes nothing
  }

  // Absent index file is treated as cold (no error), hashing everything.
  auto absent = cgraph::read_semantic_stat_index(stat_root / "does-not-exist.json");
  const auto cold_again = cgraph::plan_semantic_chunks(stat_root, empty_cache, {}, &absent);
  if (cold_again.files_hashed != 3 || cold_again.files_stat_reused != 0) {
    return 8;
  }

  std::filesystem::remove_all(stat_root);
  std::filesystem::remove_all(root);
  return 0;
}
