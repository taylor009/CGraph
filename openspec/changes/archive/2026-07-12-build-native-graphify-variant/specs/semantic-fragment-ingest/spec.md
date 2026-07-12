## ADDED Requirements

### Requirement: Host-orchestrated semantic enrichment
The system SHALL support semantic enrichment through host-generated fragments and SHALL NOT require the native binary to call LLM APIs or store model provider credentials.

#### Scenario: No model call from binary
- **WHEN** semantic enrichment is requested
- **THEN** the native tool emits work for the host to perform and does not make an LLM API request itself

### Requirement: Chunk plan generation
The system SHALL generate chunk plans for uncached or stale docs, media, and other semantic inputs, grouped into bounded chunks suitable for host subagents.

#### Scenario: Changed doc enters chunk plan
- **WHEN** a documentation file changes and no valid semantic cache entry exists for its content hash
- **THEN** the next enrichment plan includes that file in a chunk for host processing

#### Scenario: Cached content is skipped
- **WHEN** a semantic input has an unchanged content hash with a valid cached fragment
- **THEN** the chunk plan excludes that input from new host work

### Requirement: Fragment validation
The system SHALL validate every dropped semantic fragment before merging it into the graph.

#### Scenario: Valid fragment merges
- **WHEN** the host writes a valid `chunk_NN.json` fragment to the drop directory
- **THEN** the system merges it through the single-writer path and updates the semantic cache

#### Scenario: Malformed fragment is rejected
- **WHEN** the host writes malformed JSON or a fragment that violates the schema
- **THEN** the system rejects the fragment, records an error, and leaves the graph intact

### Requirement: Enrichment state visibility
The system SHALL expose whether semantic enrichment is idle, pending, running, stale, or failed for relevant files and for the graph as a whole.

#### Scenario: Stale enrichment is visible
- **WHEN** a semantic input changes after deterministic graph output is available
- **THEN** status reports enrichment as stale or pending without blocking deterministic graph queries

### Requirement: Incremental semantic cache
The system SHALL store semantic enrichment results by content hash and invalidate them when source content changes.

#### Scenario: File edit invalidates semantic cache
- **WHEN** a semantic source file's content hash changes
- **THEN** the prior semantic fragment is marked stale and the file is eligible for the next chunk plan
