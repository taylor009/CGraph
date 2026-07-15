## Why

The experience gate cleared on 2026-06-09 and its recorded Next Scope is "Add long-tail
deterministic language extractors only with golden fixtures and graph parity coverage", in
"separate changes, keeping the same gates in place" (docs/experience-gate-decision.md). Since
then no new language has landed — and the current Go behavior is defect-shaped: `detect.cpp`
maps `.go` to `DetectedLanguage::Go`, but neither dispatch switch handles it
(`tree_sitter_language_for` and `extract_non_grammar_language` both fall through), so every Go
file lands as an empty fragment with only the buried per-file warning "no extractor registered
for detected language" (file_extraction.cpp). A Go repo gets a graph of bare file nodes that
*looks* supported. Language breadth is also a headline comparison dimension for agent
code-intelligence tools in 2026 (Codebase-Memory parses 66 languages, arXiv 2603.27277).

## What Changes

- Vendor `tree-sitter-go` v0.25.0 (grammar version 15, no external scanner), pinned in
  `vendor/tree-sitter/PINS.md` and built into the grammar static lib.
- Add a declarative `go_config()` to `configured_extractors.cpp` and wire `DetectedLanguage::Go`
  into both dispatch switches. Named types (`type_spec`, `type_alias`) become `type` nodes;
  `function_declaration` + `method_declaration` become `function` nodes; `import_spec` paths
  become module stubs + `imports` edges (suffix-resolved, unresolved dropped); calls flow through
  `call_expression`, with `selector_expression` targets recorded as same-file member calls. No
  bespoke extractor .cpp — the shared configured walker handles Go, so IDs flow through the
  existing normalize.cpp contract untouched.
- Bump the persisted-index version key (`kIndexVersionKey` logic-1 -> logic-2) so a restarted
  daemon rebuilds instead of fast-loading a graph built by the pre-Go extractor.
- **Fail-loud coverage surfacing:** add `has_registered_extractor` / `unextracted_counts` to the
  extractor registry and a per-language `unextracted` map (detected files no extractor handles)
  to the daemon `status` payload, DaemonState (maintained by full rescans, the fast-load start,
  and incremental updates), and the one-shot `stats.json` — so a coverage hole can never again
  hide in a per-file warning. C# and PHP-Blade stay extractorless but become visible.

## Non-Goals

- C#/Rust extractors — the same template as separate follow-on changes.
- Go module resolution (`go.mod`-aware import mapping); non-project imports stay dropped stubs.
- Cgo, build tags, or generated-code (`*.pb.go`) special-casing.
- Fragment schema, ID normalization, graph.json shape, protocol, or MCP tool-list changes.

## Impact

- Affected specs: `deterministic-graph-pipeline` (Go extraction + unextracted counting),
  `graph-daemon-client` (status `unextracted` field).
- Affected code: vendor/tree-sitter/ (new grammar), src/engine/{configured_extractors.cpp,
  detect.cpp, non_grammar_extractors.cpp, index_persistence.cpp, pipeline.cpp,
  incremental_update.cpp, daemon_server.cpp, daemon_ops.cpp, operation_stats.cpp} + matching
  headers, tests/smoke/{extractor_goldens,configured_extractors,pipeline}_test.cpp, README.md.
- Behavior change: Go repos gain symbol/call/import nodes on the next (forced) rebuild; `status`
  and `stats.json` gain the `unextracted` map.
