#include "cgraph/file_cache.hpp"
#include "cgraph/content_root.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-file-cache-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto path = root / "src" / "main.py";

  if (cgraph::sha256_hex("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
    return 1;
  }

  write_file(path, "print('hello')\n");
  const auto fresh = cgraph::classify_cached_file(path, std::nullopt);
  if (fresh.state != cgraph::CacheState::New || !fresh.hash_computed || !fresh.current.has_value()) {
    return 1;
  }

  const auto stat_hit = cgraph::classify_cached_file(path, fresh.current);
  if (stat_hit.state != cgraph::CacheState::StatHit || stat_hit.hash_computed ||
      stat_hit.current->sha256 != fresh.current->sha256) {
    return 1;
  }

  auto stat_mismatch_same_content = *fresh.current;
  stat_mismatch_same_content.size += 1;
  const auto hash_hit = cgraph::classify_cached_file(path, stat_mismatch_same_content);
  if (hash_hit.state != cgraph::CacheState::HashHit || !hash_hit.hash_computed ||
      hash_hit.current->sha256 != fresh.current->sha256) {
    return 1;
  }

  write_file(path, "print('changed')\n");
  const auto stale = cgraph::classify_cached_file(path, fresh.current);
  if (stale.state != cgraph::CacheState::Stale || !stale.hash_computed ||
      stale.current->sha256 == fresh.current->sha256) {
    return 1;
  }

  std::filesystem::remove(path);
  const auto deleted = cgraph::classify_cached_file(path, stale.current);
  if (deleted.state != cgraph::CacheState::Deleted || deleted.hash_computed || deleted.current.has_value()) {
    return 1;
  }

  // === Content validation mode ===

  write_file(path, "print('hello')\n");
  const auto content_fresh = cgraph::classify_cached_file(path, std::nullopt, cgraph::CacheValidation::Content);
  if (content_fresh.state != cgraph::CacheState::New || !content_fresh.hash_computed ||
      !content_fresh.current.has_value()) {
    return 1;
  }

  // Content mode must NEVER return StatHit — it always hashes
  const auto content_same = cgraph::classify_cached_file(
      path, content_fresh.current, cgraph::CacheValidation::Content);
  if (content_same.state != cgraph::CacheState::HashHit || !content_same.hash_computed) {
    return 1;
  }

  // Preserved-mtime rewrite: same size, restored mtime, different content
  const auto original_mtime = std::filesystem::last_write_time(path);
  write_file(path, "print('world')\n");
  std::filesystem::last_write_time(path, original_mtime);

  // Metadata mode: fooled — returns StatHit
  const auto meta_fooled = cgraph::classify_cached_file(
      path, content_fresh.current, cgraph::CacheValidation::Metadata);
  if (meta_fooled.state != cgraph::CacheState::StatHit) {
    return 1;
  }

  // Content mode: detects the stale content
  const auto content_caught = cgraph::classify_cached_file(
      path, content_fresh.current, cgraph::CacheValidation::Content);
  if (content_caught.state != cgraph::CacheState::Stale || !content_caught.hash_computed) {
    return 1;
  }

  // === Merkle root: empty tree ===

  const auto empty_root = cgraph::compute_content_root(root, std::span<const cgraph::FileCacheEntry>{});
  if (empty_root.algorithm != cgraph::kContentRootAlgorithm || empty_root.leaf_count != 0 ||
      !cgraph::is_valid_content_root(empty_root)) {
    return 1;
  }

  // Empty root is stable (same value every time)
  const auto empty_root2 = cgraph::compute_content_root(root, std::span<const cgraph::FileCacheEntry>{});
  if (empty_root.sha256 != empty_root2.sha256 || empty_root.sha256.size() != 64) {
    return 1;
  }

  // === Merkle root: deterministic across input ordering ===

  const auto file_a = root / "a.py";
  const auto file_b = root / "b.py";
  const auto file_c = root / "c.py";
  write_file(file_a, "a = 1\n");
  write_file(file_b, "b = 2\n");
  write_file(file_c, "c = 3\n");
  const auto entry_a = cgraph::read_file_cache_entry(file_a);
  const auto entry_b = cgraph::read_file_cache_entry(file_b);
  const auto entry_c = cgraph::read_file_cache_entry(file_c);
  std::vector<cgraph::FileCacheEntry> abc = {entry_a, entry_b, entry_c};
  std::vector<cgraph::FileCacheEntry> cba = {entry_c, entry_b, entry_a};
  std::vector<cgraph::FileCacheEntry> bca = {entry_b, entry_c, entry_a};
  const auto root_abc = cgraph::compute_content_root(root, abc);
  const auto root_cba = cgraph::compute_content_root(root, cba);
  const auto root_bca = cgraph::compute_content_root(root, bca);
  if (root_abc.sha256 != root_cba.sha256 || root_abc.sha256 != root_bca.sha256) {
    return 1;
  }
  if (root_abc.leaf_count != 3) {
    return 1;
  }

  // === Merkle root: changes on content change ===

  write_file(file_a, "a = 9\n");
  const auto entry_a_changed = cgraph::read_file_cache_entry(file_a);
  std::vector<cgraph::FileCacheEntry> changed_content = {entry_a_changed, entry_b, entry_c};
  const auto root_changed = cgraph::compute_content_root(root, changed_content);
  if (root_changed.sha256 == root_abc.sha256) {
    return 1;
  }

  // === Merkle root: changes on path change (same bytes) ===

  write_file(file_a, "a = 1\n");
  const auto entry_a_restored = cgraph::read_file_cache_entry(file_a);
  const auto file_d = root / "d.py";
  write_file(file_d, "a = 1\n");
  const auto entry_d = cgraph::read_file_cache_entry(file_d);
  if (entry_a_restored.sha256 != entry_d.sha256) {
    return 1;
  }
  std::vector<cgraph::FileCacheEntry> with_a = {entry_a_restored};
  std::vector<cgraph::FileCacheEntry> with_d = {entry_d};
  const auto root_with_a = cgraph::compute_content_root(root, with_a);
  const auto root_with_d = cgraph::compute_content_root(root, with_d);
  if (root_with_a.sha256 == root_with_d.sha256) {
    return 1;
  }

  // === Merkle root: changes on add / delete ===

  std::vector<cgraph::FileCacheEntry> two = {entry_a_restored, entry_b};
  const auto root_two = cgraph::compute_content_root(root, two);
  std::vector<cgraph::FileCacheEntry> three = {entry_a_restored, entry_b, entry_c};
  const auto root_three = cgraph::compute_content_root(root, three);
  if (root_two.sha256 == root_three.sha256) {
    return 1;
  }
  if (root_two.leaf_count != 2 || root_three.leaf_count != 3) {
    return 1;
  }

  auto malformed_entry = entry_a_restored;
  malformed_entry.sha256 = "not-a-sha256";
  try {
    (void)cgraph::compute_content_root(root, std::span<const cgraph::FileCacheEntry>{&malformed_entry, 1});
    return 1;
  } catch (const std::invalid_argument&) {
  }

  auto outside_entry = entry_a_restored;
  outside_entry.path = root.parent_path() / "outside.py";
  try {
    (void)cgraph::compute_content_root(root, std::span<const cgraph::FileCacheEntry>{&outside_entry, 1});
    return 1;
  } catch (const std::invalid_argument&) {
  }

  std::filesystem::remove_all(root);
  return 0;
}
