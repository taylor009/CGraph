# Retrieval fixture (parity + end-to-end gates)

Committed fixture pair backing two smoke gates:

- `cgraph_pack_context_parity_test` — gather/packing parity with the offline harness
  (focal id injected per row, isolating the packing stage).
- `cgraph_retrieval_quality_test` — end-to-end grade-2 recall through the default
  `context` path (free-text query only; focal resolution on the measured path).

## Contents

- `graph.json` — deterministic code-only export of this repo (1181 nodes, 1521 links;
  no `research/` or build output nodes). All baseline numbers in both gates were
  measured on exactly this graph; absolute recall is only comparable within it.
- `queries.jsonl` — 38 git-mined eval rows (35 symbol-granularity), graded 2 for
  directly-changed symbols and 1 for graph neighbors. Verbatim snapshot of the
  `scripts/bootstrap_eval.py` output; labels are query-derived, never label-derived.

## Regenerating

```
python3 scripts/bootstrap_eval.py --root . --graph <deterministic-graph.json> --out <dir>
```

Config lives in `.research-eval.toml` (commit/file filters, grading, drift handling).
Regenerating the fixture changes both gates' measured baselines: re-measure and re-pin
the baseline constants in `pack_context_parity_test.cpp` and `retrieval_quality_test.cpp`
in the same change, and record the new graph's node/link counts here.
