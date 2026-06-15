# graph-daemon-client Specification

## Purpose
TBD - created by archiving change persist-op-stats-ledger. Update Purpose after archive.
## Requirements
### Requirement: Durable op-stats ledger flush on shutdown
The daemon SHALL, on leaving its serve loop for any reason (idle timeout, `shutdown` op, or clean
termination), append exactly one JSON line describing the lifetime's accumulated op-stats to
`cgraph-out/op-stats-ledger.jsonl` in the project's output directory, beside the existing on-exit
graph persist. The line SHALL record wall-clock `boot` and `shutdown` timestamps, `uptime_seconds`,
the query zero-hit count, and per-operation `count`, `total_ms`, and a fixed-layout latency
histogram. The flush SHALL be best-effort: a failure to write SHALL be logged and discarded and
SHALL NOT block, delay, or abort shutdown, nor affect the graph persist that shares the teardown.
The flush SHALL be gated on at least one substantive operation (query, path, explain, impact, or
context) in the lifetime; a lifetime with only `status`/`shutdown` ops SHALL write no line. The
ledger SHALL be append-only — each lifetime adds one line and the file is never rewritten. The live
since-boot `status` path SHALL be unchanged.

#### Scenario: A lifetime with query activity appends one ledger line
- **WHEN** a daemon serves one or more substantive ops and then shuts down
- **THEN** exactly one new JSON line is appended to `cgraph-out/op-stats-ledger.jsonl`, it parses as
  JSON, and its per-op `count`/`total_ms` equal the lifetime's accumulated op-stats

#### Scenario: Idle, work-free lifetimes write nothing
- **WHEN** a daemon spawns, answers only `status`/`shutdown` ops, and idle-shuts-down
- **THEN** no line is appended to the ledger

#### Scenario: Flush never breaks shutdown
- **WHEN** the ledger path is unwritable at shutdown
- **THEN** the failure is logged, the daemon still completes its graph persist and exits cleanly,
  and no exception escapes the teardown

#### Scenario: Append-only, crash-tolerant
- **WHEN** the ledger already contains lines from prior lifetimes
- **THEN** the new line is appended without rewriting existing lines, and a reader that encounters a
  malformed trailing line skips it and still parses every well-formed line

### Requirement: Wall-clock timestamps derived from the monotonic substrate
The ledger line's `boot` and `shutdown` timestamps SHALL be wall-clock (ISO-8601 UTC), but the
running daemon's timing substrate SHALL remain purely monotonic. The implementation SHALL read the
system wall clock at most once per lifetime, at flush time, to obtain `shutdown`, and SHALL derive
`boot` as `shutdown − (monotonic_now − start_time)`. No wall-clock read SHALL occur in per-operation
recording or in the scoped phase timers.

#### Scenario: Boot is derived, not measured live
- **WHEN** a ledger line is produced at flush
- **THEN** `boot` equals `shutdown` minus the monotonic uptime, `shutdown >= boot`, and
  `uptime_seconds` equals the monotonic elapsed time since daemon start

#### Scenario: Substrate stays monotonic
- **WHEN** operations are recorded during the daemon's lifetime
- **THEN** no system wall clock is read on the per-op recording path; only the flush reads it, once

### Requirement: Versioned latency histogram
Each ledger line SHALL carry a `schema_version` and, per operation, a latency histogram over a
pinned, log-spaced bucket layout fixed by that version (v1: upper bounds in milliseconds of
`[1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]` plus one overflow bucket — 12 counts). A latency
SHALL fall in the bucket whose range contains it, with latencies at or above the top bound counted
in the overflow bucket. Changing the bucket layout SHALL bump `schema_version`; consumers SHALL
treat histograms of different versions as non-mergeable.

#### Scenario: Latencies bucket deterministically
- **WHEN** operations with latencies of 0.4 ms, 1 ms, 1.5 ms, and 4000 ms are recorded
- **THEN** they land respectively in bucket 0 ([0,1)), bucket 1 ([1,2)), bucket 1 ([1,2)), and the
  overflow bucket, and the per-op histogram counts sum to that op's `count`

#### Scenario: Version pins the layout
- **WHEN** a ledger line is produced
- **THEN** it includes `schema_version` equal to the current ledger schema version and a histogram
  whose bucket count matches that version's pinned layout

### Requirement: Cross-session op-stats rollup
The CLI SHALL provide a `stats` subcommand — `cgraph stats [--root PATH] [--since today|<ISO>|
<duration>]` — that reads a service's `op-stats-ledger.jsonl`, selects lifetimes whose `shutdown`
falls within the window (default: today), and reports an aggregate across them: summed per-operation
counts, overall query zero-hit rate, weighted-mean latency, and approximate p50/p90 latency merged
from same-version histograms. The headline SHALL lead with per-operation query counts and the
zero-hit rate (the usefulness signal), with latency percentiles secondary. Percentiles derived from
merged histograms SHALL be labeled approximate; summed counts and weighted means are exact. When the
selected window mixes histogram `schema_version`s, the rollup SHALL report that rather than merge
incomparable buckets. When the subcommand can reach a live daemon for the same root, it SHALL also
show that daemon's since-boot stats in a clearly separate section, so live and durable figures are
never conflated.

#### Scenario: Rollup sums across lifetimes within the window
- **WHEN** the ledger holds three lifetimes within today's window with query counts 20, 22, and 12
- **THEN** `cgraph stats --since today` reports a total query count of 54 and a zero-hit rate equal
  to the summed zero-hit queries over 54

#### Scenario: Out-of-window lifetimes are excluded
- **WHEN** a lifetime shut down before the `--since` boundary
- **THEN** its counts are excluded from the rollup totals

#### Scenario: Live and durable are labeled distinctly
- **WHEN** a daemon for the root is running and the ledger has prior lifetimes
- **THEN** the output shows a live since-boot section and a durable ledger section under distinct
  labels, and the durable section's window is stated

#### Scenario: Percentiles are honest about precision
- **WHEN** the rollup reports latency percentiles merged from histograms
- **THEN** they are labeled approximate, while summed counts and weighted-mean latency are exact;
  and a window mixing `schema_version`s is reported rather than silently merged

