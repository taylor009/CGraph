#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cgraph {

enum class SemanticCacheState {
  Valid,
  Stale,
  Failed,
};

struct SemanticCacheRecord {
  std::string content_hash;
  std::filesystem::path source_path;
  std::filesystem::path fragment_path;
  SemanticCacheState state = SemanticCacheState::Valid;
};

class SemanticCache {
 public:
  void upsert(SemanticCacheRecord record);
  [[nodiscard]] std::optional<SemanticCacheRecord> find_by_content_hash(std::string_view content_hash) const;
  [[nodiscard]] std::optional<SemanticCacheRecord> find_for_file(const std::filesystem::path& path) const;
  [[nodiscard]] std::vector<SemanticCacheRecord> records() const;
  [[nodiscard]] std::size_t size() const;

 private:
  std::unordered_map<std::string, SemanticCacheRecord> records_by_hash_;
};

[[nodiscard]] SemanticCacheRecord make_semantic_cache_record(
    const std::filesystem::path& source_path,
    const std::filesystem::path& fragment_path,
    SemanticCacheState state = SemanticCacheState::Valid);

void write_semantic_cache(const SemanticCache& cache, const std::filesystem::path& path);
[[nodiscard]] SemanticCache read_semantic_cache(const std::filesystem::path& path);
bool invalidate_semantic_cache_for_file(SemanticCache& cache, const std::filesystem::path& path);

}  // namespace cgraph
