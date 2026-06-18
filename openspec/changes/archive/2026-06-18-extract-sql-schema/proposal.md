## Why

`.sql` files are now in the graph, but only as opaque file-level `sql_file` nodes — the *schema* they
define is still invisible. On turing that schema is substantial and richly relational: **144 distinct
tables, 62 enums, and 426 foreign keys**. The FK edges are the payoff: with the data model in the
graph, an agent can ask "what references the `User` table?" (`explain`/`who references`), "what breaks
if I change `Project`?" (`impact`), or trace a path from a TS call site through the data layer.

The DDL is Prisma-generated and highly regular (`CREATE TABLE "Brand" (`, `CREATE TYPE "Role" AS
ENUM (...)`, `ALTER TABLE "X" ADD FOREIGN KEY(...) REFERENCES "Y"(...)`), so regex extraction via the
existing non-grammar pattern (apex/msbuild) suffices — no SQL grammar to vendor.

The one real modeling decision is **migration merge**: Prisma migrations are append-only, so a table
is created once and altered across hundreds of files. Keying table/enum nodes on their *name* (not
their file path) makes the graph builder — which dedups nodes by id — collapse every migration's
references to a single node per entity, yielding the *current schema* rather than thousands of dupes.

## What Changes

`extract_sql` is extended (it already emits the per-file `sql_file` node) to also emit, by regex over
the file's DDL:

- **`sql_table` nodes** from `CREATE TABLE "X"` — id `make_id("sql_table:" + X)` (name-keyed, no
  path), so the same table across migrations merges to one node, defined (source_file/line) at its
  `CREATE`.
- **`sql_enum` nodes** from `CREATE TYPE "X" AS ENUM` — id `make_id("sql_enum:" + X)`, name-keyed.
- **`references` edges** (table → table) from `ALTER TABLE "X" ... FOREIGN KEY (...) REFERENCES "Y"`,
  endpoints `make_id("sql_table:" + X)` → `make_id("sql_table:" + Y)`. Reusing the existing
  `references` relation means `impact`, `explain --relation references`, and the query router's
  "references to X" all light up over the schema with no new query logic.

Duplicate nodes (same name across migrations) and duplicate FK edges collapse via the graph builder's
existing id/edge-key dedup.

## Capabilities

### Modified Capabilities

- `deterministic-graph-pipeline`: SQL DDL is extracted into a schema graph — name-keyed `sql_table` /
  `sql_enum` nodes (merged across migrations) joined by `references` (foreign-key) edges — on top of
  the file-level `sql_file` nodes.

## Non-Goals

- **Columns as nodes** — deferred; the table/enum/FK level is the high-value relational slice. (A
  follow-up can add `sql_column` nodes.)
- **Linking tables to Prisma/TS models** (`@@map` resolution across languages) — the further B3
  follow-up.
- **Inline column-level `REFERENCES`** and non-Prisma SQL dialects — only the `ALTER TABLE … ADD …
  FOREIGN KEY … REFERENCES` form (what Prisma emits) is extracted; other forms are a follow-up, not a
  regression (they simply yield no edge).
- **Dropping the `sql_file` node** — it stays for file-level discoverability; schema nodes are additive.

## Impact

- `src/engine/non_grammar_extractors.cpp` (extend `extract_sql` with the three regex passes; name-keyed
  ids + FK edges built directly, since `add_node` is path-keyed), `tests/smoke/non_grammar_extractors_test.cpp`.
- **Measured target (turing):** ~144 `sql_table` + 62 `sql_enum` nodes and ~426 `references` edges
  (collapsed from 156 CREATE / 426 FK statements across the migration history).
- **Parity:** goldens contain no `.sql` fixtures → unaffected (confirmed by running them). SQL ids are
  net-new (Graphify extracts no SQL), so the name-keyed scheme introduces no parity conflict.
- Verified by: extractor unit tests (a multi-statement migration yields the expected name-keyed
  table/enum nodes + FK edges; a second file with the same table merges, contributing only edges); a
  turing rebuild hitting the counts; an `impact`/`explain` query over a `sql_table` returning its FK
  neighbors; goldens unchanged; full suite.
