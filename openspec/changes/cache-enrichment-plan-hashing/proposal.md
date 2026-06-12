## Why

`plan_semantic_chunks` re-reads and SHA-256-hashes **every** doc/media file on **every**
refresh, because the semantic cache is content-addressed — `cache.find_by_content_hash(hash)`
needs the hash just to discover whether the file is already cached
(`semantic_chunk_plan.cpp:106-108`). The refresh fires after every build, every fast-load, every
`update .` rescan, and every doc/media change (`daemon_server.cpp:325,358,459,505`). So an
unchanged repo re-hashes its entire doc/media set on every code build, forever, for a layer that
may have no host fulfilling it.

On this (text-only) repo that is ~509 KB across 108 files — negligible. But it scales linearly
with doc/media bytes: a design folder of PDFs, or media assets, re-hashes MB→GB on every code
build. It is pure waste, and the deterministic code path already solved this exact problem:
`classify_cached_file` stats first (size + mtime) and reuses the stored hash on a `StatHit`,
reading + hashing only on a stat miss (`file_cache.cpp:155-184`). The enrichment planner simply
does not use it.

## What Changes

- **Fix 1 — stat-cache the plan walk (behavior-identical).** Introduce a `SemanticStatIndex`
  (`path → {size, modified_at, sha256}`) and thread it through `plan_semantic_chunks`. For each
  semantic path, classify against the index via the existing `classify_cached_file`: a `StatHit`
  reuses the stored hash with no file read; only new/changed files are read and hashed. The
  resulting hash feeds the content-hash cache lookup exactly as today, so the produced plan is
  identical — only the recompute is skipped.
- **Persist the index** at `drop_dir/semantic-stat-index.json`, mirroring how the semantic cache
  persists at `drop_dir/semantic-cache.json`. Loaded at daemon start, rewritten when it changes,
  so a restart re-hashes once (cold) and stays cheap thereafter.
- **Fix 2 — stop re-planning after code-only builds.** The plan runs once after the initial
  build/fast-load to populate the pending counts; thereafter a refresh fires only when the
  watcher reports doc/media changes (the serve loop already separates `docs_changed` from
  `code_changed`, `daemon_server.cpp:465-472`). A pure code `update .` rescan no longer walks the
  doc tree at all.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `semantic-fragment-ingest`: enrichment planning reuses the stored hash for unchanged doc/media
  files (stat-gated) instead of re-hashing them, persists that stat index across restarts, and
  re-plans only on doc/media changes rather than on every code build — while producing the same
  chunk plan it does today.

## Non-Goals

- **Not activating enrichment.** This change reduces the cost of the dormant planning loop; it
  does not author fragments or populate the semantic layer.
- **No change to the content-hash cache semantics, the fragment schema, or validation.** The
  content-addressed `SemanticCache` keeps keying by SHA-256; this change only avoids recomputing
  that SHA-256 for unchanged files.
- **No change to the produced plan.** For a given tree + cache, the chunk set, cache-hit count,
  and stale count are identical whether a hash was reused or recomputed.

## Impact

- `src/engine/semantic_chunk_plan.{hpp,cpp}` (stat-index param + reuse counters on
  `SemanticChunkPlan`), a new `SemanticStatIndex` + persistence (likely alongside
  `semantic_cache.cpp` or a small `semantic_stat_index.cpp`), `src/engine/daemon_server.cpp`
  (load/persist the index; gate the refresh triggers), `src/engine/semantic_orchestration.cpp`
  (CLI `enrich-plan` passes an empty/loaded index).
- New persisted artifact `drop_dir/semantic-stat-index.json` (tiny: path/mtime/size/hash per
  doc/media file).
- Tests: extend `semantic_chunk_plan_test` (unchanged file not re-hashed across plans, changed
  file re-hashed, plan output identical either way, index round-trips through persistence) and a
  daemon-level check that a code-only rescan does not re-plan while a doc change does.
- Benchmark: bytes read / files hashed per refresh on a doc/media-heavy fixture, before vs after.
