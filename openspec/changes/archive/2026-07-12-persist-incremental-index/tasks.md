## 0. Profiling spike (settles the premise) — DONE, premise overturned

- [x] 0.1 Added env-gated phase timers around the seven `run_one_shot` stages; ran on the
      reference repo (5,702 files): `CGRAPH_PROFILE_PHASES=1 build/default/src/cli/cgraph --root <repo> --out <out>`.
- [x] 0.2 Recorded timings in `design.md` (see "Spike findings"). Extraction was NOT dominant:
      `merge_fragments` was 424s of a 452s build (84%) due to an O(fragments x nodes) index
      rebuild. Fixed at the source (separate change `fix-quadratic-fragment-merge`); build is
      now ~29s with extraction (~24s) the dominant phase. This change is re-scoped accordingly.
- [x] 0.3 Removed the throwaway timers.

Scope landed: **Tier 1 + Tier 3** (the high-value slice the spike justified). Tier 2 (reuse
per-file `ExtractionResult`s for a changed subset) is deferred — with a ~76s cold build dominated
by serial file hashing rather than extraction, Tier 1 (skip the rebuild entirely on an unchanged
restart) captures the dominant everyday win, and the Tier-2 serializer can follow if needed.

## 1. Version key — DONE

- [x] 1.1 `index_persistence_test` asserts the key is non-empty, carries the `cgraph-index-v1:`
      prefix, and is stable across calls.
- [x] 1.2 Implemented `index_version_key()` as `cgraph-index-v1:` + sha256 of the running
      executable (any recompile of the extractor changes the binary -> changes the key -> cache
      invalidated even on a byte-identical tree). Registered source + test in CMake.
- [x] 1.3 `ctest --preset default -R cgraph_index_persistence_test` (pass).

## 2. Manifest (de)serialization — DONE

- [x] 2.1 `index_persistence_test`: round-trip `IndexManifest` (version key + per-file
      `FileCacheEntry`) -> disk -> load yields an equal manifest, with explicit per-field checks
      (the file `modified_at` round-trip is the risky one).
- [x] 2.2 Implemented `write_index_manifest`/`read_index_manifest` (JSON under `cgraph-out/`,
      atomic temp+rename) and `tree_matches_manifest` (stat-first, hash-on-stat-miss classify;
      equal-count + all-hits => unchanged). Tier-2 full-index serialization deferred.
- [x] 2.3 Corrupt/truncated/missing manifest -> `nullopt` ("no usable cache"), no throw — tested.
- [x] 2.4 `ctest --preset default -R cgraph_index_persistence_test` (pass).

## 3. Tiered startup — DONE (Tier 1 / Tier 3)

- [x] 3.1 New `daemon_persistence_test`: restart over an unchanged tree serves a sentinel injected
      into the persisted `graph.json` (proves a disk load, not a rebuild); a source change discards
      the cache and rebuilds (sentinel gone, new symbol present). Tier-2 subset path not built.
- [x] 3.2 Wired the startup decision in `daemon_server.cpp`: `try_load_persisted()` (version match
      + `tree_matches_manifest` -> `load_graph_snapshot` + overlay drops) on the build thread,
      falling back to a full `rescan()` (Tier 3). Every rescan now persists `graph.json` + manifest.
- [x] 3.3 Async-serve preserved: the Tier-1 check / Tier-3 rebuild both run on the build thread;
      status/query answer immediately from the empty-then-published snapshot.
- [x] 3.4 Full suite `ctest --preset default` (55/55 pass).

## 4. Verification — DONE

- [x] 4.1 Production proof on the frontend repo: a daemon respawn served the persisted graph
      (21,376 nodes) within ~1s vs a ~76s cold build; a real `query` returns ranked results.
- [x] 4.2 Full suite green (55/55).

## 5. Follow-ups (not in this change)

- Daemon cold build is ~76s, now dominated by serial sha256 hashing of every file in
  `full_stat_index_rescan` (not extraction). Parallelize that pass (same pattern as extraction).
- Tier 2 (reuse per-file extraction for a changed subset) if partial-change restarts become hot.
