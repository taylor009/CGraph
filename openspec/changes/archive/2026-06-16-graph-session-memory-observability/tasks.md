# Tasks — graph-session-memory-observability

## 1. recall_zero_hits counter
- [x] 1.1 Failing test: a `recall` whose `query` matches nothing increments `recall_zero_hits`
  in `status.ops`; a `recall` returning ≥1 does not; `query_zero_hits`/`context_zero_hits`
  unaffected.
- [x] 1.2 Implement: add `recall_zero_hits` to `DaemonOpStats`; in `record()` add
  `if (op == DaemonOp::Recall && zero_hit) recall_zero_hits += 1;`; surface it in
  `op_stats_json` beside `context_zero_hits`.

## 2. status memory inventory block
- [x] 2.1 Failing test: after `remember`, `status.result.memory` reports `checkpoint_count==1`,
  `sidecar_count==1`, `recall_count`/`recall_zero_hits` consistent with calls; a second
  checkpoint makes `checkpoint_count==2`.
- [x] 2.2 Implement: add `last_remember_at` / `last_recall_at` / `last_memory_overlay_count` to
  `DaemonState`; set `last_remember_at` from the checkpoint ms in `remember`, `last_recall_at`
  at the recall boundary.
- [x] 2.3 Implement the `memory` block in `status()`: `checkpoint_count` (count `is_memory_node_id`
  nodes), `sidecar_count` (count `*.json` in `state.memory_dir`), `recall_count`,
  `recall_zero_hits`, `last_remember_at`, `last_recall_at`, `last_overlay_count`.
- [x] 2.4 Implement: `ingest_all_memory` records its merged count into
  `state.last_memory_overlay_count`.

## 3. Durable ledger: remember/recall + recall_zero_hits
- [x] 3.1 Failing test (operation_stats_test): a lifetime serving `remember`/`recall` writes a
  ledger line whose `ops` contains `remember`/`recall` with `count`, plus a top-level
  `recall_zero_hits`; the rollup sums them and emits a `recall` summary.
- [x] 3.2 Failing test (operation_stats_test): a hand-authored OLD line lacking
  `ops.remember`/`ops.recall`/`recall_zero_hits` rolls up without error, contributing 0, with
  `schema_version` unchanged (histograms still merge).
- [x] 3.3 Implement: add `DaemonOp::Remember, DaemonOp::Recall` to `kSubstantiveOps`; add
  `recall_zero_hits` to `op_stats_ledger_line` and to `aggregate_op_stats_ledger`
  (top-level `.value(...,0)`; emit on the `recall` op summary). No `kLedgerSchemaVersion` bump.

## 4. Verify
- [x] 4.1 Existing tests stay green: v1 memory tests, overlay tests, op-stats ledger tests, and
  the adaptive/context zero-hit assertions (unchanged behavior).
- [x] 4.2 Full suite `ctest --preset default`; record pass/fail counts.
- [x] 4.3 Live check (real IPC): remember → recall (hit) → recall (miss) → status shows the
  memory block with correct counts; restart → status still reports `checkpoint_count` via the
  overlay and `last_overlay_count`>0; inspect a fresh `op-stats-ledger.jsonl` line for the
  `remember`/`recall` entries + `recall_zero_hits`.
