## 1. Parallel extraction helper

- [x] 1.1 Added a `file_extraction_test` case: 129 multi-language files extracted via
      `extract_files`, each result asserted equal to serial `extract_detected_file` in order,
      plus the empty-batch case (order-preservation + parity + thread-safety under contention).
- [x] 1.2 Implemented `extract_files(span<const DetectedFile>)`: worker count = min(hardware
      concurrency, file count); workers pull an atomic index and write their own result slot;
      serial fallback for <=1 worker / empty input.
- [x] 1.3 `ctest --preset default -R cgraph_file_extraction_test` (pass).

## 2. Wire into the pipeline and daemon

- [x] 2.1 `run_one_shot` uses `extract_files`, concatenating warnings/raw_calls/raw_relations in
      result order.
- [x] 2.2 `full_stat_index_rescan` uses `extract_files` (index maps are key-sorted by
      `rebuild_graph`, so result order is irrelevant there).
- [x] 2.3 Full suite `ctest --preset default` (53/53 pass).

## 3. Verify end-to-end

- [x] 3.1 Cold build on the reference repo: `graph.json` byte-identical to the serial build
      (`diff -q` IDENTICAL; 26,914 nodes / 77,330 links).
- [x] 3.2 A/B on the extraction phase (same binary, 5,702 files): serial 22,007.8 ms ->
      parallel (8 cores) 6,746.7 ms = 3.26x faster. Daemon cold-build pipeline work ~28.7s -> ~12s.
      (Allocator contention caps linear scaling; tree-sitter is alloc-heavy.)
