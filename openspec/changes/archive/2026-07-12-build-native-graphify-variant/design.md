## Context

Graphify builds a queryable knowledge graph from code, docs, and media, but the current Python implementation creates two user-visible costs: every agent query starts a fresh Python process, and first-run semantic enrichment can delay access to the deterministic code graph. The planspace handoff selects a native C++/C implementation, with tree-sitter and igraph at the C boundary, to preserve Graphify behavior while improving cold query latency, resident memory use, and distribution.

The Python Graphify repository remains the behavioral reference. Native output must preserve Graphify's fragment schema, ID normalization semantics, and `graph.json` node-link format closely enough that existing consumers continue to work.

## Goals / Non-Goals

**Goals:**

- Build a native deterministic graph pipeline that ports Graphify's detect, extract, build, dedup, cluster, analyze, and export stages.
- Provide one-shot CLI mode as the first runnable milestone and universal fallback.
- Provide a per-project daemon with thin client commands for low-latency agent and hook calls.
- Support host-orchestrated semantic enrichment through chunk plans and validated fragment ingestion.
- Add MCP and two reference host integrations after the daemon and parity gates are proven.
- Verify parity, daemon behavior, incremental behavior, semantic ingest, and extractor safety with automated tests, sanitizers, fuzzing, and benchmarks.

**Non-Goals:**

- No embedded LLM client, API key storage, provider abstraction, or automatic model spending in the native binary.
- No replacement for media transcription engines; existing native transcription tools can be invoked externally.
- No new graph serialization format for v1; `graph.json` compatibility is required.
- No commitment to the long-tail host integrations until the native engine and daemon beat the Python reference on measured experience gates.

## Decisions

1. **Use C++ for engine, daemon, exporters, and clients, with C libraries at the boundary.**
   tree-sitter grammars and igraph are C libraries, so C++ can link them directly while using RAII for ownership and resource cleanup. Rust and Go remain viable alternatives, but C++/C gives direct igraph Leiden/Louvain access and avoids extra FFI layers over the core parser and graph libraries.

2. **Vendor tree-sitter grammars and use vcpkg for heavier dependencies.**
   Grammars are mostly standalone `parser.c` and optional `scanner.c` files, so vendoring pins exact parser behavior and supports single-binary distribution. igraph, libcurl, JSON, and ICU or utf8proc have broader platform build concerns, so they should come through a committed vcpkg manifest.

3. **Port deterministic pipeline before daemon, semantic enrichment, MCP, or host fan-out.**
   The one-shot CLI is the first end-to-end milestone because it proves extraction, graph build, clustering, analysis, and export parity without daemon complexity. The daemon then reuses the same engine code.

4. **Keep daemon state as a cache over authoritative disk outputs.**
   The daemon holds an immutable graph snapshot in memory and serves local requests, but `graph.json` and content-hash caches remain the source of truth. On crash or restart, the daemon reloads from disk rather than replaying a write-ahead log.

5. **Use one daemon per canonical project root.**
   Per-root daemons isolate failure and permissions, match Graphify's per-repo output model, and keep socket trust boundaries simple. Idle shutdown prevents inactive projects from consuming resident memory.

6. **Use length-prefixed JSON IPC over Unix sockets or Windows named pipes.**
   This keeps the client/daemon protocol small, portable, and easy to inspect. Supported operations are `query`, `path`, `explain`, `update`, `status`, and `shutdown`.

7. **Use single-writer, immutable snapshot publication for daemon concurrency.**
   The writer performs graph mutations and publishes a new `shared_ptr<const GraphSnapshot>`; readers always observe a complete snapshot. This avoids torn graph reads during watcher updates or semantic fragment merges.

8. **Treat semantic enrichment as host-orchestrated fragment ingest.**
   The native tool emits chunk plans and validates dropped fragments, but it does not call models. This preserves control over token spend and keeps the binary provider-neutral.

## Risks / Trade-offs

- **C++ memory safety risk on untrusted repo input** -> Use RAII, avoid raw owning pointers, run ASan/UBSan in CI, and add libFuzzer targets for each extractor.
- **Parity drift from Python Graphify** -> Build fixture-level ID parity, extractor goldens, graph node/edge diffs, and output loader compatibility tests before adding integration breadth.
- **Large scope across languages, exporters, and hosts** -> Gate the long tail behind one-shot parity and daemon experience benchmarks.
- **Daemon complexity could hide correctness bugs** -> Keep disk authoritative, preserve one-shot mode, and test spawn races, version skew, idle shutdown, snapshot consistency, and cross-user rejection.
- **Semantic enrichment can still be slow** -> Make deterministic graph availability independent of enrichment and expose `enriching` or `enrichment pending` status to clients and hosts.
