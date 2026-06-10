#include "cgraph/index_persistence.hpp"

#include "cgraph/file_cache.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "cgraph_index_persistence_test";
  fs::remove_all(root);

  const auto a = root / "a.py";
  const auto b = root / "src" / "b.ts";
  write_file(a, "def f():\n    return 1\n");
  write_file(b, "export const x = 1;\n");

  // The version key is non-empty, carries the format prefix, and is stable.
  const auto key = cgraph::index_version_key();
  if (key.empty() || key.rfind("cgraph-index-v1:", 0) != 0) {
    fs::remove_all(root);
    return 1;
  }
  if (cgraph::index_version_key() != key) {
    fs::remove_all(root);
    return 1;
  }

  cgraph::IndexManifest manifest;
  manifest.version_key = key;
  manifest.files.push_back(cgraph::read_file_cache_entry(a));
  manifest.files.push_back(cgraph::read_file_cache_entry(b));

  // Round-trip: write then read yields an equal manifest.
  const auto manifest_path = root / "cgraph-out" / "index-manifest.json";
  if (!cgraph::write_index_manifest(manifest, manifest_path)) {
    fs::remove_all(root);
    return 1;
  }
  const auto loaded = cgraph::read_index_manifest(manifest_path);
  if (!loaded || loaded->version_key != key || loaded->files.size() != 2) {
    fs::remove_all(root);
    return 1;
  }
  for (std::size_t i = 0; i < manifest.files.size(); ++i) {
    const auto& lhs = manifest.files[i];
    const auto& rhs = loaded->files[i];
    if (lhs.path != rhs.path || lhs.size != rhs.size || lhs.modified_at != rhs.modified_at ||
        lhs.sha256 != rhs.sha256) {
      fs::remove_all(root);
      return 1;  // a field did not survive the round-trip (mtime is the risky one)
    }
  }

  const std::vector<cgraph::DetectedFile> unchanged = {
      {.path = a, .language = cgraph::DetectedLanguage::Python},
      {.path = b, .language = cgraph::DetectedLanguage::TypeScript},
  };

  // Unchanged tree matches.
  if (!cgraph::tree_matches_manifest(*loaded, unchanged)) {
    fs::remove_all(root);
    return 1;
  }

  // A removed file (manifest has 2, only 1 detected) does not match.
  const std::vector<cgraph::DetectedFile> removed = {
      {.path = a, .language = cgraph::DetectedLanguage::Python},
  };
  if (cgraph::tree_matches_manifest(*loaded, removed)) {
    fs::remove_all(root);
    return 1;
  }

  // An added file (3 detected vs 2 in manifest) does not match.
  const auto c = root / "c.py";
  write_file(c, "x = 2\n");
  std::vector<cgraph::DetectedFile> added = unchanged;
  added.push_back({.path = c, .language = cgraph::DetectedLanguage::Python});
  if (cgraph::tree_matches_manifest(*loaded, added)) {
    fs::remove_all(root);
    return 1;
  }

  // Changed content does not match. Sleep so the new mtime differs from the
  // cached one, forcing the hash comparison (and a Stale verdict).
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  write_file(a, "def f():\n    return 999\n");
  if (cgraph::tree_matches_manifest(*loaded, unchanged)) {
    fs::remove_all(root);
    return 1;
  }

  // A missing or corrupt manifest is "no usable cache", not a crash.
  if (cgraph::read_index_manifest(root / "nope.json").has_value()) {
    fs::remove_all(root);
    return 1;
  }
  write_file(root / "bad.json", "{ this is not json");
  if (cgraph::read_index_manifest(root / "bad.json").has_value()) {
    fs::remove_all(root);
    return 1;
  }

  fs::remove_all(root);
  return 0;
}
