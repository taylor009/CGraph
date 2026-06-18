# Design — file-level SQL indexing

## The node

One node per `.sql` file, emitted by `extract_sql(context)` via the existing `add_node` helper:

```
id          : make_id(source_file + ":sql_file:" + filename)   (stable, path-derived)
label       : the file's name (e.g. "migration.sql", "check_threads.sql")
kind        : "sql_file"
source_file : the file's path
(no source_location, no symbols, no edges)
```

`label` is the filename; the full path lives in `source_file`, so the `file:` query filter and
path-substring search disambiguate same-named files (every Prisma migration is `migration.sql`). A
distinctive `kind = "sql_file"` lets an agent filter to the data layer (`query kind:sql_file`) and
keeps the door open for future `sql_table` / `sql_column` kinds.

## Flow (why this is enough for discoverability)

```
detect_language(".sql") -> DetectedLanguage::Sql
   │
   ├─ classify_watched_file: detect_language != Unknown  => WatchedFileKind::Code
   │     (so .sql is detected, extracted deterministically, AND watched — incremental
   │      updates fold .sql edits into the resident graph)
   │
   └─ extract pipeline: extract_non_grammar_language(Sql, ctx) -> one sql_file node
         => appears in query / search / explain, is enrichable, is seam-anchorable
```

`extract_non_grammar_language` is already the first stage of the extractor dispatch (it short-circuits
before the grammar extractors), and McpConfig/MsBuild prove the pattern — `Sql` is one more case.

## Why file-level, not symbol-level (staged)

The dogfood justified *coverage*, not yet *structure*. File-level nodes deliver the immediate wins
(discoverable, enrichable, anchorable) for ~20 lines and zero SQL parsing. Symbol-level — regex or a
tree-sitter SQL grammar turning `CREATE TABLE "Thread"` into a `sql_table` node, columns into
`sql_column` nodes, and linking a table to its `@@map`'d Prisma TS model — is materially more work
and is deferred until file-level proves its keep. Keeping `kind = "sql_file"` (not the generic
`"file"`) means symbol-level can be added later without reclassifying these nodes.

## Parity

Extraction output is a parity surface, but the goldens are fixed fixtures with no `.sql` files, so
they are unaffected. The change is additive: no existing extracted language changes. Confirmed by
running `extractor_goldens` + `pack_context_parity`.

## Risks

- **Label collisions** (every `migration.sql`) — acceptable: ids are path-derived (unique),
  `source_file` disambiguates, and `file:` search narrows. Symbol-level extraction later gives real
  names.
- **Volume** — a migration-heavy repo adds one node per migration (turing: +414). Negligible against
  7.6k code nodes, and they cluster by path; no perf concern (the dogfood build was 2.8s / 124 MB).
