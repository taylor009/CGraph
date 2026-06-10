## Spike findings (task 0)

Phase timing on the reference repo (5,702 files, 26,914 nodes / 77,330 links), before/after the
`merge_fragments` fix:

```
                 BEFORE          AFTER
detect            1,060 ms        910 ms
extract          22,594 ms     23,682 ms    ← now dominant (82%)
merge           424,126 ms        296 ms    ← was 84% of the build; O(N^2) bug
resolve           1,842 ms      1,685 ms
dedup             2,004 ms      1,885 ms
communities         199 ms        164 ms
analyze             149 ms        152 ms
TOTAL            ~452 s         ~28.7 s      ← 15.7x faster, byte-identical output
```

The premise (extraction-dominated, persistence is the lever) was wrong. The real cost was a
quadratic in `merge_fragments` (`graph_builder.cpp`): the per-fragment merge rebuilt the dedup
index from the whole accumulated graph on every file. Fixed at the source (its own change,
`fix-quadratic-fragment-merge`) — maintain the index once across all fragments.

Consequences for THIS change:
- Cold build is now ~29s, not ~8.4 min. Persistence drops from urgent to a "restart fast path".
- Tier-2 floor (merge/resolve/dedup/analyze, post-fix) is ~4s — small; Tier 1 (skip rebuild via
  `graph.json`) carries most of the remaining win on an unchanged restart.
- The bigger remaining lever is now parallelizing extraction (~24s, 82%), tracked separately.
- This proposal stays valid but lower-priority; revisit scope before implementing tiers.

## Context

`run_one_shot` (`pipeline.cpp:34`) and `full_stat_index_rescan` (`incremental_update.cpp:74`)
both extract every detected file serially. The daemon calls the latter on start
(`daemon_server.cpp:261`). The post-extraction phases are not the bottleneck: `resolve_imports`/
`resolve_raw_calls`/`resolve_raw_relations` use pre-built `unordered_map` indexes
(`graph_builder.cpp:57,121,265,399`) and `semantic_dedup` uses MinHash+LSH banding so pairwise
comparison stays within candidate buckets (`dedup.cpp:112-136,255-273`). The cost is the parse.

The incremental engine rebuilds from `IncrementalGraphIndex` (`incremental_update.hpp:18`), a
map of per-file `ExtractionResult`. `rebuild_graph` (`incremental_update.cpp:25`) sorts file keys,
concatenates fragments, then merge/resolve/dedup/analyze. `graph.json` (the persisted artifact)
is the *merged, resolved, deduped, ID-normalized* product — lossy w.r.t. per-file provenance, so
it can seed serving but not incremental rebuild. The index that can seed rebuild is in-memory only.

## Goals / Non-Goals

- Goal: eliminate re-parsing on restart when the tree is unchanged or barely changed.
- Goal: never serve a graph built by a stale extractor; invalidation must be correct, not best-effort.
- Goal: tiered start produces a graph byte-identical to a cold build (parity contract held).
- Non-goal: parallel extraction (separate change); protocol/op/MCP changes; output-shape changes.

## Decisions

### Persist the extraction index, not just the graph

`load_graph_snapshot` already exists for serving, but incremental rebuild needs the per-file
`ExtractionResult`. Persist the full `IncrementalGraphIndex` (fragments, raw calls, raw relations,
`FileCacheEntry` per file, aliases). A reused cached fragment is byte-identical to a freshly
extracted one for the same content, so `rebuild_graph` over cached+fresh fragments yields the
same merged graph as a cold build (key sort at `incremental_update.cpp:36` keeps order
deterministic). This is what preserves parity for free.

### Version key gates the whole cache

Compute a content-addressed key over everything that affects extraction output: extractor
build identity, language config / configured-extractor set, and ID-normalization rules. Store it
in the cache header. On load, mismatch ⇒ discard the entire cache ⇒ Tier 3. This is mandatory:
files unchanged + extractor upgraded is the exact case where mtime/hash diffing says "nothing
changed" while the cached graph is wrong. Rationale aligns with the no-silent-staleness rule.

### Tiered start, escalating by cost

- Tier 1: `detect_project_files` + stat/hash classify against the cache. Zero changes ⇒
  `load_graph_snapshot(graph.json)`; serve; skip extraction and rebuild entirely.
- Tier 2: load index; for each detected file, `StatHit`/`HashHit` ⇒ reuse cached `ExtractionResult`,
  else `extract_detected_file`; drop cached files no longer detected; `rebuild_graph`; persist.
- Tier 3: cache absent / version mismatch / parse failure ⇒ `full_stat_index_rescan` (today's path).

Stat-first, hash-on-stat-miss: `git checkout` rewrites mtimes without changing content, so the
`HashHit` fallback (already modeled in `file_cache`) prevents a branch switch from re-extracting
the whole tree. Startup must use the hash fallback, not stat alone.

### Storage location and format

Cache artifact lives under the project output dir (`cgraph-out/`) alongside `graph.json` and
`semantic-cache.json`, written atomically via temp+rename (the pattern at `daemon_lifecycle.cpp:136`).
Format starts as JSON for consistency and debuggability; the benchmark spike (Task 0) measures load
time and on-disk size against re-extraction cost. If JSON load is not decisively cheaper than
re-parsing, switch to a compact binary encoding before fixing the format — decision recorded here.

## Risks / Trade-offs

- **Standalone rebuild floor (Tier 2 cost).** Tier 2 still runs merge/resolve/dedup/analyze over
  the whole graph every start. If that floor is large on 69k edges, Tier 1 (skip rebuild) carries
  the win and Tier 2's value narrows to "few files changed." The spike measures this floor; the
  tier ladder is structured so Tier 1 already captures the dominant everyday case regardless.
- **Cache/disk drift.** A cache written by a crashed mid-write daemon must never be half-loaded;
  atomic temp+rename plus a parse-failure ⇒ Tier 3 fallback covers this. A corrupt cache is a
  full rebuild, never a wrong graph.
- **Index size.** Per-file fragments for a large repo may be tens of MB. Acceptable if load is
  still seconds vs. minutes to re-parse; validated by the spike, with binary format as the lever.

## Test Strategy

Per `config.yaml` TDD rules, every behavior task lands its test first.

- `index_persistence_test`: round-trip `IncrementalGraphIndex` → disk → load yields an equal index;
  rebuild from the loaded index equals rebuild from the original (graph equality).
- Version-key invalidation: a cache with a mismatched version key is rejected and triggers Tier 3.
- Tier 1: unchanged tree + valid cache ⇒ no `extract_detected_file` calls, graph served from disk.
- Tier 2: change/add/remove a file ⇒ only changed files re-extracted; resulting merged graph is
  **equal to a cold `run_one_shot`** on the same tree (the parity assertion).
- Corrupt cache: truncated/garbage cache file ⇒ clean fall to full rebuild, valid graph, no crash.
- `daemon_server_test` extension: restart the server against an existing cache and assert the
  startup path took the fast tier (e.g. via a status/metrics signal) while still serving correct
  query results.
- Benchmark spike (recorded command, not a gating unit test): phase timers around the seven
  `run_one_shot` stages on the frontend repo to confirm extraction dominance and capture the
  merge/resolve/dedup/analyze floor.

Validation that cannot be a pure unit test (real cold-vs-warm start latency on a large repo) is
recorded as an explicit benchmark command in tasks, run on the 1,251-file reference repo.
