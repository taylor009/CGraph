## MODIFIED Requirements

### Requirement: SQL DDL is extracted into a schema graph
Beyond the file-level `sql_file` node, the deterministic extractor SHALL parse the DDL in `.sql`
files into schema nodes and relationships, matching identifiers in both their **quoted** and
**unquoted** forms (each optionally `schema.`-qualified):

- `CREATE TABLE <name>` SHALL emit a `sql_table` node, and `CREATE TYPE <name> AS ENUM` a
  `sql_enum` node, whether `<name>` is quoted (`"Users"`) or unquoted (`users`). An
  `ALTER TABLE <old> RENAME TO <new>` SHALL emit a `sql_table` node for `<new>`. Their ids SHALL
  be keyed on the entity **name** (independent of the source file), so the same table or enum
  appearing across multiple migration files merges — via the graph builder's id dedup — into a
  single node representing the current schema. The node SHALL record the `source_file` and
  location of its defining statement.
- A foreign key `ALTER TABLE <X> … FOREIGN KEY (…) REFERENCES <Y>` SHALL emit a `references`
  edge from the `sql_table` node for `X` to the `sql_table` node for `Y` (reusing the existing
  `references` relation, so `impact` / typed `explain` / query routing operate over it), for
  quoted and unquoted `<X>` / `<Y>`. Duplicate foreign keys across migrations SHALL collapse via
  edge dedup.
- Identifier handling SHALL reconcile quoted and unquoted references to the same table to one
  node. An **unquoted** identifier's canonical name SHALL be folded to lowercase (PostgreSQL
  semantics) and a **quoted** identifier SHALL keep its written case as its label; node ids are
  then produced through the existing case-folding id normalization (the Graphify id contract), so
  `CREATE TABLE users` and `REFERENCES "users"` resolve to the same `sql_table` node. Because that
  id normalization case-folds, case-variant identifiers (e.g. `"Users"` and `users`) reconcile to
  a single node rather than diverging — a deliberate consequence of the shared id scheme, not a
  SQL-specific rule.

Extraction is regex-based over the (Prisma-style) DDL; SQL is not fully parsed. Forms not matched
(e.g. inline column-level references inside a `CREATE TABLE` body, non-Prisma/non-Postgres
dialects) SHALL simply yield no edge rather than an error.

#### Scenario: Tables, enums, and foreign keys become a schema graph
- **WHEN** the graph is built over `.sql` migrations declaring `CREATE TABLE`, `CREATE TYPE … ENUM`,
  and `ALTER TABLE … FOREIGN KEY … REFERENCES` statements
- **THEN** the graph contains a `sql_table` node per table, a `sql_enum` node per enum, and a
  `references` edge between the owning and referenced tables for each foreign key

#### Scenario: Unquoted DDL is extracted
- **WHEN** a migration declares tables and foreign keys with unquoted identifiers, e.g.
  `CREATE TABLE IF NOT EXISTS skills (…)` and `… FOREIGN KEY (org_id) REFERENCES organizations(id)`
- **THEN** the graph contains the `sql_table` nodes (`skills`, `organizations`) and the `references`
  edge between them, exactly as it would for the equivalent quoted DDL

#### Scenario: Quoted and unquoted references to the same table reconcile
- **WHEN** a table is defined unquoted (`CREATE TABLE organizations`) and later referenced quoted
  (`REFERENCES "organizations"`), or defined quoted (`CREATE TABLE "organizations"`) and referenced
  unquoted (`REFERENCES organizations`)
- **THEN** the reference resolves to the single existing `sql_table` node (no dangling edge to a
  phantom node), because unquoted identifiers fold to lowercase and quoted identifiers preserve case

#### Scenario: Case-variant identifiers reconcile to one node
- **WHEN** a table is defined unquoted (`CREATE TABLE users`) and a foreign key references it with
  different case (`REFERENCES "Users"`)
- **THEN** the reference resolves to the single existing `sql_table` node (no dangling edge),
  because node ids are case-folded by the shared id normalization — case-variant identifiers are
  one node, consistent with the Graphify id contract rather than PostgreSQL's case sensitivity

#### Scenario: A table merges across migrations into one node
- **WHEN** the same table is created in one migration file and altered in others
- **THEN** the graph contains exactly one `sql_table` node for it (its id keyed on name, not file),
  with the foreign keys added by later migrations attached as `references` edges

#### Scenario: Schema is queryable via existing relation-aware ops
- **WHEN** an agent runs `impact` or `explain --relation references` on a `sql_table` node
- **THEN** the response returns the tables related by foreign keys, because the foreign keys are
  `references` edges the existing ops already traverse

#### Scenario: Goldens are unaffected by the unquoted extension
- **WHEN** the extractor goldens (which contain no `.sql` fixtures) are regenerated
- **THEN** they are unchanged — extending SQL identifier matching is additive and does not alter
  extraction of any other language
