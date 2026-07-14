## 1. Tree-sitter Source Contract

- [x] 1.1 Record the pre-change contract failure: the eleven tree-sitter gitlinks have no `.gitmodules` declarations and a recursive checkout cannot materialize them.
- [x] 1.2 Add one canonical `.gitmodules` entry for every existing tree-sitter gitlink, preserving all parent-index commits, then verify paths, URLs, and documented pins agree.
- [x] 1.3 Configure both GitHub Actions checkout steps to fetch submodules recursively, then validate the workflow syntax and checkout contract.

## 2. Native Dependency Graph

- [x] 2.1 Advance the sole `vcpkg.json` `builtin-baseline` with `vcpkg x-update-baseline`; do not add workflow or platform-specific version overrides.
- [x] 2.2 Materialize the updated manifest dependency graph and verify CMake configuration succeeds against it.
- [x] 2.3 Scope Clang selection to the CGraph fuzzer preset, leaving vcpkg dependency compiler selection unmodified, and verify the preset still selects a libFuzzer-capable Clang toolchain.
- [x] 2.4 Add the direct `<memory>` dependency required by `analysis.cpp` and verify the updated igraph graph compiles without transitive-header assumptions.

## 3. End-to-End Verification

- [x] 3.1 From the isolated worktree, initialize every tree-sitter submodule, build the normal native preset, and pass its complete CTest preset.
- [x] 3.2 Build the fuzzer preset and pass `ctest --preset fuzzers -R 'cgraph_fuzz.*smoke_test'` with Clang.
- [ ] 3.3 Push the branch, open a pull request, and verify every existing Linux, macOS, Windows, sanitizer, and fuzzer GitHub Actions job reaches and passes its intended CGraph tests.
