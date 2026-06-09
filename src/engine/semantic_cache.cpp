#include "cgraph/semantic_cache.hpp"

#include "cgraph/file_cache.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <utility>

namespace cgraph {
namespace {

[[nodiscard]] std::string state_to_string(SemanticCacheState state) {
  switch (state) {
    case SemanticCacheState::Valid:
      return "valid";
    case SemanticCacheState::Stale:
      return "stale";
    case SemanticCacheState::Failed:
      return "failed";
  }
  return "failed";
}

[[nodiscard]] SemanticCacheState state_from_string(const std::string& state) {
  if (state == "valid") {
    return SemanticCacheState::Valid;
  }
  if (state == "stale") {
    return SemanticCacheState::Stale;
  }
  return SemanticCacheState::Failed;
}

[[nodiscard]] nlohmann::json record_to_json(const SemanticCacheRecord& record) {
  return {
      {"content_hash", record.content_hash},
      {"source_path", record.source_path.generic_string()},
      {"fragment_path", record.fragment_path.generic_string()},
      {"state", state_to_string(record.state)},
  };
}

[[nodiscard]] SemanticCacheRecord record_from_json(const nlohmann::json& value) {
  return SemanticCacheRecord{
      .content_hash = value.value("content_hash", std::string{}),
      .source_path = value.value("source_path", std::string{}),
      .fragment_path = value.value("fragment_path", std::string{}),
      .state = state_from_string(value.value("state", std::string{"failed"})),
  };
}

}  // namespace

void SemanticCache::upsert(SemanticCacheRecord record) {
  records_by_hash_[record.content_hash] = std::move(record);
}

std::optional<SemanticCacheRecord> SemanticCache::find_by_content_hash(std::string_view content_hash) const {
  const auto record = records_by_hash_.find(std::string{content_hash});
  if (record == records_by_hash_.end()) {
    return std::nullopt;
  }
  return record->second;
}

std::optional<SemanticCacheRecord> SemanticCache::find_for_file(const std::filesystem::path& path) const {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }
  return find_by_content_hash(sha256_file_hex(path));
}

std::vector<SemanticCacheRecord> SemanticCache::records() const {
  std::vector<SemanticCacheRecord> values;
  values.reserve(records_by_hash_.size());
  for (const auto& [_, record] : records_by_hash_) {
    values.push_back(record);
  }
  std::ranges::sort(values, [](const SemanticCacheRecord& lhs, const SemanticCacheRecord& rhs) {
    return lhs.content_hash < rhs.content_hash;
  });
  return values;
}

std::size_t SemanticCache::size() const {
  return records_by_hash_.size();
}

SemanticCacheRecord make_semantic_cache_record(
    const std::filesystem::path& source_path,
    const std::filesystem::path& fragment_path,
    SemanticCacheState state) {
  return SemanticCacheRecord{
      .content_hash = sha256_file_hex(source_path),
      .source_path = source_path,
      .fragment_path = fragment_path,
      .state = state,
  };
}

void write_semantic_cache(const SemanticCache& cache, const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  auto records = nlohmann::json::array();
  for (const auto& record : cache.records()) {
    records.push_back(record_to_json(record));
  }

  std::ofstream output(path, std::ios::binary);
  output << nlohmann::json{{"version", 1}, {"records", std::move(records)}}.dump(2);
}

SemanticCache read_semantic_cache(const std::filesystem::path& path) {
  SemanticCache cache;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return cache;
  }

  const auto root = nlohmann::json::parse(input, nullptr, false);
  if (!root.is_object() || !root.contains("records") || !root["records"].is_array()) {
    return cache;
  }
  for (const auto& item : root["records"]) {
    auto record = record_from_json(item);
    if (!record.content_hash.empty()) {
      cache.upsert(std::move(record));
    }
  }
  return cache;
}

bool invalidate_semantic_cache_for_file(SemanticCache& cache, const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return false;
  }

  const auto current_hash = sha256_file_hex(path);
  bool invalidated = false;
  for (auto record : cache.records()) {
    if (record.source_path != path || record.content_hash == current_hash || record.state == SemanticCacheState::Stale) {
      continue;
    }
    record.state = SemanticCacheState::Stale;
    cache.upsert(std::move(record));
    invalidated = true;
  }
  return invalidated;
}

}  // namespace cgraph
