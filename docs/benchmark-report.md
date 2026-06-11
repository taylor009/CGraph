# Benchmark Report: cgraph vs Graphify, and Token Cost vs grep/read

Date: 2026-06-11
Machine: Apple Silicon (arm64), macOS (Darwin 25.6.0)
cgraph: `main` @ 7d5503d (Debug build, `build/default`)
Graphify: Python CLI at `~/.local/bin/graphify`
Test corpus: this repository (141 detected source files; graph of ~960 nodes after full dedup)

## 1. cgraph vs Graphify

| Measurement | cgraph (native) | Graphify (Python) | Speedup |
| --- | --- | --- | --- |
| Full build, this repo | **0.42 s** (full pipeline: extract, merge, resolve, dedup, communities, all exports) | **83.2 s** (`graphify update --no-cluster`: deterministic extraction only, clustering *skipped*) | ~198x |
| One-shot build, synthetic fixture (median of 3) | 0.019 s | 0.23 s | 12x |
| Warm query round-trip (median) | **10.6 ms** | **167 ms** | 15.8x |

Commands:

```sh
# full build on this repo
time build/default/src/cli/cgraph --root . --out /tmp/cgraph-bench-out   # 0.42 s
time graphify update . --no-cluster                                      # 83.2 s

# scripted comparisons
python3 scripts/benchmark_one_shot.py    --native build/default/src/cli/cgraph
python3 scripts/benchmark_daemon_query.py --graphd build/default/src/daemon/graphd \
    --client build/default/src/client/cgraph-client --root .
```

Notes:

- The build comparison is *conservative in Graphify's favor*: cgraph's 0.42 s includes
  community detection and every export; Graphify's 83 s explicitly skips clustering.
- The query gap is architectural, not just language. cgraph keeps the graph resident in a
  per-project daemon, so a query is one Unix-socket round-trip into memory. Graphify has no
  daemon — every `graphify query` reloads `graph.json` from disk first.
- As of 7d5503d the daemon also keeps itself current (gitignore-aware file watching,
  incremental updates, background persistence). Graphify only rebuilds on demand.
- The two query commands are not identical operations (cgraph searches ranked node labels;
  Graphify runs a BFS answer). The latency compared is the user-facing wall clock of
  "ask the graph a question", per `scripts/benchmark_daemon_query.py`.

## 2. Token cost: cgraph tools vs grep/read

Four representative navigation tasks, run for real against this repo. Token counts are
measured bytes of the exact output a model would ingest, at ~4 chars/token. Each cgraph
response was asserted to contain real content (and no `graph_state: "building"` marker) —
the first attempt at this benchmark accidentally measured mid-build empty responses, which
that marker caught.

Comparators:

- **grep-efficient** — a best-case disciplined agent: targeted greps, then 100–200-line
  windowed reads. Assumes the agent already knows which file and line range to read.
- **grep-typical** — common agent behavior: grep, then read the whole matched file(s)
  (emulating the Read tool's line-numbered output, 2000-line cap).

| Task | cgraph tok | calls | grep-eff tok | calls | grep-typ tok | calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A. Find a definition (`merge_fragments`) | **114** | 1 | 1,192 | 2 | 5,960 | 2 |
| B. List callers (`publish_graph_snapshot`) | 259 | 1 | **190** | 1 | 4,322 | 3 |
| C. Blast radius, transitive (`semantic_dedup`) | **216** | 1 | 379 | 2 | 3,311 | 4 |
| D. Context bundle to edit (`pack_context`, budget 4000) | 3,377 | 1 | **1,914** | 1 | 8,780 | 1 |
| **Total** | **3,966** | **4** | 3,675 | 6 | 22,373 | 10 |

**Savings: ~82% of tokens and 60% of round-trips vs typical agent behavior; roughly even
(-8%) on raw tokens vs a perfectly disciplined grep agent, in 33% fewer round-trips.**

Honest reading of the comparison:

- The grep-efficient column is generous to grep: it assumes knowledge the agent usually
  spends tokens acquiring. Task D's "efficient" path required already knowing that
  `pack_context` lives around line 380 of `daemon_ops.cpp`.
- Task C is the qualitative standout: cgraph's answer is transitive, direction-aware, and
  depth-ranked in one call. The grep equivalent is a multi-round manual trace that agents
  frequently skip — so in practice the choice is often "216 tokens" vs "analysis not done".
- Task B shows where grep remains competitive: raw grep lines are already a compact caller
  list. cgraph's win there is structure and ranking, not size.
- Task D shows `graph_context`'s trade: it spends budget on ranked neighbor snippets. If you
  already know exactly where to edit, one targeted Read is cheaper.
- Token savings compound: everything ingested early in a session stays in context for the
  rest of it.

## 3. Pros and cons of using cgraph

**Pros**

- Structure questions (where is X defined / who calls X / what breaks if X changes) cost
  100–300 tokens and a single ~11 ms call, returning `file:line` ready to open.
- Transitive impact analysis is effectively a new capability vs grep, not an optimization.
- Centrality ranking puts the right symbol first; grep ordering is arbitrary and noisy.
- Resident daemon keeps itself current: live gitignore-aware watching folds edits in within
  seconds, incremental state re-persists in the background and on shutdown.
- Fully deterministic; no API keys, no per-query LLM cost; misses return did-you-mean
  suggestions instead of silent empties.

**Cons**

- Only code structure is indexed: string literals, comments, config values, and
  un-extracted file types still need grep. cgraph complements grep, it does not replace it.
- For plain caller lists, disciplined grep already produces compact output.
- `graph_context` can cost more than a targeted read when you already know the edit site.
- Freshness lags edits by ~2–5 s (watch cadence + debounce); node counts can drift slightly
  above canonical between full-dedup reconciles (bounded: every 5th update reconciles).
- First call in a new project pays the cold build — sub-second here, seconds on large repos
  (served immediately with a `building` marker, but results are empty until done).
- Token figures use a ~4 chars/token estimate, not a real tokenizer: trust the ratios more
  than the absolutes.

## Bottom line

Against Graphify, cgraph is strictly better at everything both systems do (12–200x build,
16x warm query) and adds liveness Graphify doesn't have. Against grep/read, it wins
decisively on relationship questions and on realistic agent behavior (~82% token savings),
and roughly ties an optimal grep agent on raw tokens while using fewer round-trips — with
grep remaining the right tool for content (non-symbol) search.
