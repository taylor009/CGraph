# Design — cgraph seam gen

## Seam spec (host-authored JSON, validated input)

```jsonc
{
  "provider": "ml-api",                 // the service that serves the endpoints
  "api_version": "v3",
  "error_codes": [400, 500],            // default error codes for endpoints (optional)
  "services": [
    { "name": "backend", "role": "consumer", "owned": true,
      "status": "...", "replacement": "...", "graph": "..." }   // status/replacement/graph optional
  ],
  "schemas": [
    { "name": "ScoreResult", "canonical": "ml-api/src/schemas/score.ts" }
  ],
  "endpoints": [
    { "method": "POST", "path": "/v3/score",
      "path_params": ["modelId"], "query_params": ["verbose"],
      "response_schema": "ScoreResult", "error_codes": [400, 429] }   // path/query/error optional
  ],
  "consumes": [
    { "service": "backend", "method": "POST", "path": "/v3/score",
      "call_site": { "graph": "backend", "file": "src/score.ts", "line": 42 } }
  ],
  "mirrors": [
    { "schema": "ScoreResult", "graph": "backend", "file": "src/types.ts", "line": 10 }
  ]
}
```

Required: `provider`, `api_version`, `services[].name`, `schemas[].{name,canonical}`,
`endpoints[].{method,path,response_schema}`, `consumes[].{service,method,path,call_site}`,
`mirrors[].{schema,graph,file,line}`. Anything missing is a hard error with a precise message.

## Node ids (stable, contract-coordinate keyed)

```
service:<name>
endpoint:<provider>:<METHOD> <path>          e.g. endpoint:ml-api:POST /v3/score
schema:<provider>:<api_version>:<TypeName>   e.g. schema:ml-api:v3:ScoreResult
code-ref shadow  ->  the REAL consumer node id (from anchor resolution)
```

Path params are kept templated in the id (the path string as written), so re-running on the same
spec is byte-stable. Nodes dedup by id.

## Edges

| relation | from → to | meaning |
|---|---|---|
| `CONSUMES` | service → endpoint | this service calls this endpoint |
| `SERVED_BY` | endpoint → provider service | terminal: who serves it |
| `RESPONDS_WITH` | endpoint → schema | response payload type |
| `CONSUMED_AT` | endpoint → consumer code node | the real call site (anchor-resolved) |
| `MIRRORED_BY` | schema → consumer code node | the real local mirror type (anchor-resolved) |

## Anchor resolution (the load-bearing determinism)

```
resolve(graph, file, line):
    candidates = nodes n where
        n.source_file endswith file
        AND n.kind != "file"
        AND n.source_location.start_line <= line <= n.source_location.end_line
    return the candidate with the SMALLEST span (end-start); tie -> first by stable order
    if none: HARD ERROR  ("anchor did not resolve … refusing to emit a dangling edge")
```

The resolved node becomes a `code-ref` shadow node whose **id is the real consumer node's id** (so
the cross-graph edge resolves locally in the seam fragment while still pointing at the consumer's
real node for a later deep-dive), carrying `service`, `graph`, `symbol_kind`, and `span` properties.

This reuses the engine's existing node-link loader (`parse_node_link_graph`) and the
`source_location` already on every node — the same span data `explain` / `context` use.

## Validation — fail loud, emit nothing on any error

- spec is malformed / missing a required field
- a `consumes` entry references an endpoint not declared in `endpoints`
- a `mirror` references a schema not declared in `schemas`
- a `call_site`/`mirror` names a graph with no matching `--graphs NAME=…`
- an anchor `(file, line)` resolves to no node

Each is a hard error with a precise message; the command exits non-zero and writes no fragment. A
partial/dangling seam is never produced.

## Output

A single node-link fragment `chunk_00.json` in `--out` (`{ "nodes": [...], "edges": [...] }`), built
as a `Fragment` and serialized through the existing fragment JSON path so it is — by
construction — accepted by `validate_semantic_fragment_json`. It can then be ingested with
`cgraph enrich-ingest --drop <out>` like any other host-written fragment. A resolution log (every
resolved anchor → real node id) is written to stderr for auditability, mirroring the prototype.

## Why in-engine (contract)

Anchor resolution needs the graph's own `source_location` index and must be byte-deterministic and
fail-loud; that is deterministic graph work cgraph owns. The seam spec — *which service calls which
endpoint* — is host judgment, supplied as input. No model, no network, no keys. The C++ port gives
the fail-loud anchor resolution and fragment validity first-class status over the disposable Python
prototype.

## Reuse map

| need | existing machinery |
|---|---|
| load a consumer graph.json | `parse_node_link_graph` (daemon load path) |
| node span / source_file | `Node.source_location`, `Node.source_file` |
| build + serialize the fragment | `Fragment`, `fragment_json` serialization |
| guarantee the output ingests | `validate_semantic_fragment_json` |
| subcommand dispatch | the `argv[1]` pattern (`enrich-plan`/`enrich-ingest`/`stats`) in `cli/main.cpp` |

## Risks

- **Anchor drift.** A `file:line` in the spec goes stale when the consumer code moves. Mitigated by
  fail-loud resolution: a stale anchor errors rather than silently mis-binding. (A future
  symbol-name anchor mode could be more robust; out of scope here.)
- **Spec authoring burden.** The spec is hand-written today. Acceptable for the first slice; a
  spec-discovery helper is a separate future capability, explicitly a non-goal.
