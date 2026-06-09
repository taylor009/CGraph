#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cgraph {

// A TypeScript/JavaScript path alias of the wildcard form `"<prefix>*"`, mapped
// to one or more absolute directory prefixes. For `"@/*": ["./*"]` under a
// project root `/p`, this is { pattern_prefix = "@/", replacement_prefixes =
// {"/p/"} }: an import of `@/lib/utils` becomes `/p/lib/utils`.
struct PathAlias {
  std::string pattern_prefix;
  std::vector<std::string> replacement_prefixes;
};

// Reads `tsconfig.json` (then `jsconfig.json`) at `root` and returns its
// wildcard `compilerOptions.paths`, each replacement resolved against `baseUrl`
// (defaulting to the config's own directory, matching the TypeScript compiler).
// Returns empty when no config exists, it cannot be parsed, or it declares no
// wildcard paths. These aliases let cgraph resolve `@/...`-style imports to real
// project files — dependencies that a relative-only resolver leaves dangling.
[[nodiscard]] std::vector<PathAlias> load_path_aliases(const std::filesystem::path& root);

// Returns the candidate absolute (extension-stripped) import targets for a raw
// module specifier, expanding every alias whose prefix it matches. A spec that
// matches no alias yields no candidates.
[[nodiscard]] std::vector<std::string> expand_path_alias(std::span<const PathAlias> aliases, const std::string& spec);

}  // namespace cgraph
