#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 1;
  }

  const std::filesystem::path contract_path = argv[1];
  std::ifstream input(contract_path, std::ios::binary);
  if (!input) {
    return 1;
  }
  const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

  if (!contains(contents, "# Host Skill Contract") ||
      !contains(contents, "## Deterministic Graph Commands") ||
      !contains(contents, "## Chunk Plan Dispatch") ||
      !contains(contents, "## Semantic Fragment Schema") ||
      !contains(contents, "## Disk Success Signals") ||
      !contains(contents, "query") ||
      !contains(contents, "path") ||
      !contains(contents, "explain") ||
      !contains(contents, "update") ||
      !contains(contents, "status") ||
      !contains(contents, "shutdown") ||
      !contains(contents, "chunk_NN.json")) {
    return 1;
  }

  return 0;
}
