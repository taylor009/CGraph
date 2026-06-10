## Why

After fixing the quadratic merge, per-file tree-sitter extraction is the dominant cold-build
phase: ~24s of a ~29s build (82%) on a 5,702-file reference repo. The extraction loop in
`run_one_shot` and the daemon's `full_stat_index_rescan` is strictly serial, yet the parser pool
is already `thread_local` by design (`parser_pool.cpp`) and the extraction data path has no
mutable static state — `extract_detected_file` is pure per file. The work is embarrassingly
parallel and the infrastructure already assumes it.

## What Changes

- Add `extract_files(span<const DetectedFile>) -> vector<ExtractionResult>` that extracts files
  concurrently across a bounded worker pool and returns results in input order.
- Use it in both `run_one_shot` (`pipeline.cpp`) and `full_stat_index_rescan`
  (`incremental_update.cpp`) in place of the serial loop.
- Preserve byte-identical output: results are written into pre-sized slots by index and merged in
  the same order as the serial path, so node/edge ordering and the Graphify parity contract are
  unchanged.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `deterministic-graph-pipeline`: extraction across detected files MAY run concurrently while
  producing a result sequence identical to serial extraction.

## Non-Goals

- Parallelizing merge/resolve/dedup/analyze (post-extraction phases are now <6s combined).
- Changing extraction logic, ID normalization, or output shape — parity is held.

## Impact

- `src/engine/file_extraction.cpp` (+`extract_files`), `pipeline.cpp`, `incremental_update.cpp`,
  `tests/smoke/file_extraction_test.cpp`.
- Worker count bounded by hardware concurrency and file count; degrades to serial for tiny inputs.
- Verified by: order-preservation/parity unit test (parallel result equals serial per file), full
  suite, and a cold-build benchmark on the reference repo (expect ~24s extraction -> single digits).
