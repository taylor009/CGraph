#include "cgraph/semantic_cache.hpp"

#include "cgraph/file_cache.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph-semantic-cache-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const auto first_doc = root / "docs" / "first.md";
  const auto same_doc = root / "docs" / "same.md";
  const auto fragment = root / "graphify-out" / "semantic" / "chunk_01.json";
  write_file(first_doc, "# Notes\nShared content\n");
  write_file(same_doc, "# Notes\nShared content\n");
  write_file(fragment, "{\"nodes\":[],\"edges\":[]}\n");

  cgraph::SemanticCache cache;
  auto record = cgraph::make_semantic_cache_record(first_doc, fragment, cgraph::SemanticCacheState::Valid);
  const auto expected_hash = cgraph::sha256_hex("# Notes\nShared content\n");
  if (record.content_hash != expected_hash || record.source_path != first_doc || record.fragment_path != fragment) {
    return 1;
  }

  cache.upsert(std::move(record));
  const auto by_hash = cache.find_by_content_hash(expected_hash);
  if (!by_hash.has_value() || by_hash->state != cgraph::SemanticCacheState::Valid ||
      by_hash->source_path != first_doc || by_hash->fragment_path != fragment) {
    return 1;
  }

  const auto same_content = cache.find_for_file(same_doc);
  if (!same_content.has_value() || same_content->content_hash != expected_hash) {
    return 1;
  }

  write_file(same_doc, "# Notes\nChanged content\n");
  if (cache.find_for_file(same_doc).has_value()) {
    return 1;
  }

  const auto replacement_fragment = root / "graphify-out" / "semantic" / "chunk_02.json";
  write_file(replacement_fragment, "{\"nodes\":[{\"id\":\"x\"}],\"edges\":[]}\n");
  cache.upsert(cgraph::SemanticCacheRecord{
      .content_hash = expected_hash,
      .source_path = same_doc,
      .fragment_path = replacement_fragment,
      .state = cgraph::SemanticCacheState::Valid,
  });
  const auto replacement = cache.find_by_content_hash(expected_hash);
  if (!replacement.has_value() || replacement->source_path != same_doc ||
      replacement->fragment_path != replacement_fragment || cache.size() != 1) {
    return 1;
  }

  const auto cache_path = root / "graphify-out" / "semantic-cache.json";
  cgraph::write_semantic_cache(cache, cache_path);
  const auto loaded = cgraph::read_semantic_cache(cache_path);
  const auto loaded_record = loaded.find_by_content_hash(expected_hash);
  if (!loaded_record.has_value() || loaded_record->source_path != same_doc ||
      loaded_record->fragment_path != replacement_fragment || loaded.size() != 1) {
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
