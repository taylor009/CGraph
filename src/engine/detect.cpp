#include "cgraph/detect.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cgraph {
namespace {

[[nodiscard]] std::string lower_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
      return static_cast<char>(ch - 'A' + 'a');
    }
    return static_cast<char>(ch);
  });
  return value;
}

[[nodiscard]] bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool is_mcp_config_name(std::string_view filename) {
  return filename == "mcp.json" || filename == ".mcp.json" || filename == "mcp_config.json" ||
         filename == "mcp-server.json" || filename == "mcp-servers.json";
}

[[nodiscard]] bool is_msbuild_name(std::string_view filename) {
  return ends_with(filename, ".csproj") || ends_with(filename, ".vbproj") ||
         ends_with(filename, ".fsproj") || ends_with(filename, ".vcxproj") ||
         ends_with(filename, ".props") || ends_with(filename, ".targets");
}

[[nodiscard]] bool is_skipped_directory(std::string_view name) {
  static const std::unordered_set<std::string_view> skipped = {
      ".git",
      ".hg",
      ".svn",
      ".cache",
      ".idea",
      ".vscode",
      "build",
      "cmake-build-debug",
      "cmake-build-release",
      "dist",
      "node_modules",
      "target",
      "vendor",
      "graphify-out",
  };
  return skipped.contains(name);
}

[[nodiscard]] std::vector<std::string> read_root_gitignore(const std::filesystem::path& root) {
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

[[nodiscard]] bool matches_simple_gitignore(
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

}  // namespace

DetectedLanguage detect_language(const std::filesystem::path& path) {
  const auto filename = lower_ascii(path.filename().generic_string());
  if (ends_with(filename, ".blade.php")) {
    return DetectedLanguage::PhpBlade;
  }
  if (is_mcp_config_name(filename)) {
    return DetectedLanguage::McpConfig;
  }
  if (is_msbuild_name(filename)) {
    return DetectedLanguage::MsBuild;
  }

  const auto extension = lower_ascii(path.extension().generic_string());
  if (extension == ".c" || extension == ".h") {
    return DetectedLanguage::C;
  }
  if (extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".hpp" ||
      extension == ".hh" || extension == ".hxx") {
    return DetectedLanguage::Cpp;
  }
  if (extension == ".cs") {
    return DetectedLanguage::CSharp;
  }
  if (extension == ".cls" || extension == ".trigger") {
    return DetectedLanguage::Apex;
  }
  if (extension == ".go") {
    return DetectedLanguage::Go;
  }
  if (extension == ".groovy" || extension == ".gvy" || extension == ".gradle") {
    return DetectedLanguage::Groovy;
  }
  if (extension == ".java") {
    return DetectedLanguage::Java;
  }
  if (extension == ".js" || extension == ".jsx" || extension == ".mjs" || extension == ".cjs") {
    return DetectedLanguage::JavaScript;
  }
  if (extension == ".kt" || extension == ".kts") {
    return DetectedLanguage::Kotlin;
  }
  if (extension == ".pas" || extension == ".pp" || extension == ".dpr" || extension == ".lfm" ||
      extension == ".dfm") {
    return DetectedLanguage::Delphi;
  }
  if (extension == ".py" || extension == ".pyw") {
    return DetectedLanguage::Python;
  }
  if (extension == ".rb") {
    return DetectedLanguage::Ruby;
  }
  if (extension == ".scala" || extension == ".sc") {
    return DetectedLanguage::Scala;
  }
  if (extension == ".ts") {
    return DetectedLanguage::TypeScript;
  }
  if (extension == ".tsx") {
    return DetectedLanguage::Tsx;
  }
  if (extension == ".xml") {
    return DetectedLanguage::Xml;
  }
  return DetectedLanguage::Unknown;
}

std::vector<DetectedFile> detect_project_files(const std::filesystem::path& root) {
  std::vector<DetectedFile> files;
  const auto canonical_root = std::filesystem::weakly_canonical(root);
  const auto gitignore_patterns = read_root_gitignore(canonical_root);

  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      canonical_root,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;

  for (; !error && iterator != end; iterator.increment(error)) {
    const auto& entry = *iterator;
    const auto name = entry.path().filename().generic_string();

    if (entry.is_directory(error)) {
      if (is_skipped_directory(name) || matches_simple_gitignore(canonical_root, entry.path(), gitignore_patterns)) {
        iterator.disable_recursion_pending();
      }
      continue;
    }

    if (!entry.is_regular_file(error)) {
      continue;
    }

    if (matches_simple_gitignore(canonical_root, entry.path(), gitignore_patterns)) {
      continue;
    }

    const auto language = detect_language(entry.path());
    if (language == DetectedLanguage::Unknown) {
      continue;
    }
    files.push_back(DetectedFile{.path = entry.path(), .language = language});
  }

  std::ranges::sort(files, [](const DetectedFile& lhs, const DetectedFile& rhs) {
    return lhs.path.generic_string() < rhs.path.generic_string();
  });
  return files;
}

}  // namespace cgraph
