#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace cgraph {

struct FileCacheEntry;

inline constexpr std::string_view kContentRootAlgorithm = "sha256-merkle-v1";

struct ContentRoot {
  std::string algorithm;
  std::string sha256;
  std::size_t leaf_count = 0;
};

[[nodiscard]] ContentRoot compute_content_root(
    const std::filesystem::path& project_root,
    std::span<const FileCacheEntry> entries);
[[nodiscard]] bool is_valid_content_root(const ContentRoot& root);

}  // namespace cgraph
