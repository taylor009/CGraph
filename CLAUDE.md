# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`cgraph` is a native C++20/C graph-analysis engine that ports Graphify's deterministic
code-graph pipeline. It produces a Graphify-compatible `graph.json` (node-link format) plus
HTML/SVG/Obsidian/Cypher exports, and serves low-latency graph queries from a resident
per-project daemon. The design split is deliberate: **the native binary owns deterministic
graph work, chunk planning, fragment validation, and local graph mutation. Hosts own all
model/LLM logic** — there are no API keys, providers, or LLM clients in the binary. Semantic
enrichment enters only as host-written, validated fragment files. See
`docs/host-skill-contract.md`.

## Build, test, run

The project uses CMake presets + vcpkg. **vcpkg is vendored at `./.vcpkg`** and the presets
read `$env{VCPKG_ROOT}`, which is **not** set in the environment — you must export it:

```bash
export VCPKG_ROOT="$PWD/.vcpkg"          # required; presets fail without it
cmake --preset default                    # configure -> build/default (Ninja, Debug)
cmake --build build/default               # build all targets
ctest --preset default                    # run the 48 smoke tests (output-on-failure)
ctest --preset default -R cgraph_detect_test   # run a single test by name (regex)
```

Other presets: `sanitizers` (ASan/UBSan, `build/sanitizers`) and `fuzzers` (libFuzzer +
ASan/UBSan, `build/fuzzers`, requires `CGRAPH_BUILD_FUZZERS=ON`). System deps via vcpkg:
`igraph`, `curl`, `nlohmann-json`, `utf8proc`; tree-sitter is vendored under `vendor/tree-sitter`.

Built binaries land in `build/default/src/<target>/`:

- `cgraph` (cli) — one-shot: `cgraph --root PATH --out cgraph-out` builds the graph and writes all exports.
- `graphd` (daemon) — resident per-project server; also `--benchmark-query --graph PATH --query TEXT`.
- `cgraph-client` (client) — thin client: `cgraph-client --root PATH <query|path|explain|update|status|shutdown> [JSON params]`.
- `cgraph-mcp` (mcp) — MCP server (stdio JSON-RPC) fronting the daemon ops.

Benchmarks (compare native vs Python Graphify): `scripts/benchmark_one_shot.py`, `scripts/benchmark_daemon_query.py`.

## Architecture

Everything links against one static library, **`cgraph::engine`** (`src/engine/`), which holds
the entire pipeline. The four executables (`cli`, `daemon`, `client`, `mcp`) are thin `main.cpp`
shells over it. Core data types (`Node`, `Edge`, `Hyperedge`, `Fragment`, `GraphSnapshot`,
`Confidence`, `BuildState`) live in `src/engine/include/cgraph/types.hpp` — read this first.

**Deterministic pipeline** (`pipeline.cpp::run_one_shot`, the canonical flow):
`detect_project_files` → per-file `extract_detected_file` (tree-sitter via `parser_pool`,
language-specific `python_extractor`/`javascript_extractor`/`non_grammar_extractors`,
configured by `language_config`/`configured_extractors`) → `merge_fragments`
(`graph_builder`) → `resolve_raw_calls` → `semantic_dedup` (`dedup`) → `detect_communities`
(`analysis`/igraph clustering) → `analyze_graph` → `write_exports` (`export_json`). ID
normalization (`normalize.cpp`) preserves Graphify's ID contract — this is a parity surface,
treat it as load-bearing.

**Daemon / client / IPC** (`graph-daemon-client` capability):
- `daemon_identity.cpp` derives a per-project-root hash + endpoint name (one daemon per canonical project root).
- `daemon_lifecycle.cpp` / `daemon_endpoint.cpp` handle spawn, listen, and connection.
- `daemon_ops.cpp::handle_daemon_request` dispatches the six ops (`query`/`path`/`explain`/`update`/`status`/`shutdown`). Graph state is a `shared_ptr<const GraphSnapshot>` read under `snapshot_mutex`; mutations go through a **single-writer path** (`writer_mutex`, `publish_graph_snapshot`/`mutate_graph_snapshot`).
- `protocol.cpp` — length-prefixed JSON frames, `kProtocolVersion = 1`; version-checked on every message.
- `client_runtime.cpp` — thin client with connect/spawn/backoff hooks (`ClientRuntimeHooks`); auto-spawns the daemon if absent.
- `incremental_update.cpp` + `file_watcher.cpp` + `file_cache.cpp` — `update .` triggers a full stat-index rescan; the watcher feeds incremental graph updates.
- `daemon_security.cpp` / `daemon_hardening` tests — endpoint hardening surface.

**Semantic enrichment ingest** (`semantic-fragment-ingest`, host-orchestrated, no LLM in binary):
`semantic_chunk_plan.cpp` emits bounded chunk plans for uncached/stale docs/media. Hosts compute
each chunk and atomically write exactly one `chunk_NN.json` fragment into the semantic drop dir.
`semantic_drop.cpp` watches for these; `semantic_fragment_validation.cpp` + `fragment_json.cpp`
validate against the Graphify fragment schema (malformed → rejected, graph unchanged);
`semantic_ingest.cpp` merges valid fragments through the daemon single-writer path;
`semantic_cache.cpp` keys cache records by content hash. Enrichment status is surfaced through
`status` (`enrichment_state`/`pending`/`running`/`stale`/`failed`).

**MCP** (`src/mcp/mcp_server.cpp`): exposes `graph_query`/`graph_path`/`graph_explain`/
`graph_update`/`graph_status`/`graph_shutdown` tools that translate directly to daemon ops. Adds
no model logic.

## Conventions

- **Tests are 1:1 with engine source** in `tests/smoke/` (e.g. `dedup.cpp` → `dedup_test.cpp`), each registered as its own CTest executable in `tests/smoke/CMakeLists.txt`. Fixtures live in `tests/fixtures/`. Goldens for parity in `extractor_goldens_test.cpp`. Fuzzers in `tests/fuzz/`.
- **Adding an engine source file** means editing `src/engine/CMakeLists.txt` (explicit source list, not globbed) and adding the matching `_test.cpp` + `add_test` block.
- Warnings/sanitizers are applied per-target via `cmake/CgraphWarnings.cmake` and `cmake/CgraphSanitizers.cmake` (`cgraph_set_warnings` / `cgraph_enable_sanitizers`) — call both on any new target.
- **Graphify parity is a hard contract**: extraction fragment shape, ID normalization, and `graph.json` node-link output must stay compatible. Changes here need golden/parity test coverage.

## Spec workflow

This repo uses **OpenSpec** (`openspec/`, config in `openspec/config.yaml`). The active change is
`openspec/changes/build-native-graphify-variant/` (proposal, design, tasks, and per-capability
specs under `specs/`). The four capability names above map to the spec directories there.
`/opsx:*` slash commands and the `openspec-*` skills drive the propose → apply → verify → archive
loop.
