#include "cgraph/detect.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, std::string contents = {}) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

bool contains_language(const std::vector<cgraph::DetectedFile>& files, cgraph::DetectedLanguage language) {
  for (const auto& file : files) {
    if (file.language == language) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using cgraph::DetectedLanguage;

  if (cgraph::detect_language("component.blade.php") != DetectedLanguage::PhpBlade) {
    return 1;
  }
  if (cgraph::detect_language("mcp.json") != DetectedLanguage::McpConfig) {
    return 1;
  }
  if (cgraph::detect_language("project.csproj") != DetectedLanguage::MsBuild) {
    return 1;
  }
  if (cgraph::detect_language("main.cpp") != DetectedLanguage::Cpp) {
    return 1;
  }
  if (cgraph::detect_language("tool.py") != DetectedLanguage::Python) {
    return 1;
  }

  const auto root = std::filesystem::temp_directory_path() / "cgraph_detect_test";
  std::filesystem::remove_all(root);
  write_file(root / ".gitignore", "ignored.py\nbuild/\n");
  write_file(root / "src" / "main.cpp");
  write_file(root / "src" / "tool.py");
  write_file(root / "ignored.py");
  write_file(root / "build" / "generated.cpp");
  write_file(root / "README.md");

  const auto files = cgraph::detect_project_files(root);
  std::filesystem::remove_all(root);

  if (!contains_language(files, DetectedLanguage::Cpp)) {
    return 1;
  }
  if (!contains_language(files, DetectedLanguage::Python)) {
    return 1;
  }
  if (files.size() != 2) {
    return 1;
  }

  return 0;
}
