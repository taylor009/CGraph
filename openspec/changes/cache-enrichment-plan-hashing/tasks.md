## 1. Fix 1 — stat-cache the plan walk

- [x] 1.1 `semantic_chunk_plan_test` (red): add `files_hashed` / `files_stat_reused` assertions —
      first plan over N docs with an empty index hashes all (`files_hashed == N`,
      `files_stat_reused == 0`); a second plan reusing the index with no changes reuses all
      (`files_hashed == 0`, `files_stat_reused == N`) and produces identical
      chunks/cache_hits/stale_inputs; touching one file's mtime re-hashes only that file.
- [x] 1.2 Add `files_hashed` and `files_stat_reused` to `SemanticChunkPlan`. Add a
      `SemanticStatIndex` (`unordered_map<std::string, FileCacheEntry>`) parameter to
      `plan_semantic_chunks`; classify each path via `classify_cached_file`, reuse the stored hash
      on `StatHit`, hash otherwise, and update the index. Feed the resulting hash to the existing
      content-hash cache lookup unchanged.
- [x] 1.3 Update `semantic_orchestration.cpp` (`plan_enrichment`) and any other callers to pass a
      stat index (empty for the cold CLI one-shot).

## 2. Fix 1 — persist the stat index

- [x] 2.1 Test (red): write a `SemanticStatIndex`, read it back, and confirm a subsequent plan
      reuses all entries; an absent/unreadable index file is treated as empty (cold) with no error.
- [x] 2.2 Add `write_semantic_stat_index` / `read_semantic_stat_index` mirroring
      `write_semantic_cache`/`read_semantic_cache` (serialize `file_time_type` as integer ticks).
      Register any new source file in `src/engine/CMakeLists.txt` + matching `_test.cpp`.
- [x] 2.3 Daemon: load the index next to the semantic cache at startup
      (`daemon_server.cpp:183-184`) and rewrite it whenever a plan changed it.

## 3. Fix 2 — prune redundant refresh triggers

- [x] 3.1 Test (red): a code-only `update .` rescan does not trigger an enrichment re-plan (no
      doc-tree walk); a doc/media change does; the initial plan runs exactly once at startup.
- [x] 3.2 Gate the refresh triggers: keep a single startup plan; drop the per-rescan
      `request_refresh()` for code-only `update .` (`daemon_server.cpp:325`); ensure the
      doc/media-change path (`daemon_server.cpp:505`) remains the steady-state trigger. Reuse the
      existing `docs_changed`/`code_changed` split.

## 4. Verify

- [x] 4.1 Full suite `ctest --preset default` (report pass/total).
- [x] 4.2 Benchmark: a fixture with several large media/PDF docs. Record `files_hashed` and bytes
      read on the second refresh over an unchanged tree (must be 0 read / 0 hashed for unchanged
      files), before vs after. Record the exact command in the PR.
- [x] 4.3 End-to-end: run a live daemon over a doc-bearing repo; confirm an `update .` after a
      code edit does not re-walk/re-hash docs, and a doc edit does. Capture `status` enrichment
      counts before/after.
