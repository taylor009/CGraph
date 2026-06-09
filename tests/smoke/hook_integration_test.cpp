#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

bool lacks(std::string_view haystack, std::string_view needle) {
  return !contains(haystack, needle);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 1;
  }

  const std::filesystem::path hook_path = argv[1];
  std::ifstream input(hook_path, std::ios::binary);
  if (!input) {
    return 1;
  }
  const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

  if (!contains(contents, "cgraph-client") ||
      !contains(contents, "CGRAPH_CLIENT") ||
      !contains(contents, "CGRAPH_PROJECT_ROOT") ||
      !contains(contents, "CGRAPH_DAEMON") ||
      !contains(contents, "--root") ||
      !contains(contents, "--daemon") ||
      !contains(contents, "query|path|explain|update|status|shutdown") ||
      !contains(contents, "exec \"${CGRAPH_CLIENT}\"") ||
      !lacks(contents, "python") ||
      !lacks(contents, "node ")) {
    return 1;
  }

  return 0;
}
