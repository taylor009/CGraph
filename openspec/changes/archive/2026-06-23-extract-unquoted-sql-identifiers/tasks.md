## 1. Failing tests (define behavior first)

- [x] 1.1 In `tests/smoke/non_grammar_extractors_test.cpp`, add a case for fully **unquoted** DDL (`CREATE TABLE IF NOT EXISTS skills (...)`, `CREATE TYPE status AS ENUM (...)`, `ALTER TABLE skills ADD ... FOREIGN KEY (org_id) REFERENCES organizations(id)`): assert `sql_table` nodes `skills`/`organizations`, the `sql_enum` node, and the `skills -> organizations` references edge all exist.
- [x] 1.2 Add a **mixed reconciliation** case: define a table unquoted and reference it quoted (and the reverse), asserting the FK edge targets the single existing node id (no dangling endpoint).
- [x] 1.3 Add the **case-folded id** case: a `REFERENCES "Users"` against `CREATE TABLE users` resolves to the single node id (case-variant identifiers reconcile under the make_id contract, not distinct); assert `IF NOT EXISTS` is never captured as a table name.
- [x] 1.4 Build and confirm the new asserts FAIL against current `extract_sql` (records the pre-fix red state).

## 2. Implementation

- [x] 2.1 Add a `normalize_sql_ident(std::string_view raw)` helper in `non_grammar_extractors.cpp`: strip surrounding quotes and preserve case when quoted; lowercase when unquoted. Return the table/enum key.
- [x] 2.2 Replace the quoted-only identifier captures in the `CREATE TABLE`, `CREATE TYPE … ENUM`, `ALTER TABLE … RENAME TO`, and `FOREIGN KEY … REFERENCES` regexes with the quoted-or-unquoted sub-pattern (preserving the optional `schema.` qualifier and the `IF NOT EXISTS` anchor), keeping the quote state available to the normalizer.
- [x] 2.3 Route every captured table/enum identifier (CREATE name, RENAME-to name, FK owner, FK target) through `normalize_sql_ident` before `make_id`, so node ids and edge endpoints fold identically.

## 3. Verify

- [x] 3.1 Run `ctest --preset default -R cgraph_non_grammar_extractors_test` — new asserts pass.
- [x] 3.2 Run the goldens test `-R cgraph_extractor_goldens_test`; regenerate goldens and confirm zero movement on existing (non-SQL) fixtures.
- [x] 3.3 Run the full suite (`ctest --preset default`) and report pass count.
- [x] 3.4 Rebuild the turing backend graph; 0 dangling edges before/after. sql_table nodes 118 -> 124 (+6, the 6 unquoted CREATE TABLEs in the 118 scanned migrations; 135 are quoted). Corrected the proposal's inflated "1,389/37%" (it counted node_modules, which is not scanned).

## 4. Ship

- [ ] 4.1 Commit (conventional `fix(detect):`/`feat(detect):`), push, and open the PR with the parity before/after numbers.
- [ ] 4.2 After merge, run `/opsx:archive` to fold the delta into `openspec/specs/deterministic-graph-pipeline/spec.md`.
