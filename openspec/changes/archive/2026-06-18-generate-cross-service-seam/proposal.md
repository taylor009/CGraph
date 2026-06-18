## Why

cgraph models one project per graph. Real systems are several services that only make sense
*together* — a backend that calls an ml-api over HTTP, sharing wire schemas. Today nothing in cgraph
captures that join: each service has its own graph, and the contract between them (which call site
hits which endpoint, which type mirrors which response schema) lives only in prose.

A research prototype (`scripts/seam_gen.py`) proved the shape: a host-authored declarative **seam
spec** (services + endpoints + schemas + which consumer call sites hit which endpoint, with
`file:line` anchors) plus the consumer code graphs, deterministically resolved into a single
cgraph-compatible **contract fragment**. Its load-bearing property is that every cross-graph edge is
*anchored to a real node* — each `(file, line)` is resolved against the actual consumer graph, and
an anchor that resolves to nothing is a hard error, never a dangling edge. The prototype's docstring
parks exactly this: "promote to a `cgraph seam` subcommand once the shape is proven." It is proven.

This is deterministic graph work — anchor resolution against a graph's own `source_location` index,
and fragment emission — so it belongs in the keyless binary. The host authors *which service calls
what* (judgment); cgraph resolves and validates it (determinism), matching the host-skill contract.

## What Changes

- Add a `cgraph seam gen` subcommand (sibling to `enrich-plan` / `enrich-ingest`):
  `cgraph seam gen --seam SPEC.json --graphs NAME=graph.json[ --graphs …] --out DROPDIR`.
- It reads a validated seam spec and the named consumer graphs, then emits one node-link **fragment**
  (`chunk_00.json`) modeling the cross-service contract:
  - node kinds `service`, `endpoint`, `schema`, and `code-ref` (a shadow node carrying the real
    consumer node id resolved from an anchor);
  - edges `CONSUMES` (service→endpoint), `SERVED_BY` (endpoint→provider service), `RESPONDS_WITH`
    (endpoint→schema), `CONSUMED_AT` (endpoint→resolved consumer node), `MIRRORED_BY`
    (schema→resolved consumer node).
- Every `file:line` anchor is resolved to the smallest non-file node whose `source_file` matches and
  whose span contains the line. **Any unresolved anchor, unknown endpoint/schema reference, or
  missing `--graphs` entry is a hard error — the command fails and emits nothing** (no dangling
  edges, ever).
- The emitted fragment passes the existing `validate_semantic_fragment_json`, so it ingests through
  the existing `enrich-ingest` path with no validation change (fragment parsing is open-vocabulary:
  a node needs `id`+`label`, edges need `source`/`target`/`relation`).

## Capabilities

### Added Capabilities

- `cross-service-seam`: deterministic generation of a cross-service contract fragment from a
  host-authored seam spec and the consumer code graphs, with anchored (never dangling) cross-graph
  edges, emitted as a standard ingestable fragment.

## Non-Goals

- **Fusing / rendering** (`scripts/seam_fuse.py`'s view-only multi-graph render) — deferred. This
  change ships only generation; the visualization is a follow-up.
- **A queryable cross-service seam graph** (query/path/explain across services) — deferred; it would
  break the one-daemon-per-project-root model and is a much larger change.
- **Discovering the seam automatically** — the spec is host-authored (which call site hits which
  endpoint is a judgment the host owns). cgraph only resolves and validates it.
- **New fragment-validation vocabulary** — none needed; the contract kinds/relations are plain
  strings the existing parser already accepts.

## Impact

- New engine module `src/engine/seam.cpp` (+ `include/cgraph/seam.hpp`) and its
  `tests/smoke/seam_test.cpp`; `src/engine/CMakeLists.txt` + `tests/smoke/CMakeLists.txt` entries.
- `src/cli/main.cpp` gains the `seam gen` subcommand and usage line.
- Reuses: node-link graph loading (`parse_node_link_graph`), `Fragment` + fragment JSON
  serialization (`fragment_json`), and `validate_semantic_fragment_json`. No daemon, no MCP, no
  parity surface touched.
- Verified by: unit tests over a seam spec + two fixture graphs (correct nodes/edges/ids, anchors
  resolved to the real node ids, fail-loud on a dangling anchor / unknown endpoint / missing graph),
  the emitted fragment passing fragment validation, and the full suite.
