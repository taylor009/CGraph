## Why

Graphify's current Python implementation pays runtime startup and import cost on every agent-facing query, and its first-run latency is dominated by semantic enrichment that blocks before a useful graph is available. A native C++/C variant can preserve Graphify parity while making deterministic graph analysis fast, resident, and less disruptive to Codex/Claude-style agent loops.

## What Changes

- Add a native C++/C graph analysis toolchain that ports Graphify's deterministic pipeline: detect, extract, build, deduplicate, cluster, analyze, and export.
- Add a one-shot CLI mode that builds a queryable deterministic graph without requiring a resident process.
- Add a resident per-project daemon (`graphd`) plus thin client commands for low-latency `query`, `path`, `explain`, `update`, `status`, and `shutdown`.
- Add host-orchestrated semantic enrichment ingest that accepts validated fragment files and merges them into the graph without embedding LLM clients, API keys, or provider logic in the binary.
- Add first reference host integrations and an MCP server that front the daemon protocol.
- Preserve compatibility with Graphify's extraction fragments, ID normalization contract, and `graph.json` node-link output format.

## Capabilities

### New Capabilities

- `deterministic-graph-pipeline`: Native detection, extraction, graph build, deduplication, clustering, analysis, and export with Graphify-compatible graph contracts.
- `graph-daemon-client`: Per-project resident daemon, cross-platform local IPC, thin client commands, lifecycle handling, and safe concurrent graph snapshots.
- `semantic-fragment-ingest`: Host-orchestrated semantic enrichment through chunk plans, validated fragment drops, cache updates, and stale/enrichment status.
- `host-integration-mcp`: Host skill integrations and MCP server access that route through the native daemon/client without adding model-provider logic.

### Modified Capabilities

- None.

## Impact

- Adds a native CMake/vcpkg-based C++/C codebase with vendored tree-sitter grammars, igraph, libcurl, JSON, and ICU or utf8proc dependencies.
- Adds platform-specific daemon and IPC behavior for Linux, macOS, and Windows.
- Adds parity fixtures, extractor goldens, graph output diffs, daemon concurrency tests, semantic ingest validation tests, sanitizer builds, and fuzzing targets.
- Requires compatibility benchmarking against the Python Graphify reference before committing to the long-tail language, exporter, and host integration fan-out.
