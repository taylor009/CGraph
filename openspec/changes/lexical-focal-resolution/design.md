## Test strategy

Focal resolution is deterministic and pure over the snapshot, so it unit-tests cleanly in
`daemon_ops_test.cpp`; the offline harness is the quantitative cross-check, not the gate.

- **Unit (red first), against a small fixture graph:**
  - a natural-language query that is NOT a substring of any label resolves to a lexically
    overlapping focal (was `focus:null`);
  - an exact symbol name / id / bare label still resolves to that exact node — the substring/exact
    path is tried first and unchanged (no regression);
  - a query whose best lexical overlap is below the threshold stays **unresolved**: returns
    `suggestions` and records the zero hit (honest no-match preserved);
  - multi-seed: a query overlapping several symbols pulls neighbors from more than one ego graph
    (a grade-relevant node reachable only from the 2nd-best seed is gathered);
  - determinism: equal-overlap ties break by (centrality desc, id) — same input, same focus.
- **End-to-end:** `cgraph-client --root . context '{"q":"<NL commit-style query>"}'` returns a
  non-empty `focus` + bundle where it previously returned `focus:null`.
- **Offline re-confirmation (directional, not the gate):** `research/focal_resolution.py` reproduces
  the 0.000 → 0.321 direction on the engine-equivalent logic.

## Decisions

- **Fallback only when exact/substring is empty.** Order: exact id → exact label → bare-symbol
  (`resolve_node`) → substring (`matching_nodes`) → **lexical**. Precise lookups an agent already
  relies on are byte-for-byte unchanged; lexical only rescues the previously-empty case.

- **Reuse existing scoring.** `lexical_terms` + `query_term_overlap` (daemon_ops.cpp:172-224) already
  exist for the adaptive gate / knapsack value. No new scorer — the fallback wires them into focal
  resolution.

- **Multi-seed top-N (N=5).** The experiment shows the single-seed lexical focal is relevant only
  ~23% of the time; unioning the top-5 ego graphs is what moved recall 0.212 → 0.321. N=5 is the
  measured point; expose it as an internal constant, not a public param, initially.

- **Confidence floor keeps zero-hit honest.** A lexical resolver with `overlap > 0` resolves nearly
  every query, which would (a) mask the zero-hit quality signal and (b) return a misleading bundle
  for off-topic queries. A minimum-overlap threshold means genuinely irrelevant queries still report
  unresolved + suggestions. The threshold is a tuning knob; start conservative (e.g. require ≥1
  shared term, i.e. overlap > 0, AND the top node's overlap ≥ a small floor) and record the chosen
  value in results.

- **Multi-seed cost.** Unioning N ego graphs raises gather-side I/O (up to N× `read_source_snippet`
  seed reads + a larger candidate pool), but the knapsack packer still caps the returned payload by
  budget. Note the candidate-pool growth in the result, per the no-silent-caps discipline.

## Validation that cannot be tested directly

- Absolute recall numbers are offline and directional (N=35, tiktoken proxy); the engine gate is the
  behavioral unit/e2e assertions (resolves-where-empty, exact-unchanged, below-threshold-unresolved,
  multi-seed-union, determinism), not a recall threshold baked into a test.
- A Python metric win is not a product win until it transfers through the engine's real token
  accounting and query path — the e2e check is the minimum bar; a follow-up daemon-query benchmark
  confirms the real-path delta.
