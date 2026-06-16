# graph-session-memory (delta: graph-session-memory-observability)

## ADDED Requirements

### Requirement: Status reports a memory inventory
The `status` op SHALL include a `memory` object describing the session-memory layer:
`checkpoint_count` (the number of `memory:` checkpoint nodes in the live snapshot),
`sidecar_count` (the number of checkpoint sidecar files on disk), `recall_count` (lifetime
`recall` op count), `recall_zero_hits` (recalls that returned no checkpoints), `last_remember_at`
and `last_recall_at` (recency of the most recent respective op, empty when never called), and
`last_overlay_count` (the number of checkpoints re-applied by the most recent memory re-overlay).
The block SHALL be present whenever the daemon is serving, with zeroed/empty values before any
memory activity.

#### Scenario: Inventory reflects written checkpoints
- **WHEN** one checkpoint has been written and `status` is requested
- **THEN** `memory.checkpoint_count` is 1 and `memory.sidecar_count` is 1; after a second
  checkpoint, `memory.checkpoint_count` is 2

#### Scenario: Inventory is restored after re-overlay
- **WHEN** the graph is rebuilt and memory checkpoints are re-overlaid from their sidecars
- **THEN** `memory.checkpoint_count` reflects the re-applied checkpoints (not duplicated) and
  `memory.last_overlay_count` is the number re-applied

### Requirement: Recall zero-hits are counted
The daemon SHALL count `recall` operations that return no checkpoints as recall zero-hits and
expose the count in `status.ops` and the `memory` block. Counting recall zero-hits SHALL NOT
change the existing `query` or `context` zero-hit counters or behavior.

#### Scenario: A recall that returns nothing is counted
- **WHEN** a `recall` is issued whose query matches no checkpoint
- **THEN** `recall_zero_hits` increments, while `query_zero_hits` and `context_zero_hits` are
  unchanged

#### Scenario: A recall that returns results is not a zero-hit
- **WHEN** a `recall` returns at least one checkpoint
- **THEN** `recall_zero_hits` does not increment

### Requirement: remember and recall are recorded in the durable op-stats ledger
The durable op-stats ledger SHALL record `remember` and `recall` operations — their per-op
counts and latency histograms in each ledger line and in the cross-session rollup — and SHALL
record a top-level `recall_zero_hits`. This SHALL be additive and backward compatible: the
histogram bucket layout and `schema_version` SHALL be unchanged, and ledger lines written before
this change (which lack `remember`/`recall` op entries and `recall_zero_hits`) SHALL still parse
and roll up, contributing zero to the new fields.

#### Scenario: A memory-active lifetime records remember/recall durably
- **WHEN** a daemon serves `remember`/`recall` ops and then shuts down
- **THEN** its ledger line's `ops` object contains `remember` and `recall` entries with counts,
  and a top-level `recall_zero_hits`, and the rollup sums them across lifetimes

#### Scenario: Pre-existing ledger lines remain parseable
- **WHEN** the rollup processes a ledger that contains lines written before this change
- **THEN** those lines parse without error, their missing `remember`/`recall`/`recall_zero_hits`
  read as zero, and same-`schema_version` histograms still merge
