#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cgraph {

// Shared project-scanning ignore logic. Both the deterministic code detector
// (detect.cpp) and the semantic chunk planner (semantic_chunk_plan.cpp) walk the
// project tree and must skip the same directories and honor the same root
// .gitignore — keeping this in one place prevents the two scanners from drifting
// (e.g. the planner pulling in a vendored, git-ignored .vcpkg toolchain that the
// code scan correctly skips).

// Directories that are always skipped regardless of .gitignore (VCS metadata,
// editor caches, build output, dependency/vendor trees, including the Python
// ecosystem: virtualenvs, site-packages, and tool caches).
[[nodiscard]] bool is_skipped_directory(std::string_view name);

// Whether a directory should be skipped during a project walk: true when its leaf
// name is in the always-skip set OR it is a virtualenv root (contains a
// `pyvenv.cfg` marker). The structural check catches venvs whose directory name is
// not in the list (e.g. `qa-env/`). Callers that have the full path should prefer
// this over the name-only `is_skipped_directory`.
[[nodiscard]] bool is_dependency_directory(const std::filesystem::path& dir);

// Parse the root .gitignore into a list of simple patterns. Comments, blank
// lines, and negations (`!`) are dropped; a trailing `/` is stripped so a
// directory entry matches both the directory and its contents.
[[nodiscard]] std::vector<std::string> read_root_gitignore(const std::filesystem::path& root);

// Match a path (file or directory) against the parsed patterns. Supports the
// common gitignore shapes used in this repo: anchored (`/build`), bare-name
// (`node_modules`, matched at any depth), and path (`a/b`) patterns.
[[nodiscard]] bool matches_simple_gitignore(
    const std::filesystem::path& root,
    const std::filesystem::path& path,
    const std::vector<std::string>& patterns);

}  // namespace cgraph
