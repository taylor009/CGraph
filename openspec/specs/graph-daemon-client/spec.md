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

### Requirement: Adaptive relevance-gated context gathering
The `pack_context` path SHALL support a `gather` parameter (`"adaptive"` **default**, `"fixed"`) that
changes which candidates the BFS collects and adds an additive gather-reach summary to the response
(see "Adaptive responses report gather reach"); the per-entry focus/included/omitted shape SHALL be
otherwise identical between `fixed` and `adaptive`. Under `gather = "fixed"` the gathered candidate
set and the existing response fields SHALL be byte-for-byte unchanged from the historical fixed
k-hop BFS (the pre-flip default), so callers can opt back into it exactly. Under `gather =
"adaptive"` (the default) the gather SHALL expand all nodes at depth 0 and 1 unconditionally
(preserving the full 2-hop core), and SHALL expand a node at depth ≥ 2 only when its query
lexical-overlap is ≥ `gather_theta` (default 0.05), to a maximum depth of 3, so that the third hop is
taken only along query-relevant paths. Adaptive gather SHALL use the existing knapsack fill and the
existing deterministic relevance signal; it SHALL introduce no model or LLM.

#### Scenario: Default gather is adaptive
- **WHEN** a `context` request omits `gather`
- **THEN** the gather is `"adaptive"` (knapsack packing, depth 3, θ=0.05 gate) and the response
  reports `gather: "adaptive"`

#### Scenario: Explicit fixed gather is unchanged
- **WHEN** a `context` request sets `gather = "fixed"`
- **THEN** the gathered candidate set and the pre-existing response fields are byte-for-byte
  identical to the historical fixed k-hop behavior for the same focal, budget, and packing

#### Scenario: Adaptive keeps the 2-hop core and gates the third hop
- **WHEN** a `context` request runs adaptive gather with a `gather_theta`
- **THEN** every node within 2 hops of the focal is still gathered, and a node at depth 2 expands
  its depth-3 neighbors only if its query lexical-overlap is ≥ `gather_theta`; a depth-2 node below
  the threshold contributes no depth-3 neighbors

#### Scenario: Adaptive reaches beyond two hops for less than full three-hop cost
- **WHEN** adaptive gather runs on the evaluation set versus a fixed 2-hop and fixed 3-hop gather
- **THEN** its candidate set reaches relevant symbols a 2-hop gather misses, at a candidate-token
  cost far below the full 3-hop gather

### Requirement: In-engine revalidation gates adaptive gather
The `context` default gather SHALL be `"adaptive"`, and a parity test SHALL guard that default by
reproducing its grade-2 recall improvement through the engine's own token accounting (capped
source-slice cost and the response's real packing), not only the offline Python harness. The parity
test SHALL drive the `context` op with `gather = "adaptive"` over the evaluation rows and assert the
in-engine recall and candidate-cost deltas against recorded targets. It SHALL measure against a
committed, version-controlled fixture pair (a deterministic code-only graph and a verbatim eval
snapshot), NOT the mutable working-tree artifacts (`cgraph-out/graph.json`,
`research/eval/queries.jsonl`), so the gate is reproducible and immune to daemon-state or
working-tree drift. Because the fixture is always present, the gate SHALL run on every checkout
including CI; the artifact-absent skip SHALL remain only as a defensive fallback for the case where
the fixture is missing. The recorded targets and tolerance SHALL be unchanged by the default flip.

#### Scenario: Parity gate reproduces the recall gain in-engine
- **WHEN** the parity test runs `gather = "adaptive"` against the committed fixture rows
- **THEN** mean grade-2 recall@budget is at least the fixed-2-hop baseline plus the change's target
  margin, measured under the engine's real cost model, and the test fails if it is not

#### Scenario: Parity gate runs against the committed fixture, not the working tree
- **WHEN** the parity test runs on any checkout, regardless of the contents of
  `cgraph-out/graph.json` or whether a daemon has accumulated unrelated nodes
- **THEN** it reads the committed fixture pair, reaches the measurement (does not skip), and its
  result depends only on the fixture and the engine, not on working-tree state

#### Scenario: Skip is a fallback only when the fixture is missing
- **WHEN** the committed fixture pair is absent (e.g. a deliberately stripped tree)
- **THEN** the test skips with a success exit rather than failing, exactly as the prior
  artifact-absent behavior

### Requirement: Context responses self-describe their gather and packing mode
Every `context` op response SHALL include a `gather` field (`"fixed"` or `"adaptive"`) and a
`packing` field (`"greedy"` or `"knapsack"`) in every code path, so a caller can determine which
retrieval and packing strategy produced the bundle without inferring it from other fields. The fields
SHALL reflect the strategy actually used after defaults and the `adaptive`-implies-`knapsack` coupling
are applied.

#### Scenario: Default response names its mode
- **WHEN** a `context` request runs with the defaults (adaptive gather)
- **THEN** the response includes `gather: "adaptive"` and `packing: "knapsack"`

