# Design — SQL schema extraction (B2: tables + enums + FK edges)

## Regex passes in extract_sql

All case-insensitive, over `context.source`. Identifiers are double-quoted in Prisma DDL.

```
CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?"([^"]+)"        -> sql_table node (group 1 = name)
CREATE\s+TYPE\s+"([^"]+)"\s+AS\s+ENUM                       -> sql_enum  node (group 1 = name)
ALTER\s+TABLE\s+"([^"]+)"\s+ADD\s+(?:CONSTRAINT\s+"[^"]+"\s+)?
   FOREIGN\s+KEY\s*\([^)]*\)\s*REFERENCES\s+"([^"]+)"       -> references edge (g1 = owner, g2 = target)
```

(The turing form `ALTER TABLE "Brand" ADD FOREIGN KEY("projectId")REFERENCES "Project"("id")` has no
CONSTRAINT name and tight whitespace — the optional group + `\s*` handle both that and the
`ADD CONSTRAINT "..." FOREIGN KEY` form.)

## Node + edge construction (name-keyed, not via add_node)

`add_node` builds path-keyed ids (`make_id(source_file + ":" + kind + ":" + label)`), which would NOT
merge across migrations. Schema nodes are therefore built directly:

```
table node:  id = make_id("sql_table:" + name), label = name, kind = "sql_table",
             source_file = this file, source_location = the CREATE line
enum node:   id = make_id("sql_enum:"  + name), label = name, kind = "sql_enum", ...
FK edge:     { source = make_id("sql_table:" + owner),
               target = make_id("sql_table:" + target),
               relation = "references" }
```

`make_id` is pure normalization (NFKC + casefold + non-word-run collapse) with **no path component**,
so `make_id("sql_table:Brand")` is identical in every migration file. The graph builder dedups nodes
by id and edges by key, so:

- the same table CREATEd/ALTERed across N migrations → **one** `sql_table` node (first CREATE wins,
  carrying its source_file/line); ALTER-only files contribute edges, not nodes;
- duplicate FK statements → one `references` edge.

This is the load-bearing decision: name-keyed ids turn append-only migration history into the current
schema.

## Why `references`

Reusing the existing edge relation means the schema is immediately queryable through shipped ops:
`impact <table>` (blast radius), `explain <table> --relation references`, and the query router's
"references to X" / "who references X" all work with no new query logic. A FK is conceptually "X
references Y", so the relation reads correctly.

## Coexistence with the file-level node

`extract_sql` still emits the per-file `sql_file` node (stage a). A migration file thus yields its
`sql_file` node plus the name-keyed schema nodes/edges it declares. (A `sql_file → sql_table`
"defines" edge is a possible nicety, deferred — it adds per-file edges without changing the schema
view.)

## Parity & ids

Extraction output is a parity surface, but the goldens have no `.sql` fixtures, so they are
unaffected. Graphify extracts no SQL, so there is no parity baseline the name-keyed id scheme could
conflict with — confirmed by running `extractor_goldens` + `pack_context_parity`.

## Risks

- **Casefold collisions** — `make_id` casefolds, so `"Brand"` and `"brand"` would share an id. Prisma
  uses distinct PascalCase names; a real collision is vanishingly unlikely and, if it occurred, would
  merge two same-cased-name tables (acceptable, rare).
- **Dangling FK target** — a `REFERENCES "Y"` where `Y` has no local `CREATE TABLE` yields an edge to
  a node absent from the graph (external/dropped table). Rare in a single Prisma schema; the edge is
  harmless (node-link tolerates it) and a later pass could prune.
- **Non-Prisma DDL** — inline `col TYPE REFERENCES "Y"` and other dialects aren't matched; they
  produce no edge (not a regression), and are a clean follow-up.
