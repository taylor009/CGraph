# Tasks — add-go-extractor

## 1. Grammar

- [x] 1.1 Vendor tree-sitter-go v0.25.0 (`1547678a9da59885853f5f5cc8a99cc203fa2e2c`) under
      vendor/tree-sitter/grammars/go; add the PINS.md row
- [x] 1.2 Register grammars/go/src/parser.c + include dir in vendor/tree-sitter/CMakeLists.txt

## 2. Extraction

- [x] 2.1 `go_config()` + `go_import_handler` in src/engine/configured_extractors.cpp; extern
      `tree_sitter_go()`; `case DetectedLanguage::Go` in both dispatch switches
- [x] 2.2 Bump `kIndexVersionKey` to `cgraph-index-v1:logic-2` (src/engine/index_persistence.cpp)

## 3. Fail-loud coverage

- [x] 3.1 `language_name()` (detect), `handles_non_grammar_language()` (non_grammar_extractors),
      `has_registered_extractor()` + `unextracted_counts()` (configured_extractors)
- [x] 3.2 `BuildStats.unextracted` populated in run_one_shot; emitted in build_stats_json
- [x] 3.3 `DaemonState.unextracted` populated by full_stat_index_rescan and the fast-load start,
      adjusted by apply_incremental_code_updates, emitted in the `status` payload

## 4. Tests (gates)

- [x] 4.1 Go golden case in tests/smoke/extractor_goldens_test.cpp
- [x] 4.2 Go walker assertions (types, alias, method, function, imports, plain + member calls) and
      registry/coverage assertions in tests/smoke/configured_extractors_test.cpp
- [x] 4.3 pipeline_test: one-shot over py+go+cs — Go symbols present, `unextracted == {csharp: 1}`
      in stats and stats.json
- [x] 4.4 Full suite: 61/62 passing on `default` and `sanitizers` presets. The one failure
      (`cgraph_pack_context_parity_test`) fails identically on clean origin/main on this Linux
      machine — pre-existing, unrelated (verified via a baseline worktree at 1cc1052)

## 5. Verification & docs

- [x] 5.1 One-shot over github.com/pkg/errors (10 files): 102 nodes / 209 edges in 71 ms —
      80 functions (Cause, As, Errorf, ...), 12 types (Frame, StackTrace, fundamental, ...),
      114 CALLS edges; `unextracted: {}`. Daemon + client: `query Cause` -> entity route,
      function Cause @ errors.go:160; status reports node_count 102, build_state ready,
      unextracted {}
- [x] 5.2 README.md language list includes Go

## Build fixes (pre-existing on main, required to compile this branch)

- [x] analysis.cpp missing `<memory>` include; cli/main.cpp duplicate `std::error_code ec` on the
      Linux path; daemon_lifecycle_test.cpp designated-initializer field order
