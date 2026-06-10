#include "cgraph/file_extraction.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, const char* contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
}

// Compares two extraction results by their observable fragment shape: node ids
// and labels in order, plus edge endpoints. Enough to prove a parallel result
// is identical to the serial one for the same file.
[[nodiscard]] bool same_fragment(const cgraph::ExtractionResult& lhs, const cgraph::ExtractionResult& rhs) {
  if (lhs.fragment.nodes.size() != rhs.fragment.nodes.size()) return false;
  if (lhs.fragment.edges.size() != rhs.fragment.edges.size()) return false;
  if (lhs.fragment.warnings != rhs.fragment.warnings) return false;
  for (std::size_t i = 0; i < lhs.fragment.nodes.size(); ++i) {
    if (lhs.fragment.nodes[i].id != rhs.fragment.nodes[i].id) return false;
    if (lhs.fragment.nodes[i].label != rhs.fragment.nodes[i].label) return false;
  }
  for (std::size_t i = 0; i < lhs.fragment.edges.size(); ++i) {
    if (lhs.fragment.edges[i].source != rhs.fragment.edges[i].source) return false;
    if (lhs.fragment.edges[i].target != rhs.fragment.edges[i].target) return false;
    if (lhs.fragment.edges[i].relation != rhs.fragment.edges[i].relation) return false;
  }
  return true;
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
  if (!missing.fragment.nodes.empty() || missing.fragment.warnings.empty()) {
    std::filesystem::remove_all(root);
    return 1;
  }

  // Parallel extraction must produce, in detection order, results identical to
  // serial extraction file-by-file. Build a batch large enough to keep multiple
  // worker threads contended, mixing languages and one missing file.
  std::vector<cgraph::DetectedFile> batch;
  for (int i = 0; i < 64; ++i) {
    const auto py = root / ("mod" + std::to_string(i) + ".py");
    write_file(py, "def f():\n    return g()\n\ndef g():\n    return 1\n");
    batch.push_back({.path = py, .language = cgraph::DetectedLanguage::Python});

    const auto ts = root / ("mod" + std::to_string(i) + ".ts");
    write_file(ts, "export function f() { return g(); }\nfunction g() { return 1; }\n");
    batch.push_back({.path = ts, .language = cgraph::DetectedLanguage::TypeScript});
  }
  batch.push_back({.path = root / "gone.py", .language = cgraph::DetectedLanguage::Python});

  const auto parallel = cgraph::extract_files(batch);
  if (parallel.size() != batch.size()) {
    std::filesystem::remove_all(root);
    return 1;
  }
  for (std::size_t i = 0; i < batch.size(); ++i) {
    const auto serial = cgraph::extract_detected_file(batch[i]);
    if (!same_fragment(parallel[i], serial)) {
      std::filesystem::remove_all(root);
      return 1;
    }
  }

  // An empty batch is valid and yields no results.
  if (!cgraph::extract_files({}).empty()) {
    std::filesystem::remove_all(root);
    return 1;
  }

  std::filesystem::remove_all(root);
  return 0;
}
