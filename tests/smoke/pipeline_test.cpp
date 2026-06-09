#include "cgraph/pipeline.hpp"

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
  const auto root = std::filesystem::temp_directory_path() / "cgraph_pipeline_test";
  const auto out = std::filesystem::temp_directory_path() / "cgraph_pipeline_out";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(out);

  write_file(root / "main.py", "def main():\n    return helper()\n\ndef helper():\n    return 1\n");

  const auto result = cgraph::run_one_shot(root);
  if (result.file_count != 1 || result.graph.nodes.empty()) {
    std::filesystem::remove_all(root);
    return 1;
  }

  cgraph::write_exports(result.graph, out);
  const bool ok =
      std::filesystem::exists(out / "graph.json") &&
      std::filesystem::exists(out / "graph.html") &&
      std::filesystem::exists(out / "graph.svg") &&
      std::filesystem::exists(out / "obsidian.md") &&
      std::filesystem::exists(out / "cypher.txt") &&
      std::filesystem::exists(out / "call-flow.html");

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(out);
  return ok ? 0 : 1;
}
