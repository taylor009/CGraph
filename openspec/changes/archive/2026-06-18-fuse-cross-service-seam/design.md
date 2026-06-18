# Design — cgraph seam fuse

## Flow

```
fuse_seam(seam_fragment, graphs: name -> GraphSnapshot) -> GraphSnapshot | error

  fused_nodes : id -> Node   (insertion-ordered; real service nodes authoritative)
  fused_edges : deduped by (source, target, relation)

  1. for each (name, graph) in graphs (in arg order):
       for each node: copy, set properties.community = name,
                      set properties.service = name if absent; fused_nodes[id] = node  (service wins)
       add all edges
  2. seam fragment:
       for each node:
         if kind == "code-ref": DROP  (the real service node with this id is already present)
         else: set properties.community = seam_community(node); fused_nodes.setdefault(id, node)
       add all seam edges
  3. for every fused edge, both source and target MUST be in fused_nodes
       else -> hard error "edge endpoint missing; supply the owning service graph via --graph"
  4. return GraphSnapshot{ nodes = fused_nodes (insertion order), edges = fused_edges }
```

`seam_community(node)`:
- `service:<name>`        -> `<name>` (the service is its own cluster)
- `endpoint:<provider>:…` -> the node's `properties.provider` (the provider's cluster)
- `schema:<provider>:…`   -> `<provider>` (parsed from the id)

## Why drop the shadow code-refs

`seam gen` emits a `code-ref` shadow whose id IS the consumer's real node id. In the fused view the
real service graph already contributes that node (with its full neighborhood and real
`source_location`), so the shadow is redundant — dropping it lets the `CONSUMED_AT` / `MIRRORED_BY`
edge bind to the real node, making the contract edge drillable into the surrounding code. Edge
endpoints therefore resolve against the real nodes, which is exactly what the fail-loud check in
step 3 guarantees.

## Output — direct HTML export (not the prototype's ingest hack)

`scripts/seam_fuse.py` emitted a fragment and re-ingested it into an empty root just to borrow
`graph.html`. The engine exposes `export_graph_html(GraphSnapshot)` and `to_node_link_json` directly,
so `fuse` builds the fused `GraphSnapshot` and writes `--out/graph.json` + `--out/graph.html`
straight out. No daemon, no empty-root round-trip. The renderer clusters/colors by
`node.properties.community` (verified: `communityFor` reads `properties.community`), so the
per-service tagging in steps 1–2 is the entire contract with the renderer.

## Loader promotion (the one refactor)

`parse_node_link_graph` (+ `parse_node_link_node` / `parse_node_link_edge`) are file-local to
`daemon_lifecycle.cpp`. `fuse` needs full graphs (all node properties + edges), so this change moves
them to a public home — the natural inverse of `to_node_link_json` in `export_json.hpp` (or a small
`node_link.hpp`). `daemon_lifecycle.cpp` then calls the public function. Behavior-preserving; the
daemon fast-load path is covered by existing `daemon_ops` / lifecycle tests, which must stay green.

(`seam gen`'s minimal reader can later be folded onto the same loader, but that is out of scope here —
it reads only the fields anchor resolution needs and is already tested.)

## Why view-only (the Scope C boundary)

Fusing several graphs into one breaks the one-daemon-per-project-root invariant, so the fused graph
is deliberately a *static artifact* — `graph.json` + `graph.html` you open — not a daemon and not
queryable. A queryable cross-service graph (query/path/explain spanning services) remains a separate,
larger capability (Scope C), explicitly deferred.

## Risks

- **Missing service graph → dangling edges.** Mitigated by the step-3 fail-loud check: a contract
  edge whose endpoint isn't in any supplied graph errors rather than rendering a broken picture.
- **Id collision between a seam non-shadow node and a service node.** Seam `service:`/`endpoint:`/
  `schema:` ids are namespaced and distinct from real code-node ids, so `setdefault` (service wins)
  is safe; only the shadow code-refs intentionally share ids, and those are dropped.
