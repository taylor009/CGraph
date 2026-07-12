## 1. Cache-aware rescan + verify

- [x] 1.1 Update `incremental_rescan_test`: after removing one file and adding another, a second
      `full_stat_index_rescan` re-extracts only the new file (`files_reextracted == 1`, not 2) and
      reuses the unchanged one, with an identical resulting graph.
- [x] 1.2 Make `full_stat_index_rescan` classify each detected file against `index.cache`; reuse
      `index.files[key]` for StatHit/HashHit, re-extract only changed/new files (in parallel).
- [x] 1.3 Full suite `ctest --preset default` (55/55).

## 2. Tier-2 measurement (rejected)

- [x] 2.1 Prototyped + benchmarked a persisted per-file index (`index-cache.json` + startup load).
      39,114-node repo: Tier-3 cold 18.8s vs Tier-2 restart 22.0s (net loss); 144 MB index file.
      Reverted — recorded as a Non-Goal in the proposal so it is not re-attempted without a
      materially faster (binary) serialization that beats parallel re-extraction.
