## Why

`cgraph seam gen` (the `cross-service-seam` capability) now produces a contract fragment joining
services by their wire contracts. But a fragment is data, not a picture — the payoff of a seam is
*seeing* how the services connect: backend's real call sites, the ml-api endpoints they hit, the
shared schemas, each service as its own cluster. The proven prototype `scripts/seam_fuse.py` does
exactly this — merge the seam fragment with the real service code graphs into one node-link view,
tag each node with a `community` so the renderer clusters per service, and drop the seam's shadow
`code-ref` nodes so the contract edges attach to each service's real node (with its full
neighborhood). This change productizes that as `cgraph seam fuse`, completing the seam picture.

The renderer already clusters and colors by `node.properties.community` (`export_graph_html`,
`communityFor`), so fuse only has to tag nodes and hand the fused snapshot to the existing exporter.

## What Changes

- Add a `cgraph seam fuse --seam SEAM.json --graph NAME=graph.json[ --graph …] --out DIR` subcommand
  (sibling to `seam gen`).
- It merges the seam fragment with the named service code graphs into a single fused graph and
  writes `graph.json` + `graph.html` to `--out`:
  - each `--graph NAME=path` service: every node tagged `properties.community = NAME` (and
    `service = NAME`); the real service nodes are authoritative on id collision;
  - the seam fragment: `code-ref` shadow nodes are **dropped** (the real service node already carries
    that id and its full neighborhood, so `CONSUMED_AT` / `MIRRORED_BY` edges attach to it), and the
    remaining `service` / `endpoint` / `schema` nodes are tagged with their community (service name
    for `service:`, provider for `endpoint:` / `schema:`);
  - edges are deduplicated; **any edge endpoint id missing from the fused node set is a hard error**
    (a service graph wasn't supplied) — no dangling render.
- Output is a static, view-only render: each service is a colored cluster, joined by the seam's
  contract edges, opened directly in `graph.html`.
- **Promote `parse_node_link_graph` to a public header** (it is the inverse of the already-public
  `to_node_link_json`): `seam fuse` needs to load full service graphs (all node properties + edges),
  and the loader is currently file-local to `daemon_lifecycle.cpp`. Exposing it is the canonical-
  location fix and removes the need for a second ad-hoc reader.

## Capabilities

### Modified Capabilities

- `cross-service-seam`: adds a view-only fused render — `cgraph seam fuse` merges the seam fragment
  and the consumer code graphs into one community-clustered node-link graph (`graph.json` +
  `graph.html`), collapsing shadow code-refs onto the real service nodes and failing loud on any
  missing service graph.

## Non-Goals

- **A queryable cross-service graph** (query / path / explain across services) — still deferred
  (Scope C). The fused output is a *static render artifact*, not a daemon and not queryable; the
  one-daemon-per-project-root model is untouched.
- **Replacing `seam gen`** — fuse consumes a seam fragment (from `seam gen`) plus the service graphs;
  it does not re-resolve anchors.
- **New renderer features** — fuse reuses `export_graph_html` as-is; the only contract with the
  renderer is the existing `properties.community` clustering.

## Impact

- `src/engine/seam.cpp` (+ `seam.hpp`): a `fuse_seam` entry returning the fused `GraphSnapshot` (or
  an error). `src/cli/main.cpp`: the `seam fuse` subcommand + usage. `tests/smoke/seam_test.cpp`
  (or a sibling) covers fuse.
- **Refactor:** move `parse_node_link_graph` (and its node/edge helpers) to a public header
  (`export_json.hpp` or a small `node_link` header) and update `daemon_lifecycle.cpp` to use it —
  behavior-preserving; the daemon load path keeps working (covered by `daemon_ops` tests).
- Reuses `export_graph_html` + `to_node_link_json` for output. No daemon/MCP/parity surface touched.
- Verified by: a fuse unit test (community tags per service, shadow code-refs dropped, contract
  edges attach to real nodes, fail-loud on a missing service graph), a live smoke producing an
  openable `graph.html`, daemon tests still green after the loader move, and the full suite.
