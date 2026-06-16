# Graph session memory: observability

## Why

graph-session-memory is now durable (survives restart, incremental edits, rescans), but it is
barely measurable. The investigation found:

- `remember`/`recall` already appear in **live** `status.ops.lifetime` with `count` + `mean_ms`
  (the lifetime loop iterates every op — `operation_stats.cpp:130-138`). Live-only, resets each
  boot.
- They are **excluded from the durable ledger**: `kSubstantiveOps` is
  `{Query,Path,Explain,Impact,Context}` (`operation_stats.cpp:170-171`), so memory activity is
  absent from `op-stats-ledger.jsonl` and the cross-session rollup — no historical trend.
- **Recall zero-hits are computed but dropped**: the Recall dispatch sets `zero_hit`
  (`daemon_ops.cpp:1385`), but `record()` only persists zero-hits for `Query`/`Context`
  (`operation_stats.hpp:154-164`) — there is no `recall_zero_hits`.
- `status` has **no memory inventory**: its `semantic` block counts doc/concept nodes, nothing
  about `memory:` checkpoints, sidecars, or overlay re-applies.

For a feature meant to let agents `/clear` aggressively, "how much is memory used / does recall
hit" is exactly what we need to watch — and we can't, durably.

## What Changes (v1)

- **Add a `memory` block to `status`** (assembled in `status()`, which already has the snapshot
  and `state`):
  - `checkpoint_count` — `memory:` checkpoint nodes in the live snapshot.
  - `sidecar_count` — `*.json` sidecars under `state.memory_dir`.
  - `recall_count` — lifetime `recall` op count.
  - `recall_zero_hits` — recalls that returned nothing (new counter, below).
  - `last_remember_at` / `last_recall_at` — ISO/epoch-ms of the most recent op (best-effort,
    read at the op boundary like the ledger flush).
  - `last_overlay_count` — checkpoints re-applied by the most recent `ingest_all_memory` pass
    (best-effort, set from the overlay path).
- **Count recall zero-hits.** Add a `recall_zero_hits` counter to `DaemonOpStats`, incremented
  in `record()` for `op == Recall && zero_hit` (mirrors `context_zero_hits`). Surface it in
  `status.ops` and the memory block. `Query`/`Context` zero-hit behavior is unchanged.
- **Promote `remember`/`recall` into the durable ledger.** Add both to `kSubstantiveOps` so they
  appear in each ledger line's per-op `ops` object and in the rollup, and add `recall_zero_hits`
  as a top-level ledger field beside `query_zero_hits`/`context_zero_hits`. **No schema bump:**
  `kLedgerSchemaVersion` governs the histogram *bucket layout* only; the op set is additive and
  the rollup already skips ops absent from a line (`operation_stats.cpp:296-298`) and reads
  top-level fields with a 0 default (`:288-292`). Old lines remain parseable and contribute 0.
  (Side effect, intended: a memory-only daemon lifetime now flushes a ledger line, because
  `has_substantive_ops()` will count `recall`/`remember`.)

## Goals

- `status` exposes a memory inventory (checkpoints, sidecars, recall volume, recall misses,
  recency, last overlay size).
- Memory activity is recorded **durably** across restarts in the op-stats ledger.
- Recall zero-hit rate is observable.
- Append-only JSONL ledger compatibility is preserved; pre-existing lines still parse.

## Non-Goals

- No GC/TTL and no automatic memory pruning.
- No dashboard / UI.
- No change to `graph_remember`/`graph_recall` semantics or payloads.
- No change to `query`/`context` zero-hit behavior.
- No eval changes; no histogram bucket-layout change (no `kLedgerSchemaVersion` bump).

## Capabilities

- **graph-session-memory** (MODIFIED): the daemon now reports a memory inventory in `status`,
  counts recall zero-hits, and records `remember`/`recall` (with recall zero-hits) in the durable
  op-stats ledger.

## Impact

- Engine: new `recall_zero_hits` + `last_*`/overlay counters (`operation_stats.hpp`,
  `DaemonState`); `record()` extension; `op_stats_json` + the `memory` block in `status()`
  (`daemon_ops.cpp`); `kSubstantiveOps` adds `Remember`/`Recall`; ledger line + rollup gain
  `recall_zero_hits` (`operation_stats.cpp`); `ingest_all_memory` reports its merge count
  (`daemon_server.cpp`).
- Tests: `daemon_ops_test` (status memory block; recall-miss increments `recall_zero_hits`;
  inventory after re-overlay), `operation_stats_test` (ledger line carries remember/recall +
  recall_zero_hits; rollup sums them; **old lines without these keys still parse and roll up**).
- Ledger: lines grow by two op entries + one field; older ledgers unaffected.
