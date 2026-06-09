#include "cgraph/tsconfig_aliases.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "cgraph_tsconfig_aliases_test";
  fs::remove_all(root);
  fs::create_directories(root);

  // A typical Next.js tsconfig: `@/*` maps to the project root. JSON-with-
  // comments must be tolerated.
  write_file(root / "tsconfig.json", R"json({
  // project config
  "compilerOptions": {
    "baseUrl": ".",
    "paths": { "@/*": ["./*"], "~types/*": ["lib/types/*"] }
  }
})json");

  const auto aliases = cgraph::load_path_aliases(root);
  if (aliases.size() != 2) {
    return 1;
  }

  // `@/lib/utils` resolves to <root>/lib/utils; `~types/x` to <root>/lib/types/x.
  const auto canonical_root = fs::weakly_canonical(root).generic_string();
  const auto at = cgraph::expand_path_alias(aliases, "@/lib/utils");
  if (at.size() != 1 || at.front() != canonical_root + "/lib/utils") {
    return 1;
  }
  const auto tilde = cgraph::expand_path_alias(aliases, "~types/notebook");
  if (tilde.size() != 1 || tilde.front() != canonical_root + "/lib/types/notebook") {
    return 1;
  }
  // A relative/bare specifier that matches no alias yields no candidates.
  if (!cgraph::expand_path_alias(aliases, "./local").empty()) {
    return 1;
  }
  if (!cgraph::expand_path_alias(aliases, "react").empty()) {
    return 1;
  }

  // No config -> no aliases (and no throw).
  const auto empty_root = root / "empty";
  fs::create_directories(empty_root);
  if (!cgraph::load_path_aliases(empty_root).empty()) {
    return 1;
  }

  fs::remove_all(root);
  return 0;
}
