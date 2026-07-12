## Context

The enrichment planner is content-addressed: to decide whether a doc/media file already has a
valid semantic fragment, it computes the file's SHA-256 and looks it up
(`semantic_chunk_plan.cpp:106-108`). That forces a full read + hash of every file on every plan
run. The plan runs on a worker thread after every build, fast-load, drop ingest, and doc/media
change (`daemon_server.cpp:325,358,459,505`), so unchanged files are re-hashed repeatedly. The
deterministic code path already avoids this with a stat-then-hash cache; this change applies the
same primitive to enrichment planning and prunes the redundant triggers.

## Reusing the existing primitive

`classify_cached_file(path, previous)` (`file_cache.cpp:155-184`) is exactly the needed logic:

```
stat(path) -> {size, mtime}
  same_stat(current, previous)?  -> StatHit: current.sha256 = previous.sha256   (NO read)
  else                            -> read + sha256_file_hex; New / HashHit / Stale
```

`FileCacheEntry{path, size, modified_at, sha256}` is the per-file record. The enrichment path
will hold a map of these keyed by path — the same shape as `IncrementalGraphIndex.cache` on the
code side.

## Fix 1 — stat-cache the walk

`plan_semantic_chunks` gains a `SemanticStatIndex&` parameter (a
`std::unordered_map<std::string, FileCacheEntry>` keyed by path string). Per semantic path:

```cpp
auto prev = index.find(key); // optional
auto classification = classify_cached_file(path, prev ? prev->second : std::nullopt);
if (classification.state == Deleted) continue;        // vanished between walk and stat
const std::string hash = classification.current->sha256;  // reused on StatHit, fresh otherwise
index[key] = *classification.current;                  // refresh the stat entry
// ... existing content-hash cache lookup with `hash`, unchanged downstream ...
```

Two observability counters are added to `SemanticChunkPlan` alongside `cache_hits`/`stale_inputs`:
- `files_hashed` — files actually read + SHA-256'd this plan
- `files_stat_reused` — files whose hash came from a `StatHit` (the win)

These make the optimization directly testable (no mocking): a second plan over an unchanged tree
must report `files_hashed == 0`, `files_stat_reused == N`.

The CLI one-shot `enrich-plan` passes a fresh (or disk-loaded) index; cold, every file is `New`
and hashed exactly as today.

## Persistence

A `SemanticStatIndex` serializer mirrors `write_semantic_cache`/`read_semantic_cache`
(`semantic_cache.cpp:103-114`), persisting to `drop_dir/semantic-stat-index.json`. The daemon
loads it next to the cache at startup (`daemon_server.cpp:183-184`) and rewrites it whenever a
plan changes it (a new/changed file was hashed). `file_time_type` is serialized as a stable
integer (ticks since epoch) — the same value `same_stat` compares.

## Fix 2 — prune redundant triggers

The plan must run once after the initial build/fast-load to populate the pending counts, then
only when docs/media actually change. The serve loop already classifies a watcher batch into
`code_changed` / `docs_changed` (`daemon_server.cpp:465-472`) and only calls `request_refresh()`
on `docs_changed` (line 505). The redundant triggers are:

- line 325 — after every `update .` rescan (a code-only operation)
- line 358 — after a fast-load (legitimately the *initial* plan, but only once)

Plan: keep a single startup refresh (gate 325/358 so the post-build refresh fires only for the
first build after start, or move the initial refresh to an explicit one-shot at startup), and
drop the per-rescan refresh for code-only `update .`. Net: a pure code build/rescan never walks
the doc tree; doc/media edits still re-plan.

## Test strategy

- `semantic_chunk_plan_test` (red first), using the new counters:
  - First plan over a fixture with N docs and an empty index: `files_hashed == N`,
    `files_stat_reused == 0`.
  - Second plan reusing the returned index, no changes: `files_hashed == 0`,
    `files_stat_reused == N`, and `chunks`/`cache_hits`/`stale_inputs` identical to the first.
  - Touch one file (bump mtime): exactly that file is re-hashed; the rest reuse; the plan output
    (hashes, chunk membership) is unchanged because content is unchanged.
  - Persistence round-trip: write the index, read it back, a subsequent plan reuses all.
- Daemon-level (reuse the existing enrichment/daemon test): a code-only `update .` does not
  increment the plan count; a doc/media change does. Initial plan runs exactly once at startup.
- Benchmark (recorded command): a fixture with several large media/PDF files; capture
  `files_hashed`/bytes read on the second refresh — must be 0 read for unchanged files.

### Correctness note (stated, not hidden)

`StatHit` reuses the stored hash without re-reading, so a same-second, same-size edit could reuse
a stale hash for one refresh cycle. This is the identical tradeoff the deterministic code path
already accepts repo-wide (`same_stat` compares size + mtime only). The content-hash cache still
reconciles on the next stat miss, and a real edit almost always changes size or mtime. This
change does not widen the risk; it applies the same accepted policy to enrichment.

## Open questions

- Whether to fold the stat metadata into `SemanticCacheRecord` instead of a separate index.
  Rejected for now: pending (unfulfilled) files have no cache record, so the index must track
  files the cache does not — a separate map is cleaner and keeps the content cache purpose-built.
- Exact gating mechanism for the initial-plan-once behavior (a `bool initial_plan_done` vs moving
  the call) — settled during implementation; the contract is "once at startup, then doc/media
  changes only."
