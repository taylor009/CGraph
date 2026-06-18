## ADDED Requirements

### Requirement: Seam fuse renders a clustered multi-service view
The engine SHALL provide a view-only fused render of a seam, exposed as the
`cgraph seam fuse --seam SEAM --graph NAME=graph.json[ --graph …] --out DIR` subcommand. It SHALL
merge the seam fragment with the named consumer code graphs into a single node-link graph and write
`graph.json` and `graph.html` to the output directory. Every node from a `--graph NAME=…` service
SHALL be tagged `properties.community = NAME`, and each seam `service` / `endpoint` / `schema` node
SHALL be tagged with its community (the service name for a `service` node, the provider for an
`endpoint` or `schema` node), so the existing renderer (which clusters and colors by
`properties.community`) draws each service as its own cluster joined by the seam's contract edges.
Edges SHALL be deduplicated. The fused output is a static artifact — it is NOT a daemon and NOT
queryable.

#### Scenario: Each service renders as its own cluster
- **WHEN** `cgraph seam fuse` runs on a seam fragment plus the supplied service graphs
- **THEN** the written `graph.json` tags every node with a `properties.community` (the service name,
  or the provider for endpoint/schema nodes), and `graph.html` is produced for opening

### Requirement: Shadow code-refs collapse onto real service nodes
When fusing, the seam's `code-ref` shadow nodes SHALL be dropped, because the real service node
carrying the same id is already present from that service's graph (with its full neighborhood and
real source location). The seam's `CONSUMED_AT` and `MIRRORED_BY` edges SHALL therefore attach to
the real service nodes, so the contract is drillable into the surrounding code.

#### Scenario: Contract edge binds to the real node, not a shadow
- **WHEN** a seam fragment contains a `code-ref` shadow for a consumer call site and the consumer's
  service graph is supplied to fuse
- **THEN** the fused graph contains no `code-ref` node for that id, and the `CONSUMED_AT` edge
  targets the real service node already present from the service graph

### Requirement: Fuse fails loud on a missing service graph
Every endpoint of every fused edge SHALL resolve to a node present in the fused node set. If any
edge endpoint id is absent — because the owning service graph was not supplied via `--graph` — the
command SHALL fail with a precise error and write no fused graph; a dangling render SHALL NOT be
produced.

#### Scenario: Omitting a referenced service graph is rejected
- **WHEN** a seam edge references a node from a service graph that was not passed to `cgraph seam
  fuse`
- **THEN** the command fails with an error identifying the missing endpoint and writes no output
