## ADDED Requirements

### Requirement: Go source files are extracted through the configured tree-sitter path
The deterministic extractor SHALL handle `.go` files (which project file detection already maps
to `DetectedLanguage::Go`) via a declarative `LanguageConfig` over the shared walker (no bespoke
extractor translation unit). For each Go file it SHALL emit: a file node; `type` nodes for named types
(`type_spec` and `type_alias` — structs, interfaces, aliases); `function` nodes for
`function_declaration` and `method_declaration` (methods keep their bare name; the receiver is
not resolved); module stub nodes + file-level `imports` edges for each `import_spec` quoted path
(resolved against project files by suffix, unresolved stubs dropped); and raw calls from
`call_expression` — a `selector_expression` target is recorded as a same-file member call
carrying the bare field name. IDs flow through the existing normalization contract unchanged.

#### Scenario: A Go file produces real symbols
- **WHEN** the graph is built over a project containing `type Service struct{}`, a
  pointer-receiver method `func (s *Service) Run()`, and a plain function
- **THEN** the graph contains a `type` node "Service" and `function` nodes "Run" and the plain
  function, each contained by the file node

#### Scenario: Go calls resolve like other languages
- **WHEN** a Go function body calls a same-file function `helper()` and a package function
  `fmt.Println(...)`
- **THEN** `helper` yields a plain raw call (project-resolvable) and `Println` a member call that
  resolves only within the caller's file, never by project-wide name guess

#### Scenario: Persisted graphs from the pre-Go extractor are not fast-loaded
- **WHEN** a daemon restarts over any project with an index manifest written by a binary older
  than this change
- **THEN** the version key mismatch forces a full rebuild instead of serving the stale
  symbol-less graph

### Requirement: Detected-but-unextracted files are counted per language
The pipeline SHALL maintain a per-language map of detected files that no registered extractor
handles (`unextracted`: language name -> file count), exposed by `unextracted_counts` over any
detected-file set and included in the one-shot `stats.json`. Registry membership is answered by
`has_registered_extractor` (tree-sitter config or non-grammar extractor). `Unknown` files are
excluded (they are not detected as project files).

#### Scenario: A coverage hole is visible in one-shot stats
- **WHEN** `cgraph --root` runs over a project containing a `.cs` file
- **THEN** `stats.json` contains `"unextracted": {"csharp": 1}` alongside the build counters

#### Scenario: Total coverage yields an empty map
- **WHEN** every detected file's language has a registered extractor
- **THEN** `unextracted` is an empty object, not absent
