## Context

The parent repository index contains eleven mode-`160000` tree-sitter gitlinks, and `vendor/tree-sitter/CMakeLists.txt` compiles files inside every one of them. Their upstream repositories and commits are recorded in `vendor/tree-sitter/PINS.md`, but the repository has no `.gitmodules`, while both CI jobs use the default `actions/checkout` behavior that does not fetch submodules. CI therefore fails before configuration and leaves a post-job error for the unknown gitlink path.

The vcpkg manifest already provides a single `builtin-baseline`. The current baseline resolves native BLAS/LAPACK dependencies that fail on the supported runner matrix before CGraph tests execute.

Once dependency repair allows tests to execute, the committed retrieval fixture exposes a latent portability defect: its `source_file` fields contain one developer's absolute checkout path. Because context packing prices on-disk source slices, missing CI snippets change the measured selection result. The fixture must store repository-relative paths and the test must resolve them through its compile-time repository root before invoking production packing code.

The fuzzer job also exports `CC=clang` and `CXX=clang++` for the entire process. Manifest installation inherits those variables, so vcpkg builds OpenBLAS Debug AVX-512 assembly with Clang and fails before CGraph configures. Only CGraph's fuzzer targets require Clang; the dependency graph does not.

The updated igraph package no longer makes `<memory>` available transitively to `analysis.cpp`. That translation unit owns igraph objects through `std::unique_ptr`, so it must include the standard header it directly consumes.

Linux compilation also exposes two `std::error_code ec` declarations in the same `current_executable_path` function scope: one in the Linux preprocessor branch and one in the shared argv fallback. macOS does not see the collision because its branch-local declaration is nested inside an `if` block.

GCC then enforces C++20 designated-initializer order in the daemon-lifecycle fixture. `Node::source_location` is declared before `Node::kind`, while that fixture initializes `kind` first; Clang accepts the same code with a warning.

Keeping Clang scoped to CGraph exposes a compiler boundary in the updated igraph port. The port enables link-time optimization automatically, so vcpkg's native GCC emits compiler-specific LTO objects that the Clang fuzzer link cannot consume. OpenBLAS must retain the native compiler, so fuzzer dependencies need a Linux triplet that disables igraph LTO at package creation.

On the current Windows runner, vcpkg's dynamic-library LAPACK provider builds LAPACK 3.12.1 with LLVM Flang 22.1.3, which cannot parse the generated module files. The same vcpkg graph already selects its C-only `clapack` provider when dependency libraries are static, avoiding a Fortran compiler while preserving the LAPACK package contract.

After the static provider succeeds, Ninja selects MinGW from the hosted runner's `PATH` while the `x64-windows-static-md` dependencies use the MSVC ABI. The build also exposes POSIX-only UTC conversion calls in both the durable-ledger implementation and the CLI's independent `today` calculation. The Windows job must select MSVC explicitly, and UTC conversion must branch on the target platform rather than assume POSIX APIs.

## Goals / Non-Goals

**Goals:**

- Make a fresh recursive checkout reproduce all tree-sitter source pins recorded by the parent commit.
- Keep vcpkg version selection in the manifest and advance it as one cross-platform dependency graph.
- Make every existing CI job configure, build, and execute its intended CGraph tests.
- Validate the static repository contract locally before relying on hosted runners, then validate the real supported matrix in GitHub Actions.

**Non-Goals:**

- Changing CGraph source or runtime behavior.
- Copying third-party source into the parent repository.
- Adding platform-specific dependency versions, exclusions, or fallback implementations.
- Changing the set of supported CI runners or presets.

## Decisions

### Declare the existing gitlinks as submodules

Add `.gitmodules` entries for the existing paths and canonical URLs, and set `submodules: recursive` on both checkout steps. This matches the repository's existing mode-`160000` data model and preserves commit pinning in the parent index.

Alternative considered: replace gitlinks with ordinary vendored files. Rejected because it rewrites the dependency model, duplicates upstream source in the parent history, and exceeds the smallest source-level repair.

### Keep one vcpkg source of truth

Use `vcpkg x-update-baseline` to advance `vcpkg.json` and commit only the resulting `builtin-baseline` change. `lukka/run-vcpkg` consumes that manifest baseline, so CI does not need a second commit pin in workflow YAML.

Alternative considered: pin or override ports per runner. Rejected because platform branches would hide a dependency-graph inconsistency instead of standardizing it at the manifest.

### Prove the contract in two layers

