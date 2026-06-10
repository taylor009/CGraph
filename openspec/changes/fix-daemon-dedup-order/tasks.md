## 1. Reorder + verify

- [x] 1.1 Strengthen `incremental_update_test` parity check: daemon rescan graph must match a
      `run_one_shot` graph on node/edge counts (not just labels), and carry `degree_centrality`
      (guards the ordering — over-merge would diverge the counts).
- [x] 1.2 Move `detect_communities` + `analyze_graph` out of `rebuild_graph` into `finalize_graph`;
      call it after dedup in both `full_stat_index_rescan` and `apply_incremental_code_updates`.
- [x] 1.3 Persist graph + manifest before enrichment planning; log the Tier-1 load before planning.
- [x] 1.4 Full suite `ctest --preset default` (55/55 pass).

## 2. Profiling + at-scale verification (recorded)

- [x] 2.1 Phase-timed the daemon cold rescan (env-gated, throwaway, since removed): dedup was
      62.7s of 71s; extract 4s, hash+index 1.5s, rebuild 2.2s.
- [x] 2.2 After the fix on the reference repo: daemon cold build ~11s; daemon node_count 26,914 /
      edge_count 77,330 == canonical CLI build (`cgraph --root … --out …`), which was a wrong 21,376.
- [x] 2.3 Tier-1 restart still works against the corrected cache: "loaded persisted graph
      (5702 files unchanged)", node_count 26,914 in ~4.6s vs ~11s cold.
