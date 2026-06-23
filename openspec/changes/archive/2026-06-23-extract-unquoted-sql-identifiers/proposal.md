## Why

The deterministic SQL extractor only matches **double-quoted** identifiers, so any unquoted
DDL is silently skipped — the table gets no `sql_table` node and foreign keys touching it
are dropped from `impact` / `explain` / query routing, with no error or warning. On the
turing backend's scanned migrations this is currently small (6 of 141 `CREATE TABLE`s are
unquoted, so 6 tables and their FKs were missing), but it is unbounded: hand-written and
non-Prisma migrations use unquoted identifiers freely, and the gap is invisible until a
reference dangles. Extracting both forms makes the schema graph faithful regardless of
quoting style.

(Note: an earlier estimate of "~1,389 unquoted `CREATE TABLE`s / ~37%" counted raw matches
across the entire tree including `node_modules`/worktrees, which the graph does not scan; the
real in-graph universe is the 118 tracked migration files.)

## What Changes

- `CREATE TABLE`, `CREATE TYPE … AS ENUM`, `ALTER TABLE … RENAME TO`, and `ALTER TABLE …
  FOREIGN KEY … REFERENCES` extraction SHALL match **unquoted** identifiers in addition to
  the existing quoted form (both with the optional `schema.` qualifier already supported).
- An unquoted identifier's canonical name SHALL fold to lowercase (PostgreSQL semantics) and a
  quoted identifier SHALL keep its written case as its label; node ids continue through the
  existing case-folding id normalization, so an unquoted `REFERENCES organizations` and a
  quoted `CREATE TABLE "organizations"` reconcile to the same `sql_table` node. The fix does
  NOT bypass that id normalization — so case-variant identifiers (`"Users"` vs `users`)
  reconcile to one node, consistent with the Graphify id contract.
- Extended extractor test coverage for unquoted DDL, mixed quoted/unquoted reconciliation,
  and the case-folded id boundary. Extractor goldens regenerated and verified.

Non-goals (unchanged, still yield no edge rather than an error): inline column-level
`REFERENCES` inside a `CREATE TABLE` body, non-Prisma/non-Postgres dialect quirks, and full
SQL parsing.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `deterministic-graph-pipeline`: the "SQL DDL is extracted into a schema graph" requirement
  changes — table/enum/FK/rename matching is no longer restricted to quoted identifiers, and
  node ids gain explicit Postgres case-folding semantics so quoted and unquoted references to
  the same table reconcile.

## Impact

- **Code**: `src/engine/non_grammar_extractors.cpp::extract_sql` (regexes + an identifier
  normalization helper). No change to node/edge types or the graph builder.
- **Tests**: `tests/smoke/non_grammar_extractors_test.cpp`; `tests/smoke/extractor_goldens_test.cpp`
  (goldens regenerated).
- **Graph output / parity**: backend (and any unquoted-DDL repo) gains `sql_table` nodes and
  `references` edges that were previously absent — a graph-construction parity surface. More
  complete schema coverage in `impact`/`explain`/routing; node/edge counts increase.
- **Builds on** commit `18409e5` (schema-qualified + `RENAME TO` handling).
