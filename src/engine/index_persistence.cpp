#include "cgraph/index_persistence.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace cgraph {
namespace {

// Logic version of the deterministic pipeline's persisted artifacts. A persisted
// graph.json + manifest may be fast-loaded only when this key matches the one
// stamped at write time, so it must change whenever a graph built by an older
// binary would be incompatible with this one. BUMP THE TRAILING NUMBER on any
// change to a parity surface: extraction/fragment shape, ID normalization
// (normalize.cpp), the dedup/merge contract, or the graph.json node-link output
// (export_json.cpp).
//
// This is deliberately a constant, NOT a hash of the running executable: keying
// on binary bytes invalidated every project's fast-load cache on any rebuild or
// re-sign (e.g. `codesign --force --sign -` after install), forcing a cold
// multi-minute rebuild on the next query even when extraction was untouched. The
// golden/parity tests catch an *unintended* output change; bumping this key on an
// *intended* one is an author/reviewer responsibility, like any on-disk schema
// version.
constexpr const char* kIndexVersionKey = "cgraph-index-v1:logic-1";

[[nodiscard]] std::string normalized_key(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::int64_t mtime_count(const std::filesystem::file_time_type& time) {
  return static_cast<std::int64_t>(time.time_since_epoch().count());
}

[[nodiscard]] std::filesystem::file_time_type mtime_from_count(std::int64_t count) {
  return std::filesystem::file_time_type(std::filesystem::file_time_type::duration(count));
}

}  // namespace

std::string index_version_key() {
  return kIndexVersionKey;
}

bool write_index_manifest(const IndexManifest& manifest, const std::filesystem::path& path) {
  auto files = nlohmann::json::array();
  for (const auto& entry : manifest.files) {
    files.push_back(nlohmann::json{
        {"path", entry.path.generic_string()},
        {"size", entry.size},
        {"modified_at", mtime_count(entry.modified_at)},
        {"sha256", entry.sha256},
    });
  }
  const nlohmann::json document{
      {"version_key", manifest.version_key},
      {"files", std::move(files)},
  };

  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    error.clear();
  }
  const auto temp_path = path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << document.dump();
    if (!output) {
      return false;
    }
  }
  std::filesystem::rename(temp_path, path, error);
  if (!error) {
    return true;
  }
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temp_path, path, error);
  return !error;
}

std::optional<IndexManifest> read_index_manifest(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  try {
    const auto document = nlohmann::json::parse(input);
    IndexManifest manifest;
    manifest.version_key = document.at("version_key").get<std::string>();
    for (const auto& entry : document.at("files")) {
      FileCacheEntry file;
      file.path = std::filesystem::path(entry.at("path").get<std::string>());
      file.size = entry.at("size").get<std::uintmax_t>();
      file.modified_at = mtime_from_count(entry.at("modified_at").get<std::int64_t>());
      file.sha256 = entry.at("sha256").get<std::string>();
      manifest.files.push_back(std::move(file));
    }
    return manifest;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

bool tree_matches_manifest(const IndexManifest& manifest, std::span<const DetectedFile> detected) {
  if (manifest.files.size() != detected.size()) {
    return false;  // a file was added or removed
  }
  std::unordered_map<std::string, const FileCacheEntry*> by_key;
  by_key.reserve(manifest.files.size());
  for (const auto& entry : manifest.files) {
    by_key.emplace(normalized_key(entry.path), &entry);
  }
  for (const auto& file : detected) {
    const auto found = by_key.find(normalized_key(file.path));
    if (found == by_key.end()) {
      return false;  // detected file absent from the manifest
    }
    const auto classification = classify_cached_file(file.path, *found->second);
    if (classification.state != CacheState::StatHit && classification.state != CacheState::HashHit) {
      return false;  // content changed (or vanished)
    }
  }
  return true;
}

}  // namespace cgraph
