## Why

A dogfood build of a real production repo (`turing`, ~361k LOC TS) surfaced a concrete gap: of its
**414 `.sql` files** (Prisma migrations + schema — `CREATE TABLE`/`CREATE TYPE`/`ALTER TABLE`), the
graph contains **zero nodes**. `.sql` is entirely unknown to detection: `classify_watched_file`
sees `detect_language(.sql) == Unknown`, so SQL is treated as a `Document` and never enters the
deterministic graph (Documents only become nodes via host enrichment). For a SQL-backed backend, the
graph is blind to the entire data layer, and that layer is disconnected from the TS code built on it.

The cheap, staged fix is **file-level** indexing: recognize `.sql` and emit one node per file. That
makes the 414 files discoverable (`query` / `file:` search), enrichable (the semantic layer can
attach docs), and seam-anchorable — without parsing SQL. It mirrors how the engine already indexes
other non-grammar languages (McpConfig, MsBuild) via `extract_non_grammar_language`. Symbol-level
extraction (tables/columns/enums as nodes, linked to Prisma models) is the natural follow-up, scoped
out here.

## What Changes

- Add `DetectedLanguage::Sql`; map the `.sql` extension to it in `detect_language` (`detect.cpp`).
- Add a non-grammar handler `extract_sql` that emits a single **file-level node** per `.sql` file:
  `kind = "sql_file"`, `label =` the file's name, `source_file =` its path, no symbols, no edges.
  Wire it into `extract_non_grammar_language`'s dispatch.
- As a consequence, `.sql` classifies as `Code` (not `Document`), so the daemon watcher folds `.sql`
  changes into the graph incrementally, and detection includes `.sql` files.

## Capabilities

### Modified Capabilities

- `deterministic-graph-pipeline`: SQL files are indexed as file-level nodes in the deterministic
  graph (one `sql_file` node per `.sql`), making the data layer discoverable, enrichable, and
  seam-anchorable.

## Non-Goals

- **Symbol-level SQL extraction** — parsing `CREATE TABLE`/`TYPE`/`ALTER` into table/enum/column
  nodes + edges. That is the (b) follow-up; this ships file-level only.
- **Linking SQL to Prisma/TS models** — connecting a table node to its `@@map`'d TS model is part of
  the symbol-level follow-up.
- **SQL dialect parsing** — no SQL is parsed; the node represents the file, not its contents.

## Impact

- `src/engine/include/cgraph/detect.hpp` (enum value), `src/engine/detect.cpp` (extension map),
  `src/engine/non_grammar_extractors.cpp` + `.hpp` (`extract_sql` + dispatch),
  `tests/smoke/detect_test.cpp`, `tests/smoke/non_grammar_extractors_test.cpp`.
- **Measured effect (turing):** 414 `.sql` files → 414 `sql_file` nodes (today: 0).
- **Parity:** the extractor goldens contain no `.sql` fixtures, so goldens are unaffected (confirmed
  by running them). Repos with `.sql` files gain nodes — an additive graph change, not a regression
  of existing extraction.
- Verified by: detection maps `.sql` → `Sql`; the extractor emits exactly one `sql_file` node per
  file; a turing rebuild shows 414 `sql_file` nodes; goldens unchanged; full suite.