Before implementation, preserve the current failing evidence from GitHub Actions: checkout cannot resolve `vendor/tree-sitter/core`, dependency installation fails, and no CGraph test runs. After implementation, validate locally that every gitlink has a matching `.gitmodules` entry and materializes at its parent-index commit, then configure, build, and run the applicable native tests. The final acceptance gate is the unmodified GitHub Actions matrix reaching and passing every intended CGraph test.

This metadata change has no useful isolated unit-test seam. Its direct test is a fresh recursive checkout plus the real package/build/test flow.

### Keep committed retrieval inputs checkout-portable

Store fixture `source_file` values relative to the repository and reject absolute paths in the parity executable. Rebase each path through the existing `CGRAPH_REPO_ROOT` test definition before publishing the snapshot to the production request path. This retains real source-slice accounting without embedding a host path or adding runtime fallback behavior.

### Scope the fuzzer compiler to CGraph

Select `clang` and `clang++` with `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` in the fuzzer configure preset instead of exporting job-wide compiler variables. CGraph still configures and builds its libFuzzer targets with Clang, while vcpkg resolves its native dependency compiler independently. This keeps one manifest graph and avoids a compiler override or port fallback for OpenBLAS.

### Declare direct standard-library dependencies

Include `<memory>` in `analysis.cpp`, where `std::unique_ptr` is used. The previous successful builds depended on an implementation detail of older igraph headers; making the dependency explicit keeps runtime behavior unchanged and lets the single updated manifest graph compile consistently.

Give the Linux `/proc/self/exe` probe's error code a role-specific name. This preserves the existing executable-resolution behavior and lets the platform branch coexist with the shared argv canonicalization path without adding another code path.

Order the daemon-lifecycle fixture's `source_location` and `kind` designators exactly as `Node` declares them. The fixture values and persistence assertion remain unchanged, while both supported compiler families accept the translation unit.

Use a dedicated `x64-linux-clang` vcpkg triplet for the fuzzer job. The triplet preserves the built-in Linux architecture and linkage contract and appends `-DIGRAPH_ENABLE_LTO=OFF` to port configuration, after the igraph port's automatic setting. Export the triplet for `run-vcpkg` and pass it explicitly as CMake's `VCPKG_TARGET_TRIPLET` so dependency installation and consumption share one ABI directory. This keeps GCC for OpenBLAS, Clang for CGraph and libFuzzer, and ordinary object code at their static-library boundary.

Use an `x64-windows-static-md` triplet for the Windows default job. Static dependency libraries trigger vcpkg's declared `clapack` provider while the dynamic CRT retains CGraph's normal MSVC runtime contract. Select the triplet through the existing matrix configure step; do not pin a Windows-only package version or change the runner.

Initialize the hosted Windows MSVC environment and pass `cl` explicitly to CMake so CGraph and its vcpkg graph share one ABI. In the existing UTC ledger helpers, select `gmtime_s` and `_mkgmtime` for `_WIN32` and retain `gmtime_r` and `timegm` elsewhere. Make the CLI's `today` path reuse the ledger's canonical UTC formatter/parser instead of duplicating platform conversion calls.

## Risks / Trade-offs

- **An upstream submodule becomes unavailable** → Pins remain immutable in the parent commit, and CI failure identifies the exact unavailable repository rather than compiling partial source.
- **The newest vcpkg baseline introduces a different runner regression** → Advance only one manifest baseline and accept it only after the complete matrix passes; do not add per-runner exceptions.
- **Native dependencies use compiler-specific LTO** → Disable igraph LTO only in the Linux Clang-consumer triplet; preserve the same versions, linkage, and native dependency compiler.
- **The Windows runner's Fortran compiler rejects LAPACK sources** → Use static dependency linkage so vcpkg selects its supported C-only LAPACK provider, retaining the dynamic MSVC runtime.
- **Ninja selects a compiler with a different ABI from the Windows dependency triplet** → Initialize and select MSVC explicitly before configuring CGraph.
- **Recursive checkout costs additional network time** → The repository needs all eleven grammars to build the configured extractor set, so the cost reflects the actual source contract.
- **Hosted runner images change later** → A manifest-pinned graph and complete matrix make drift visible; future repairs must again update the canonical baseline rather than add fallbacks.

## Migration Plan

1. Add `.gitmodules` without changing any gitlink commit.
2. Enable recursive submodule checkout in both CI jobs.
3. Advance the manifest baseline with the vcpkg command.
4. Run local contract and native build/test validation.
5. Push the isolated branch and require the existing GitHub Actions matrix to pass before merge.

Rollback is a normal revert of this change; no runtime data or API migration exists.

## Open Questions

None. The supported runner and preset matrix remains the one already declared in `.github/workflows/ci.yml`.
