## 1. Seam spec + fragment generation (engine)

- [x] 1.1 `seam_test.cpp`: a valid spec (provider, 2 services, 1 schema, 1 endpoint, 1 consumes, 1
      mirror) + a fixture graph produces the expected nodes (`service:`/`endpoint:`/`schema:` ids +
      `code-ref` shadows whose ids equal the resolved consumer node ids) and the five contract edges;
      anchor resolution picks the smallest-span node (scoreModel 40-50, not the file-spanning 1-100).
- [x] 1.2 `seam.cpp` / `seam.hpp`: parse + validate the spec, load consumer graphs (purpose-built
      reader — `parse_node_link_graph` is file-local to the daemon and reusing it would refactor the
      load path for fields seam never needs), resolve anchors (smallest non-`file` node whose
      `source_file` ends with the anchor path and span contains the line), build a `Fragment` with the
      contract nodes/edges + shadow code-refs, insertion-ordered dedup by id (deterministic emission).
- [x] 1.3 Registered `seam.cpp` in `src/engine/CMakeLists.txt` and `cgraph_seam_test` in
      `tests/smoke/CMakeLists.txt` (warnings + sanitizers via the cmake helpers).

## 2. Fail-loud validation

- [x] 2.1 `seam_test.cpp`: each error path returns `ok=false` with an empty fragment — unresolved
      anchor (line 999), `consumes` → unknown endpoint, `mirror` → unknown schema, `call_site` naming
      a graph not supplied, and a malformed spec (missing `provider`).
- [x] 2.2 Implemented as hard errors with precise messages; the generator returns an error result and
      writes nothing.

## 3. Ingestable output

- [x] 3.1 `seam_test.cpp`: the emitted fragment passes `validate_semantic_fragment_json`.
- [x] 3.2 `cgraph seam gen` serializes the `Fragment` to `chunk_00.json` in `--out` via the existing
      fragment JSON path and writes the per-anchor resolution log to stderr.

## 4. CLI subcommand

- [x] 4.1 `cli/main.cpp`: added `seam gen --seam SPEC --graphs NAME=graph.json[…] --out DROPDIR`
      (repeatable `--graphs`), dispatched like `enrich-plan`/`enrich-ingest`; usage line added.
- [x] 4.2 Live smoke: spec + fixture graph → `cgraph seam gen` wrote a 6-node/5-edge fragment;
      `cgraph enrich-ingest` accepted it (`1 fragment merged, 0 rejected` → 6 nodes).

## 5. Verify

- [x] 5.1 `ctest --preset default -R cgraph_seam_test` → passed.
- [x] 5.2 Full suite `ctest --preset default` → 60/60; parity goldens unchanged (seam touches no
      extraction/export parity surface).
