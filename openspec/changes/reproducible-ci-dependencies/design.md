## Context

The parent repository index contains eleven mode-`160000` tree-sitter gitlinks, and `vendor/tree-sitter/CMakeLists.txt` compiles files inside every one of them. Their upstream repositories and commits are recorded in `vendor/tree-sitter/PINS.md`, but the repository has no `.gitmodules`, while both CI jobs use the default `actions/checkout` behavior that does not fetch submodules. CI therefore fails before configuration and leaves a post-job error for the unknown gitlink path.

The vcpkg manifest already provides a single `builtin-baseline`. The current baseline resolves native BLAS/LAPACK dependencies that fail on the supported runner matrix before CGraph tests execute.

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

## Risks / Trade-offs

- **An upstream submodule becomes unavailable** → Pins remain immutable in the parent commit, and CI failure identifies the exact unavailable repository rather than compiling partial source.
- **The newest vcpkg baseline introduces a different runner regression** → Advance only one manifest baseline and accept it only after the complete matrix passes; do not add per-runner exceptions.
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