#### Scenario: Explicit fixed/greedy response names its mode
- **WHEN** a `context` request sets `gather = "fixed"` with default packing
- **THEN** the response includes `gather: "fixed"` and `packing: "greedy"`

### Requirement: Adaptive responses report gather reach
When `gather = "adaptive"`, the `context` response SHALL include a `reach` summary reporting the total
candidate count gathered, the number of candidates admitted beyond the 2-hop core by the relevance
gate, and the number of depth-2 frontier nodes the gate rejected, so a caller and the telemetry can
observe whether the gate actually expanded the third hop. When `gather = "fixed"` the response SHALL
NOT include the `reach` summary.

#### Scenario: Gate admits a relevant third hop
- **WHEN** adaptive gather runs with a query whose terms match a depth-2 node, and that node has
  depth-3 neighbors
- **THEN** the `reach` summary reports at least one candidate admitted beyond the 2-hop core

#### Scenario: Gate collapses to the core when nothing is relevant
- **WHEN** adaptive gather runs with a query that matches no depth-2 frontier node above the threshold
- **THEN** the `reach` summary reports zero candidates admitted beyond the 2-hop core

### Requirement: Context op contributes a zero-result signal to op-stats
The op-stats recording for the `context` op SHALL set the zero-hit flag when the focal node does not
resolve — the id or query matched nothing — mirroring the query op's zero-hit semantics, so the
durable ledger distinguishes context calls that found a symbol from those that found nothing. A
`context` call that returns a resolved focus SHALL NOT be recorded as a zero hit, even when a tight
budget left no room for neighbors (the focal snippet is still usable context). The context zero-hit
count SHALL be persisted on the ledger line and summed in the cross-session rollup so the rate is
durable, not only live.

#### Scenario: Unresolved focus is a zero hit
- **WHEN** a `context` request resolves no focal node
- **THEN** the op-stats record for that call has the zero-hit flag set, and the persisted context
  zero-hit count for that lifetime increments

#### Scenario: A resolved focus is not a zero hit
- **WHEN** a `context` request returns a resolved focus (with or without included neighbors)
- **THEN** the op-stats record for that call does not have the zero-hit flag set

### Requirement: Op-stats distinguish adaptive gather usage
The durable op-stats ledger SHALL track `context` calls served with `gather = "adaptive"` separately
from fixed-gather calls, as an additive count that is persisted and summed across sessions, so a
rollup can report adaptive adoption. The adaptive count SHALL be readable from ledger files written
before this field existed by defaulting it to zero, so older ledgers continue to roll up without
migration.

#### Scenario: Adaptive context calls are counted distinctly
- **WHEN** N `context` calls run with `gather = "adaptive"` and M run with `gather = "fixed"`, then
  the stats are persisted and reloaded
- **THEN** the cross-session rollup reports the adaptive `context` count as N, independent of the
  total `context` count

#### Scenario: Older ledgers without the field still roll up
- **WHEN** a persisted ledger line predates the adaptive-usage field
- **THEN** the rollup reads its adaptive count as zero and sums it without error

### Requirement: graph_context advertises adaptive gather and forwards its parameters
The `graph_context` MCP tool description SHALL document the adaptive gather mode, when to use it (a
query is present and relevant reach beyond two hops is wanted at bounded token cost), and the
requirement that a `query`/`q` be supplied for the gate to take effect. The `gather` and
`gather_theta` arguments SHALL forward verbatim to the daemon `context` op.

#### Scenario: Tool description names adaptive
- **WHEN** an MCP client lists tools
- **THEN** the `graph_context` description mentions the adaptive gather mode

#### Scenario: Adaptive parameters forward to the context op
- **WHEN** an MCP client calls `graph_context` with `gather = "adaptive"` and a `gather_theta`
- **THEN** the daemon request targets the `context` op with `gather` and `gather_theta` carried
  through unchanged

### Requirement: Explain supports optional relation filtering
The `explain` op SHALL accept an optional `relation` parameter. When `relation` is a
non-empty string, the op SHALL return only adjacent edges whose stored relation token
equals it, using the same exact, case-sensitive comparison as `impact`'s relation filter;
no case-folding or alias mapping SHALL be applied. The relation filter SHALL be applied
before centrality ordering and before the `limit` truncation, so that edges matching the
requested relation are never displaced by higher-centrality edges of other relations. The
relation filter SHALL compose with the existing `direction` filter: when both are given,
only edges satisfying both SHALL be returned. When `relation` is absent or empty, the op's
returned neighbor set, ordering, and counts SHALL be byte-for-byte identical to current
behavior. A `relation` value that matches no adjacent edge SHALL yield an empty neighbor
list and SHALL NOT be treated as an error or a missing-node case.

#### Scenario: Relation filter returns only matching edges
- **WHEN** `explain` is called on a node with mixed adjacent relations and `relation: CALLS`
- **THEN** every returned neighbor carries `relation == "CALLS"`, and an adjacent non-`CALLS`
  neighbor that appears in the unfiltered result is absent

