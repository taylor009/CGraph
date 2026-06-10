#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/file_cache.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cgraph {

// A version-stamped snapshot of the file set a persisted graph.json was built
// from. On daemon restart, if the version key still matches the running binary
// and every detected file is a stat/hash hit against this manifest (no files
// added or removed), the graph can be served straight from disk without
// re-extracting anything (the Tier-1 fast path).
struct IndexManifest {
  std::string version_key;
  std::vector<FileCacheEntry> files;
};

// Content-addressed key over the running executable, so any recompile of the
// extractor (or anything else) invalidates a persisted cache even when the
// source tree is byte-identical. Stable across calls within one process.
[[nodiscard]] std::string index_version_key();

// Atomic write (temp + rename). Returns false if the file could not be written.
[[nodiscard]] bool write_index_manifest(const IndexManifest& manifest, const std::filesystem::path& path);

// Returns nullopt if the manifest is absent or unparseable (treated as "no
// usable cache", never a half-loaded manifest).
[[nodiscard]] std::optional<IndexManifest> read_index_manifest(const std::filesystem::path& path);

// True iff the detected file set is identical to the manifest's (same count, no
// additions or removals) and every file is a StatHit or HashHit against its
// manifest entry. Does NOT check the version key — the caller compares that
// against index_version_key() first.
[[nodiscard]] bool tree_matches_manifest(const IndexManifest& manifest, std::span<const DetectedFile> detected);

}  // namespace cgraph
