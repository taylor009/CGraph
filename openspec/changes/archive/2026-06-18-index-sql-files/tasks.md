## 1. Detect .sql

- [x] 1.1 `detect.hpp`: added `Sql` to `DetectedLanguage`. `detect.cpp`: `.sql` → `DetectedLanguage::Sql`.
- [x] 1.2 `detect_test.cpp`: `migration.sql` detects as `Sql` (so it classifies as `Code`, not a
      `Document`).

## 2. File-level extractor

- [x] 2.1 `non_grammar_extractors.cpp`/`.hpp`: added `extract_sql(context)` emitting exactly one node
      (`kind = "sql_file"`, `label =` filename, `source_file =` path, no symbols/edges, no
      `source_location`); added the `DetectedLanguage::Sql` case to `extract_non_grammar_language`.
- [x] 2.2 `non_grammar_extractors_test.cpp`: a `.sql` file yields exactly one `sql_file` node with the
      expected label/source_file and no edges.

## 3. Verify

- [x] 3.1 Parity goldens unchanged: `extractor_goldens` + `pack_context_parity` pass.
- [x] 3.2 Full suite `ctest --preset default` → 60/60.
- [x] 3.3 Live: rebuilt `turing` → 1,979 files (was 1,565), **414 `sql_file` nodes** (was 0), 8,056
      nodes total, 2.4 s. Samples: `check_threads.sql`, `migration.sql`.