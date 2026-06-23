## Context

`extract_sql` (`src/engine/non_grammar_extractors.cpp`) is regex-based over Prisma-style DDL.
Today every identifier pattern requires double quotes — `CREATE\s+TABLE\s+…"([^"]+)"`,
`REFERENCES\s+…"([^"]+)"`, etc. Commit `18409e5` taught these patterns to skip an optional
`"schema".` qualifier and to handle `RENAME TO`, but they still only match quoted names.

Real migrations mix styles freely: `CREATE TABLE IF NOT EXISTS skills (` and
`… REFERENCES organizations(id)` (unquoted) sit alongside `CREATE TABLE "Brand"` and
`REFERENCES "public"."Project"` (quoted). The unquoted ~37% are dropped silently. The
extractor keys `sql_table`/`sql_enum` nodes on the entity **name** so cross-migration
occurrences merge; that contract means a quoted definition and an unquoted reference of the
same table must resolve to the **same id**, which forces an explicit case-folding rule.

## Goals / Non-Goals

**Goals:**
- Extract unquoted `CREATE TABLE` / `CREATE TYPE … ENUM` / `ALTER TABLE … RENAME TO` /
  `FOREIGN KEY … REFERENCES` identifiers in addition to the quoted form.
- Reconcile quoted and unquoted references to the same table to one node id via correct
  PostgreSQL case-folding (unquoted → lowercase; quoted → preserved).
- Keep extraction deterministic and golden-stable; no new node/edge kinds.

**Non-Goals:**
- Inline column-level `REFERENCES` inside a `CREATE TABLE` body (still yields no edge).
- Full SQL parsing or non-Postgres dialect identifier rules (e.g. MySQL backticks,
  SQL Server `[brackets]`).
- Any change to the graph builder, dedup, or downstream ops.

## Decisions

- **One identifier sub-pattern, applied everywhere.** Replace each `"([^"]+)"` capture with a
  pattern that matches either a quoted or an unquoted identifier and an optional `schema.`
  prefix, e.g. `(?:(?:"[^"]+"|[A-Za-z_][A-Za-z0-9_$]*)\s*\.\s*)?("[^"]+"|[A-Za-z_][A-Za-z0-9_$]*)`.
  Rationale: the quoted-vs-unquoted distinction must be preserved into the capture so the
  normalizer can apply the right case rule — stripping quotes in the regex would lose it.
  Alternative considered: two separate regexes per statement (quoted, unquoted) — rejected as
  duplicative and harder to keep in sync across four statement forms.
- **Normalize at the capture site, not in `make_id`.** Add a small helper
  `normalize_sql_ident(raw)` that returns the table key: if `raw` is `"..."`, strip the quotes
  and preserve case; otherwise lowercase it. Feed its output into the existing
  `make_id("sql_table:" + key)`. Rationale: `make_id` is a generic id slugger shared by every
  extractor — SQL case-folding is a SQL-specific concern and belongs at the SQL call site.
- **Keep the `schema.` qualifier handling from 18409e5.** The optional qualifier is matched and
  discarded for both quoted and unquoted forms; the table component is what gets normalized.
- **Defer the final id authority to `make_id`.** `make_id` (normalize.cpp) already
  case-folds every id via `UTF8PROC_CASEFOLD` — the Graphify id contract, which the project
  treats as load-bearing parity and which this change must NOT bypass. So reconciliation of
  quoted vs unquoted references comes for free at the id layer; `normalize_sql_ident` exists to
  strip quotes and produce a sensible Postgres-folded LABEL, not to control id identity. A
  consequence the spec makes explicit: case-variant identifiers (`"Users"` vs `users`) reconcile
  to ONE node — supporting PostgreSQL's case sensitivity here would require a case-sensitive id
  path, which would break the parity contract and is explicitly out of scope.

## Risks / Trade-offs

- **Goldens shift on any repo with unquoted DDL** → the goldens fixtures contain no `.sql`, so
  `extractor_goldens_test` stays green; the change is additive there. Regenerate and diff to
  confirm zero movement on existing fixtures, and add a dedicated unquoted-SQL fixture/asserts.
- **Over-broad unquoted identifier regex could match keywords** (e.g. capturing `IF` from
  `CREATE TABLE IF NOT EXISTS`) → anchor the patterns with the existing
  `(?:IF\s+NOT\s+EXISTS\s+)?` clause before the identifier capture, and cover this exact form
  in tests.
- **Reconciliation must not silently mis-route a reference** → tests assert quoted-def/unquoted-ref
  and the reverse both land on the single node id, plus a case-variant reference
  (`REFERENCES "Users"` against `CREATE TABLE users`) resolving via the case-folded id rather than
  dangling.
- **Parity contract**: node/edge counts rise on real repos. Expected and desired (the schema
  was under-counted); document the before/after in the PR per repo convention.
