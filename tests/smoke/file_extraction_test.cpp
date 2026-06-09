#include "cgraph/file_extraction.hpp"

#include <filesystem>
#include <fstream>

namespace {

void write_file(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph_file_extraction_test";
  std::filesystem::remove_all(root);
  const auto source_path = root / "main.py";
  write_file(source_path, "def main():\n    return helper()\n");

  const auto result = cgraph::extract_detected_file(cgraph::DetectedFile{
      .path = source_path,
      .language = cgraph::DetectedLanguage::Python,
  });
  if (result.fragment.nodes.empty() || !result.fragment.warnings.empty()) {
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto missing = cgraph::extract_detected_file(cgraph::DetectedFile{
      .path = root / "missing.py",
      .language = cgraph::DetectedLanguage::Python,
  });
  std::filesystem::remove_all(root);
  if (!missing.fragment.nodes.empty() || missing.fragment.warnings.empty()) {
    return 1;
  }

  return 0;
}
