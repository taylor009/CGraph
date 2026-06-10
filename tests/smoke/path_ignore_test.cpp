#include "cgraph/path_ignore.hpp"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

void write_file(const fs::path& path, std::string contents) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

// is_skipped_directory covers the always-skip vendor/build/VCS set regardless of
// any .gitignore.
int test_skipped_directory() {
  if (!cgraph::is_skipped_directory("node_modules") || !cgraph::is_skipped_directory(".git") ||
      !cgraph::is_skipped_directory("build") || !cgraph::is_skipped_directory("cgraph-out")) {
    return 1;
  }
  // A normal source directory must not be skipped.
  if (cgraph::is_skipped_directory("src") || cgraph::is_skipped_directory("docs")) {
    return 1;
  }
  return 0;
}

// The regression this module exists to prevent: a root .gitignore that ignores a
// vendored toolchain (the repo's own `/.vcpkg/`) must cause that directory AND
// its contents to match, so every project scanner skips it the same way.
int test_gitignore_matches_vendored_dir() {
  const auto root = fs::temp_directory_path() / "cgraph_path_ignore_test";
  fs::remove_all(root);
  fs::create_directories(root);

  // Mirror this repo's .gitignore shapes: anchored (`/.vcpkg/`, `/build/`) and
  // a bare-name pattern (`vcpkg_installed`) matched at any depth. Comments,
  // blanks, and negations must be ignored by the parser.
  write_file(root / ".gitignore",
             "# comment\n"
             "\n"
             "/build/\n"
             "/.vcpkg/\n"
             "vcpkg_installed/\n"
             "!keep.me\n");

  const auto patterns = cgraph::read_root_gitignore(root);
  // 3 real patterns; comment/blank/negation dropped.
  if (patterns.size() != 3) {
    fs::remove_all(root);
    return 1;
  }

  write_file(root / ".vcpkg" / "ports" / "igraph" / "README.md", "vendored doc\n");
  write_file(root / "build" / "default" / "x.txt", "build artifact\n");
  write_file(root / "src" / "deep" / "vcpkg_installed" / "lib.txt", "nested vendored\n");
  write_file(root / "docs" / "real.md", "real doc\n");

  int rc = 0;
  // Anchored dir + its contents match.
  rc |= !cgraph::matches_simple_gitignore(root, root / ".vcpkg", patterns);
  rc |= !cgraph::matches_simple_gitignore(root, root / ".vcpkg" / "ports" / "igraph" / "README.md", patterns);
  rc |= !cgraph::matches_simple_gitignore(root, root / "build", patterns);
  // Bare-name pattern matches at depth.
  rc |= !cgraph::matches_simple_gitignore(root, root / "src" / "deep" / "vcpkg_installed", patterns);
  // A real source/doc path must NOT match — otherwise we'd skip the project itself.
  rc |= cgraph::matches_simple_gitignore(root, root / "docs" / "real.md", patterns);
  rc |= cgraph::matches_simple_gitignore(root, root / "src", patterns);

  fs::remove_all(root);
  return rc ? 1 : 0;
}

// No .gitignore -> no patterns -> nothing matches (scanner falls back to the
// always-skip directory set only).
int test_empty_gitignore() {
  const auto root = fs::temp_directory_path() / "cgraph_path_ignore_empty";
  fs::remove_all(root);
  fs::create_directories(root);
  const auto patterns = cgraph::read_root_gitignore(root);
  const int rc = (!patterns.empty() ||
                  cgraph::matches_simple_gitignore(root, root / "anything.md", patterns))
                     ? 1
                     : 0;
  fs::remove_all(root);
  return rc;
}

}  // namespace

int main() {
  return test_skipped_directory() || test_gitignore_matches_vendored_dir() || test_empty_gitignore();
}
