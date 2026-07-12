## 1. Project Skeleton and Dependencies

- [x] 1.1 Create the CMake project layout for engine, CLI, daemon, client, tests, and fuzz targets
- [x] 1.2 Add a committed `vcpkg.json` for igraph, libcurl, JSON, and ICU or utf8proc dependencies
- [x] 1.3 Vendor pinned tree-sitter core and grammar sources into a static grammar library
- [x] 1.4 Add Linux, macOS, and Windows CI jobs with normal and ASan/UBSan configurations
- [x] 1.5 Add a no-op binary smoke test that links all native dependencies on each target platform

## 2. Core Graph Contracts

- [x] 2.1 Define native node, edge, hyperedge, fragment, graph snapshot, and source-location types
- [x] 2.2 Implement Graphify-compatible `make_id` normalization with ICU or utf8proc
- [x] 2.3 Port ID normalization fixtures covering ASCII, accented, composed, decomposed, CJK, and Cyrillic identifiers
- [x] 2.4 Implement JSON serialization and parsing for Graphify-compatible extraction fragments
- [x] 2.5 Add fragment schema validation tests for valid fragments, malformed fragments, and optional fields

## 3. Deterministic Extraction

- [x] 3.1 Implement file detection with extension handling, gitignore behavior, and filename special cases
- [x] 3.2 Implement the `LanguageConfig` abstraction and intern tree-sitter node symbols at config initialization
- [x] 3.3 Implement one `TSParser` per worker thread with parser reuse and no cross-thread parser sharing
- [x] 3.4 Implement the generic tree-sitter walker for config-driven languages
- [x] 3.5 Port initial config-driven extractors for Ruby, Kotlin, Scala, Java, Groovy, C, and C++
- [x] 3.6 Port bespoke Python extraction behavior
- [x] 3.7 Port bespoke JavaScript and TypeScript extraction behavior
- [x] 3.8 Port non-grammar parsers needed for Graphify parity, including MSBuild XML, forms, Apex, and MCP config
- [x] 3.9 Implement per-file extraction error containment with warnings and empty results
- [x] 3.10 Add extractor golden tests against ported Graphify fixtures for each implemented language

## 4. Graph Build, Dedup, Cluster, and Analyze

- [x] 4.1 Implement fragment merge into igraph with per-file seen-id dedup and idempotent cross-file adds
- [x] 4.2 Implement raw-call resolution using a label-to-node index and common-name ambiguity suppression
- [x] 4.3 Implement semantic dedup with exact normalization, entropy gate, MinHash/LSH blocking, Jaro-Winkler thresholding, community boost, and union-find merge
- [x] 4.4 Implement community detection with igraph Leiden and Louvain fallback
- [x] 4.5 Implement centrality metrics, god-node ranking, and cross-community surprise analysis
- [x] 4.6 Add graph parity tests that compare native node and edge sets against reference Graphify corpora

## 5. Exports and One-Shot CLI

- [x] 5.1 Implement Graphify-compatible NetworkX node-link `graph.json` export
- [x] 5.2 Implement `graph.html`, `graph.svg`, Obsidian, Neo4j `cypher.txt`, and call-flow export support
- [x] 5.3 Implement one-shot CLI mode for full deterministic pipeline execution
- [x] 5.4 Add tests proving Graphify-compatible loaders can parse native `graph.json`
- [x] 5.5 Benchmark one-shot first-run time-to-first-graph against the Python Graphify reference

## 6. Daemon and Thin Client

- [x] 6.1 Implement project-root canonicalization and per-root daemon identity
- [x] 6.2 Implement Unix socket endpoint discovery and permissions for Linux and macOS
- [x] 6.3 Implement Windows named-pipe endpoint discovery and user-scoped DACLs — DEFERRED, not implemented: Windows paths are explicit stubs (daemon_server.cpp, daemon_endpoint.cpp, client_runtime.cpp); spec re-scoped to POSIX-only
- [x] 6.4 Implement length-prefixed JSON frames and protocol version negotiation
- [x] 6.5 Implement daemon operations for `query`, `path`, `explain`, `update`, `status`, and `shutdown`
- [x] 6.6 Implement thin client auto-spawn, bounded connect backoff, and spawn-race locking
- [x] 6.7 Implement single-writer graph mutation and immutable snapshot publication for concurrent reads
- [x] 6.8 Implement idle shutdown, clean endpoint cleanup, disk reload on startup, and periodic persistence
- [x] 6.9 Add daemon tests for spawn races, version mismatch, unauthorized peers, idle exit, and read-during-update consistency
- [x] 6.10 Benchmark daemon cold query-path latency against Python Graphify command calls

## 7. Incremental Updates

- [x] 7.1 Implement platform file watchers with debounce for code, docs, and media changes
- [x] 7.2 Implement two-tier cache classification using stat metadata and SHA256 content hashes
- [x] 7.3 Implement code-file re-extract, merge, prune, rename, and delete handling
- [x] 7.4 Implement neighborhood-scoped incremental dedup and periodic full dedup reconciliation
- [x] 7.5 Implement watcher overflow and explicit `update .` fallback to full stat-index rescan
- [x] 7.6 Add incremental tests for edit, rename, delete, touch no-op, overflow rescan, and clean full-build reconciliation

## 8. Semantic Fragment Ingest

- [x] 8.1 Implement semantic cache records keyed by content hash
- [x] 8.2 Implement chunk plan generation for uncached or stale docs, media, and semantic inputs
- [x] 8.3 Implement fragment drop-directory watching and `chunk_NN.json` discovery
- [x] 8.4 Implement semantic fragment validation before graph mutation
- [x] 8.5 Merge valid semantic fragments through the daemon single-writer path and update cache entries
- [x] 8.6 Surface enrichment states in `status`, including idle, pending, running, stale, and failed
- [x] 8.7 Add semantic ingest tests for valid merge, malformed rejection, cache hit skip, cache invalidation, and stale status

## 9. Host Integrations and MCP

- [x] 9.1 Write the host-agnostic skill contract for graph commands, chunk plans, fragment schema, and disk success signals
- [x] 9.2 Implement the hook-based reference integration using the thin client for agent-loop commands
- [x] 9.3 Implement the always-on reference integration using the host-agnostic skill contract
- [x] 9.4 Implement the MCP server over JSON-RPC stdio with daemon protocol forwarding
- [x] 9.5 Add integration tests for host command routing, semantic work dispatch, and MCP query forwarding

## 10. Safety, Fuzzing, and Experience Gate

- [x] 10.1 Add libFuzzer targets for each implemented per-language extractor under ASan/UBSan
- [x] 10.2 Add regression tests for malformed source files, invalid UTF-8, deep syntax trees, and oversized inputs
- [x] 10.3 Run parity, daemon, incremental, semantic ingest, and fuzz smoke tests in CI
- [x] 10.4 Compare native benchmarks against Python Graphify for cold query latency and time-to-first-graph
- [x] 10.5 Decide whether to proceed to long-tail language, exporter, and host integration fan-out based on the experience gate results
