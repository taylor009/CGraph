## ADDED Requirements

### Requirement: SQL DDL is extracted into a schema graph
Beyond the file-level `sql_file` node, the deterministic extractor SHALL parse the DDL in `.sql`
files into schema nodes and relationships:

- `CREATE TABLE "<name>"` SHALL emit a `sql_table` node, and `CREATE TYPE "<name>" AS ENUM` a
  `sql_enum` node. Their ids SHALL be keyed on the entity **name** (independent of the source file),
  so the same table or enum appearing across multiple migration files merges — via the graph
  builder's id dedup — into a single node representing the current schema. The node SHALL record the
  `source_file` and location of its defining `CREATE` statement.
- A foreign key `ALTER TABLE "<X>" … FOREIGN KEY (…) REFERENCES "<Y>"` SHALL emit a `references`
  edge from the `sql_table` node for `X` to the `sql_table` node for `Y` (reusing the existing
  `references` relation, so `impact` / typed `explain` / query routing operate over it). Duplicate
  foreign keys across migrations SHALL collapse via edge dedup.

Extraction is regex-based over the (Prisma-style) DDL; SQL is not fully parsed. Forms not matched
(e.g. inline column-level references, non-Prisma dialects) SHALL simply yield no edge rather than an
error.

#### Scenario: Tables, enums, and foreign keys become a schema graph
- **WHEN** the graph is built over `.sql` migrations declaring `CREATE TABLE`, `CREATE TYPE … ENUM`,
  and `ALTER TABLE … FOREIGN KEY … REFERENCES` statements
- **THEN** the graph contains a `sql_table` node per table, a `sql_enum` node per enum, and a
  `references` edge between the owning and referenced tables for each foreign key

#### Scenario: A table merges across migrations into one node
- **WHEN** the same table is created in one migration file and altered in others
- **THEN** the graph contains exactly one `sql_table` node for it (its id keyed on name, not file),
  with the foreign keys added by later migrations attached as `references` edges

#### Scenario: Schema is queryable via existing relation-aware ops
- **WHEN** an agent runs `impact` or `explain --relation references` on a `sql_table` node
- **THEN** the response returns the tables related by foreign keys, because the foreign keys are
  `references` edges the existing ops already traverse

#### Scenario: Extraction parity is preserved
- **WHEN** the extractor goldens (which contain no `.sql` fixtures) are regenerated
- **THEN** they are unchanged — SQL schema extraction is additive and alters no existing language
