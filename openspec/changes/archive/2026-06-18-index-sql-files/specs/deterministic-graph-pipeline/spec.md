## ADDED Requirements

### Requirement: SQL files are indexed as file-level nodes
Project file detection SHALL recognize the `.sql` extension as a known language
(`DetectedLanguage::Sql`), so `.sql` files are classified as code (detected, extracted, and watched
for incremental updates) rather than as enrichment-only documents. For each detected `.sql` file the
deterministic extractor SHALL emit exactly one file-level node with `kind = "sql_file"`, a label
derived from the file name, and the file's `source_file` set — with no symbol nodes and no edges
(file-level only; SQL contents are not parsed). The node SHALL be queryable, enrichable, and
seam-anchorable like any other graph node.

#### Scenario: A SQL file produces one file-level node
- **WHEN** the graph is built over a project containing `.sql` files (e.g. Prisma migrations)
- **THEN** each `.sql` file contributes exactly one node of kind `sql_file` whose `source_file` is
  that file, and no symbol nodes or edges are emitted for it

#### Scenario: SQL files are discoverable
- **WHEN** an agent queries the graph for SQL files (e.g. by kind `sql_file` or a `file:` path match)
- **THEN** the `sql_file` nodes are returned, so the project's data layer is visible in the graph

#### Scenario: Extraction parity is preserved
- **WHEN** the extractor goldens (which contain no `.sql` fixtures) are regenerated
- **THEN** they are unchanged — indexing `.sql` is additive and does not alter extraction of any
  existing language
