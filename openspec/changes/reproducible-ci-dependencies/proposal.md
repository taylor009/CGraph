## Why

CGraph's GitHub Actions matrix currently fails during dependency checkout or installation before any CGraph test executes. The repository already records tree-sitter dependencies as gitlinks and pins vcpkg through its manifest, but CI does not materialize the gitlinks and the current baseline does not build across the supported matrix.

## What Changes

- Declare every existing tree-sitter gitlink in `.gitmodules` using its canonical upstream URL.
- Make every CI checkout recursively materialize those declared submodules.
- Advance the single `builtin-baseline` in `vcpkg.json` to a revision that resolves and builds on Linux, macOS, and Windows.
- Scope the fuzzer job's Clang selection to the CGraph CMake preset so dependency builds retain vcpkg's native toolchain.
- Declare the standard-library header used by graph-analysis ownership types instead of relying on igraph's transitive includes.
- Normalize the committed retrieval fixture to repository-relative source paths so the now-running matrix observes the same token costs on every checkout.
- Verify the normal and fuzzer jobs reach and pass CGraph's tests on the complete GitHub Actions matrix.
- Non-goals: changing CGraph runtime behavior, replacing gitlinks with copied source, or adding platform-specific dependency fallbacks.

## Capabilities

### New Capabilities

- `reproducible-ci-dependencies`: CI checkouts materialize the repository's declared source dependencies and install one manifest-pinned dependency graph consistently across supported runners.

### Modified Capabilities

None.

## Impact

- Dependency metadata: `.gitmodules`, `vcpkg.json`
- Automation: `.github/workflows/ci.yml`, `CMakePresets.json`
- Test portability: `tests/fixtures/pack_context_parity/graph.json`, `tests/smoke/pack_context_parity_test.cpp`
- Native compilation: `src/engine/analysis.cpp`
- External systems: GitHub Actions runners, upstream tree-sitter repositories, and the vcpkg registry
- Public APIs and CGraph runtime behavior: unchanged
