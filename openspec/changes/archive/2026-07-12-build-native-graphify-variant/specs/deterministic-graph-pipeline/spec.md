## ADDED Requirements

### Requirement: Native deterministic pipeline
The system SHALL provide a native one-shot pipeline that detects project files, extracts language fragments, builds and deduplicates the graph, clusters communities, analyzes graph metrics, and exports deterministic outputs.

#### Scenario: One-shot graph build completes
- **WHEN** the user runs the native one-shot command against a supported project root
- **THEN** the system produces a deterministic graph without requiring a daemon or semantic enrichment

#### Scenario: Unsupported file is skipped safely
- **WHEN** the file detector encounters an unsupported or ignored file
- **THEN** the system excludes that file without aborting the pipeline

### Requirement: Graphify fragment contract
The system SHALL emit and consume extraction fragments compatible with Graphify's fragment shape, including `nodes`, `edges`, optional `hyperedges`, source metadata, relation names, confidence labels, and confidence scores where applicable.

#### Scenario: Extractor emits compatible fragment
- **WHEN** a supported source file is extracted
- **THEN** the extracted fragment contains Graphify-compatible node and edge records for downstream build and merge stages

#### Scenario: Extractor failure is contained
- **WHEN** one file extractor throws or fails to parse
- **THEN** the system records a warning and continues the batch with an empty fragment for that file

### Requirement: ID normalization parity
The system SHALL normalize node identifiers byte-for-byte compatibly with Graphify's `_make_id` and build normalization behavior, including Unicode normalization, word-character handling, underscore collapse, and case folding.

#### Scenario: Unicode fixture matches reference
- **WHEN** the native normalizer runs against ASCII, accented, composed, decomposed, CJK, and Cyrillic identifier fixtures
- **THEN** every output matches the Python Graphify reference output exactly

### Requirement: Tree-sitter extraction parity
The system SHALL use tree-sitter grammars and per-language extraction logic to match Graphify's node and edge sets for supported language fixtures.

#### Scenario: Language golden matches reference
- **WHEN** a native extractor runs against a ported Graphify language fixture
- **THEN** the produced node and edge sets match the reference fixture except for documented ordering differences

### Requirement: Graph build and dedup parity
The system SHALL merge fragments into a graph with Graphify-compatible per-file deduplication, cross-file idempotency, semantic merge behavior, and raw-call resolution.

#### Scenario: Duplicate symbols merge correctly
- **WHEN** multiple fragments contain semantically duplicate nodes
- **THEN** the build stage merges them according to the reference dedup pipeline and avoids ghost duplicate nodes

#### Scenario: Ambiguous raw call remains unresolved
- **WHEN** a raw call matches only common or ambiguous names
- **THEN** the system avoids creating a misleading extracted call edge

### Requirement: Graph analysis and exports
The system SHALL compute community assignments, centrality-derived god-node rankings, cross-community surprise signals, and Graphify-compatible exports.

#### Scenario: Graph JSON is compatible
- **WHEN** the native pipeline exports `graph.json`
- **THEN** existing Graphify-compatible loaders can parse the output as NetworkX node-link data

#### Scenario: Analysis output is available
- **WHEN** clustering and analysis complete
- **THEN** clients can access community, centrality, and surprise metadata needed by query and reporting features

### Requirement: Verification gates
The system SHALL include automated parity tests, sanitizer builds, fuzz targets, and benchmarks before long-tail language, exporter, or host integration fan-out.

#### Scenario: Parity gate blocks fan-out
- **WHEN** native one-shot output has unexplained missing or spurious graph nodes or edges against the reference corpus
- **THEN** implementation does not proceed to long-tail integrations until the difference is fixed or explicitly documented
