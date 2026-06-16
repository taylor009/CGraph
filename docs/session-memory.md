# Session memory: checkpoint and recall

cgraph can act as a durable task-memory layer so a coding agent can `/compact` or `/clear`
a long session without losing the thread. The graph daemon (`graphd`) and `graph.json` are
**external to the agent's context window**, so a checkpoint written before `/clear` is still
there after it.

This is host/agent-authored memory. The engine stores it verbatim and keeps it inert to code
analysis and code retrieval — it never changes query/context/impact rankings.

## Tools

- `graph_remember {title, body, touches?, tags?}` — write one checkpoint. The `body` is a
  distilled markdown summary; `touches` lists code symbols (ids or names) the work concerns.
  The body is written under `cgraph-out/memory/` and the checkpoint node points at it.
- `graph_recall {query?, limit?}` — return recent checkpoints newest-first, each with its body
  summary and briefs of the code it touched.

## Workflow

```
before /compact or /clear ──► graph_remember(title, body, touches=[symbols])
after /clear ─────────────► graph_recall()            (restores the thread, ~KB payload)
                                  │
                                  └─► graph_context(id=<linked symbol>)   (reload code, budget-bounded)
after a long exploration ──► graph_remember(distilled finding)
before opening Playwright ─► graph_remember(what you're about to test)
```

The discipline is **distill → checkpoint → clear → recall**:

- Checkpoint a *distilled* summary — what you did and what's next.
- **Never** persist raw tool output, Playwright DOM snapshots, or chain-of-thought. Those are
  ephemeral; only the distilled outcome belongs in a checkpoint.
- After `/clear`, recall restores the task state cheaply; use `graph_context` on the linked
  symbols to reload just-enough code instead of re-reading files.

## Persistence (v1)

The primary use case works because **`/clear` does not stop the daemon** — it is a Claude Code
context operation. A checkpoint written before `/clear` is recall-able after it from the same
long-running daemon. Bodies live under `cgraph-out/memory/`; checkpoint nodes are added to the
live graph and persisted into `graph.json`.

Surviving a daemon **restart** or a full **rescan** is not guaranteed in v1: both rebuild the
graph from extraction (which has no memory nodes), so the live graph loses checkpoint nodes
(their body files remain on disk). Re-overlaying memory after a rebuild — so checkpoints
survive restart and rescan — is a planned follow-up.
