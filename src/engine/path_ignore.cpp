#include "cgraph/path_ignore.hpp"

#include <fstream>
#include <unordered_set>

namespace cgraph {

bool is_skipped_directory(std::string_view name) {
  static const std::unordered_set<std::string_view> skipped = {
      ".git",
      ".hg",
      ".svn",
      ".cache",
      ".idea",
      ".vscode",
      ".agents",
      "build",
      "cmake-build-debug",
      "cmake-build-release",
      "coverage",
      "dist",
      ".next",
      "node_modules",
      "target",
      "vendor",
      "cgraph-out",
      "graphify-out",
  };
  return skipped.contains(name);
}

std::vector<std::string> read_root_gitignore(const std::filesystem::path& root) {
  std::vector<std::string> patterns;
  std::ifstream input(root / ".gitignore");
  std::string line;
  while (std::getline(input, line)) {
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
      continue;
    }
    line.erase(0, first);
    if (line.empty() || line[0] == '#' || line[0] == '!') {
      continue;
    }
    if (!line.empty() && line.back() == '/') {
      line.pop_back();
    }
    if (!line.empty()) {
      patterns.push_back(line);
    }
  }
  return patterns;
}

bool matches_simple_gitignore(
    const std::filesystem::path& root,
    const std::filesystem::path& path,
    const std::vector<std::string>& patterns) {
  if (patterns.empty()) {
    return false;
  }

  const auto relative = std::filesystem::relative(path, root).generic_string();
  const auto filename = path.filename().generic_string();

  for (const auto& pattern : patterns) {
    if (pattern.empty()) {
      continue;
    }

    if (pattern.front() == '/') {
      if (relative == pattern.substr(1) || relative.starts_with(pattern.substr(1) + "/")) {
        return true;
      }
      continue;
    }

    if (pattern.find('/') == std::string::npos) {
      if (filename == pattern || relative == pattern || relative.starts_with(pattern + "/") ||
          relative.find("/" + pattern + "/") != std::string::npos) {
        return true;
      }
      continue;
    }

    if (relative == pattern || relative.starts_with(pattern + "/")) {
      return true;
    }
  }

  return false;
}

}  // namespace cgraph
