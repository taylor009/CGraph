## ADDED Requirements

### Requirement: Seam generation emits a cross-service contract fragment
The engine SHALL provide deterministic generation of a cross-service contract fragment from a
host-authored seam spec and one or more named consumer code graphs, exposed as the
`cgraph seam gen --seam SPEC --graphs NAME=graph.json[ --graphs …] --out DROPDIR` subcommand. The
output SHALL be a single node-link fragment (`chunk_00.json`) in the drop directory whose nodes use
the kinds `service`, `endpoint`, `schema`, and `code-ref`, with stable ids `service:<name>`,
`endpoint:<provider>:<METHOD> <path>`, `schema:<provider>:<api_version>:<name>`, and (for a
`code-ref`) the resolved consumer node's own id; and whose edges use the relations `CONSUMES`
(service→endpoint), `SERVED_BY` (endpoint→provider service), `RESPONDS_WITH` (endpoint→schema),
`CONSUMED_AT` (endpoint→consumer code node), and `MIRRORED_BY` (schema→consumer code node). Nodes
SHALL be deduplicated by id. Regenerating from the same spec and graphs SHALL produce a
byte-equivalent fragment.

#### Scenario: Spec and graphs produce the contract fragment
- **WHEN** `cgraph seam gen` runs on a spec declaring a provider, services, schemas, endpoints,
  `consumes`, and `mirrors`, with the named consumer graphs supplied
- **THEN** the emitted fragment contains a `service` node per service, an `endpoint` node per
  endpoint with `SERVED_BY` → its provider service and `RESPONDS_WITH` → its response schema, a
  `schema` node per schema, a `CONSUMES` edge from each consuming service to its endpoint, and a
  `code-ref` shadow node plus `CONSUMED_AT` / `MIRRORED_BY` edge for each resolved anchor

#### Scenario: Regeneration is byte-stable
- **WHEN** the same spec and graphs are passed to `cgraph seam gen` twice
- **THEN** the two emitted fragments are byte-equivalent

### Requirement: Cross-graph anchors resolve to real nodes or fail loud
Each `consumes.call_site` and each `mirror` SHALL be resolved against the named consumer graph to the
smallest non-`file` node whose `source_file` matches the anchor path and whose `source_location`
span contains the anchor line; the resolved node's real id SHALL be used as the `code-ref` target so
no cross-graph edge is dangling. If any anchor resolves to no node, the command SHALL fail and emit
no fragment — a partial or dangling seam SHALL NOT be produced.

#### Scenario: Anchor resolves to the smallest containing node
- **WHEN** an anchor line falls within several nested nodes' spans in the consumer graph
- **THEN** the anchor resolves to the node with the smallest span, and the `CONSUMED_AT` /
  `MIRRORED_BY` edge targets that node's real id

#### Scenario: Unresolvable anchor is a hard error
- **WHEN** an anchor's `(file, line)` matches no node span in the named consumer graph
- **THEN** the command fails with a precise error naming the anchor and writes no fragment

### Requirement: Seam spec and references are validated before emission
The seam spec SHALL be validated before any fragment is emitted: required fields present, every
`consumes` entry referencing a declared endpoint, every `mirror` referencing a declared schema, and
every anchor's graph name matching a supplied `--graphs` entry. Any violation SHALL be a hard error
that emits no fragment. The emitted fragment SHALL satisfy `validate_semantic_fragment_json`, so it
is ingestable through the existing fragment-ingest path without any validation change.

#### Scenario: Unknown endpoint or schema reference is rejected
- **WHEN** a `consumes` entry names a `method`+`path` with no matching declared endpoint, or a
  `mirror` names a schema not in `schemas`
- **THEN** the command fails with a precise error and writes no fragment

#### Scenario: Missing consumer graph is rejected
- **WHEN** a `call_site` or `mirror` names a graph for which no `--graphs NAME=…` was supplied
- **THEN** the command fails with a precise error and writes no fragment

#### Scenario: Emitted fragment is ingestable
- **WHEN** a seam fragment is generated successfully
- **THEN** it passes `validate_semantic_fragment_json` and can be ingested via `cgraph enrich-ingest`
  without modification
