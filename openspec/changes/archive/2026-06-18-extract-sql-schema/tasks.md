## 1. Schema extraction (engine)

- [x] 1.1 `non_grammar_extractors_test.cpp`: a migration with `CREATE TYPE "Role" AS ENUM`,
      `CREATE TABLE "Brand"`/`"Project"`, and `ALTER TABLE "Brand" … FOREIGN KEY … REFERENCES
      "Project"` yields `sql_table` nodes (Brand, Project), a `sql_enum` (Role), the `sql_file` node,
      and exactly one `references` edge whose endpoints equal the Brand/Project table node ids.
- [x] 1.2 `non_grammar_extractors_test.cpp`: the same table CREATEd in a DIFFERENT file produces an
      identical (path-independent) node id — the name-keyed merge property.
- [x] 1.3 Extended `extract_sql`: three regex passes (CREATE TABLE → `sql_table`, CREATE TYPE…ENUM →
      `sql_enum`, ALTER…FOREIGN KEY…REFERENCES → `references` edge), name-keyed ids built directly;
      `sql_file` node retained. (Raw-string delimiter `R"rx(...)rx"` since the patterns contain `)"`.)

## 2. Verify

- [x] 2.1 Parity goldens unchanged (`extractor_goldens` + `pack_context_parity` pass).
- [x] 2.2 Full suite `ctest --preset default` → 60/60.
- [x] 2.3 Live: rebuilt `turing` → **117 `sql_table`, 41 `sql_enum`, 414 `sql_file` nodes, 231
      `references` FK edges** between tables (collapsed from 156 CREATE / 426 FK statements via
      name-keyed merge + edge dedup; ALTER-form FKs only). `impact sql_table_user
      (dependents, references)` → 145 dependents, traversing the FK closure.