#### Scenario: Absent relation preserves current behavior
- **WHEN** `explain` is called without a `relation` parameter
- **THEN** the returned neighbor set, centrality ordering, and counts are identical to the
  behavior before this change

#### Scenario: Relation and direction compose
- **WHEN** `explain` is called with `direction: in` and `relation: CALLS`
- **THEN** only incoming `CALLS` edges are returned (the intersection of both filters)

#### Scenario: No-match relation yields an empty neighbor list
- **WHEN** `explain` is called with a `relation` that no adjacent edge carries
- **THEN** the response is a found node with an empty neighbor list, not an error or a
  not-found result

### Requirement: graph_explain advertises typed traversal and forwards the relation parameter
The `graph_explain` MCP tool description SHALL document the `relation` parameter and the
named single-hop traversal patterns an agent uses, mapped to the project's stored relation
tokens: find callers (`direction: in`, `relation: CALLS`), find callees (`direction: out`,
`relation: CALLS`), find references (`relation: references`), trace imports
(`relation: imports`), and inspect inheritance/implementation edges (`relation: inherits`).
The tool SHALL forward the `relation` argument verbatim to the `explain` op so the param is
never silently dropped.

#### Scenario: Tool description names the typed patterns
- **WHEN** an MCP client lists tools
- **THEN** the `graph_explain` description names the `relation` parameter and the
  callers/callees/references usage patterns

#### Scenario: relation forwards to the explain op
- **WHEN** an MCP client calls `graph_explain` with `relation: CALLS`
- **THEN** the daemon receives `op == "explain"` with `params.relation == "CALLS"`

### Requirement: Parity gate uses a committed deterministic fixture graph
The pack_context parity gate's graph fixture SHALL be a deterministic, code-only build of the
project (excluding disposable research and generated build artifacts), committed under version
control alongside a verbatim snapshot of the evaluation set. The fixture graph SHALL be reproducible:
rebuilding it from the same sources SHALL produce a byte-identical `graph.json`. The fixture graph
and the fixture eval set SHALL be internally consistent — every grade-2 evaluation `node_id` SHALL
resolve to a node in the fixture graph — so the gate is independent of checkout path and machine. The
fixture eval set SHALL be a verbatim copy: labels, grades, queries, and recorded targets SHALL NOT be
altered when producing or regenerating the fixture.

#### Scenario: Fixture graph is deterministic
- **WHEN** the fixture graph is rebuilt from the same sources
- **THEN** the resulting `graph.json` is byte-identical to the committed fixture

#### Scenario: Fixture graph and eval set are internally consistent
- **WHEN** the parity test resolves each grade-2 evaluation `node_id` against the fixture graph
- **THEN** every id resolves to a node present in the fixture graph

#### Scenario: Fixture excludes disposable and generated content
- **WHEN** the fixture graph is generated
- **THEN** it contains only the project's code (e.g. src/tests/scripts) and no `research/` or
  `build/` nodes, matching the graph the recorded targets were calibrated on

### Requirement: Lexical multi-seed focal resolution for free-text queries
When resolving a focal node for a `context` or `query` request, the engine SHALL first attempt
exact (id, label, bare symbol) and substring matching, and SHALL fall back to lexical term-overlap
matching only when those produce no match. The lexical fallback SHALL rank nodes by the overlap of
the query's lexical terms with the node label and resolve the focal from the top match,
deterministically (ties broken by centrality then id). For a `context` request, the gather SHALL be
seeded from the top-N lexical matches and union their neighborhoods. When the best lexical overlap
falls below a minimum confidence threshold, the focal SHALL remain unresolved — the response returns
suggestions and the call is recorded as a zero hit, unchanged from current behavior.

#### Scenario: Natural-language query resolves via lexical overlap
- **WHEN** a `context` request supplies a free-text query that is not an exact match or a substring
  of any node id or label, but shares lexical terms with one or more symbols
- **THEN** a focal node is resolved from the highest-overlap match and a non-empty context bundle is
  returned, instead of the empty `focus:null` response

#### Scenario: Exact lookups are unchanged
- **WHEN** a request supplies an exact node id, an exact label, or a bare symbol name that resolves
  by the existing exact/substring path
- **THEN** that node is resolved exactly as before and the lexical fallback does not run

#### Scenario: Off-topic query stays an honest zero hit
- **WHEN** a query's best lexical overlap with any node is below the confidence threshold
- **THEN** the focal stays unresolved, the response returns `suggestions`, and the call is recorded
  as a context/query zero hit

#### Scenario: Multi-seed gather unions several ego graphs
- **WHEN** a free-text `context` query overlaps several symbols and resolves via the lexical fallback
- **THEN** the gathered candidate set is the union of the neighborhoods of the top-N lexical seeds,
  deduplicated by shallowest reach, and a relevant node reachable only from a lower-ranked seed is
  included

#### Scenario: Resolution is deterministic
- **WHEN** the same free-text query is resolved twice against the same snapshot
- **THEN** the same focal node (and seed set) is selected, with equal-overlap ties broken by
  centrality then id

