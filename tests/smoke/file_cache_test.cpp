#include "cgraph/file_cache.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

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

  std::filesystem::remove_all(root);
  return 0;
}
