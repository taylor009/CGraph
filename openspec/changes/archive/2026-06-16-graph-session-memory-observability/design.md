# Design — graph-session-memory-observability

## Test strategy (before implementation)

All of this is deterministic at the engine layer (status payload, counters, ledger
serialize/rollup); only recency timestamps touch the wall clock.

1. **Status memory inventory** (daemon_ops_test): after `remember`, `status.result.memory`
   reports `checkpoint_count == 1`, `sidecar_count == 1`, and `recall_count`/`recall_zero_hits`
   reflecting the calls made. After a second checkpoint, `checkpoint_count == 2`.
2. **Recall zero-hit counted** (daemon_ops_test): a `recall` with a `query` matching nothing
   increments `recall_zero_hits`; a `recall` that returns ≥1 does not. `query_zero_hits` and
   `context_zero_hits` are unaffected by recall calls.
3. **Inventory survives re-overlay** (daemon_ops_test): after simulating a rebuild + overlay,
   `checkpoint_count` is restored (1, not duplicated) — reuses the overlay test harness.
4. **Ledger carries memory** (operation_stats_test): a lifetime that served `remember`/`recall`
   produces a ledger line whose `ops` object contains `remember`/`recall` entries with `count`,
   and a top-level `recall_zero_hits`; the rollup sums them and emits a `recall` summary.
5. **Old ledger lines still parse** (operation_stats_test): a hand-written line WITHOUT
   `remember`/`recall` op entries and WITHOUT `recall_zero_hits` rolls up cleanly — those
   contribute 0, no error, `schema_version` unchanged so histograms still merge.

## Mechanism

### recall_zero_hits
Add `std::size_t recall_zero_hits = 0;` to `DaemonOpStats` and, in `record()`, mirror the
existing context branch:
```
if (op == DaemonOp::Recall && zero_hit) { recall_zero_hits += 1; }
```
The Recall dispatch already computes `zero_hit = result.value("total",0)==0`
(`daemon_ops.cpp:1385`), so no dispatch change is needed. Surface it in `op_stats_json`
beside `context_zero_hits`.

### status memory block
`status(state, graph)` already has both inputs. Add a `memory` object:
- `checkpoint_count`: count `graph.nodes` where `is_memory_node_id(id)`.
- `sidecar_count`: count `*.json` in `state.memory_dir` (skip if the dir is absent).
- `recall_count`: `op_stats.count[Recall]`; `recall_zero_hits`: the new counter.
- `last_remember_at` / `last_recall_at`: new `std::string` fields on `DaemonState`, set at the
  op boundary. `remember` already computes the checkpoint `ms` timestamp — reuse it for
  `last_remember_at` (no extra clock read). `last_recall_at` reads the wall clock once per
  recall (recall is infrequent; consistent with the ledger's read-at-boundary rule). Empty
  string when never called.
- `last_overlay_count`: a `std::size_t` on `DaemonState` set by `ingest_all_memory` to the
  number of fragments it merged in its last pass.

### ledger: add remember/recall
Extend `kSubstantiveOps` to include `DaemonOp::Remember, DaemonOp::Recall`. This is the single
lever that makes them appear in: the per-op `ops` object of each ledger line
(`op_stats_ledger_line` iterates `kSubstantiveOps`), the rollup (`aggregate_op_stats_ledger`
iterates the same), and the flush gate (`has_substantive_ops`). Add `recall_zero_hits` to the
ledger line (beside `context_zero_hits`) and to the rollup (top-level, `.value(...,0)` default),
emitting it on the `recall` op summary like `context`'s zero-hit rate.

## Back-compat (the load-bearing safety argument)

- **No schema bump.** `kLedgerSchemaVersion` gates only histogram *bucket layout* merge
  (`operation_stats.hpp:69-82`, the `mergeable` check at `operation_stats.cpp:284,303`). Adding
  ops to the per-op object and a new top-level counter does not change bucket layout, so version
  stays 1 and histograms across old/new lines still merge.
- **Old lines parse and contribute 0.** The rollup already guards `if (!ops.contains(name))
  continue;` (`operation_stats.cpp:296-298`) and reads top-level fields with a 0 default
  (`:288-292`). A pre-change line lacks `ops.remember`/`ops.recall`/`recall_zero_hits` → those
  read as 0. A test pins this explicitly.
- **Append-only unchanged.** `append_op_stats_ledger` still appends one line per lifetime; line
  shape only grows.

## Wall-clock note
`last_remember_at` is derived from the checkpoint timestamp `remember` already computes (no new
read). `last_recall_at` reads the wall clock once per recall — the only added clock touch, at an
op boundary, matching the ledger's existing read-at-flush discipline; the monotonic op-latency
substrate is untouched.

## Untestable-directly notes
`last_overlay_count` is set inside the `ingest_all_memory` daemon lambda (not unit-addressable);
it is covered by the live check (remember → restart → status shows the overlay count) rather
than a pure unit test. Everything else is engine-level deterministic.

## Deferred (non-goals)
GC/TTL, pruning, dashboards. A future change may add per-op zero-hit *rates* to the rollup
uniformly, but v1 only adds `recall_zero_hits` to match the existing context pattern.
