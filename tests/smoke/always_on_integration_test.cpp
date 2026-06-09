#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }

  const auto skill = read_file(argv[1]);
  const auto runner = read_file(argv[2]);
  if (skill.empty() || runner.empty()) {
    return 1;
  }

  if (!contains(skill, "Host Skill Contract") ||
      !contains(skill, "docs/host-skill-contract.md") ||
      !contains(skill, "cgraph-client") ||
      !contains(skill, "chunk_NN.json") ||
      !contains(skill, "enrichment_state") ||
      !contains(skill, "no provider logic") ||
      !contains(runner, "cgraph-client") ||
      !contains(runner, "CGRAPH_PROJECT_ROOT") ||
      !contains(runner, "CGRAPH_INTERVAL_SECONDS") ||
      !contains(runner, "status") ||
      !contains(runner, "update") ||
      !contains(runner, "run_client status") ||
      !contains(runner, "run_client update") ||
      !contains(runner, "--daemon \"${CGRAPH_DAEMON}\"") ||
      contains(skill, "OPENAI_API_KEY") ||
      contains(runner, "OPENAI_API_KEY")) {
    return 1;
  }

  return 0;
}
