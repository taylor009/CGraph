# host-integration-mcp Specification

## Purpose
TBD - created by archiving change build-native-graphify-variant. Update Purpose after archive.
## Requirements
### Requirement: Host-agnostic skill contract
The system SHALL provide a host-agnostic skill contract that describes deterministic graph use, chunk plan dispatch, semantic fragment schema, and disk-based success signals.

#### Scenario: Host receives chunk work
- **WHEN** a host integration requests semantic enrichment work
- **THEN** it receives chunk instructions and a schema for writing validated fragment files

### Requirement: Reference host integrations
The system SHALL implement two reference host integrations before long-tail fan-out: one hook-based host integration and one always-on host integration.

#### Scenario: Hook-based host uses thin client
- **WHEN** the hook-based host invokes graph commands during an agent loop
- **THEN** it calls the thin client instead of starting a language runtime or running a full graph build

#### Scenario: Always-on host uses skill contract
- **WHEN** the always-on host needs graph context or semantic enrichment
- **THEN** it follows the host-agnostic skill contract and routes graph commands through the native client or daemon

### Requirement: MCP server front door
The system SHALL provide an MCP server that exposes graph tools through JSON-RPC over stdio and
forwards graph operations to the daemon protocol. Tool descriptions and parameter schemas SHALL
state the engine's actual default behavior; in particular the `graph_context` tool SHALL
document `adaptive` as the default `gather` mode, matching the engine default.

#### Scenario: MCP query forwards to daemon
- **WHEN** an MCP client calls the graph query tool
- **THEN** the MCP server sends the corresponding request to the daemon and returns the daemon response

#### Scenario: Advertised defaults match engine defaults
- **WHEN** an MCP client reads the `graph_context` tool description and schema and then calls
  the tool omitting `gather`
- **THEN** the behavior it receives is the behavior the description documents as the default

### Requirement: No provider logic in integrations
The system SHALL keep model-provider selection and token spending under host control rather than embedding provider-specific logic in the native binary or MCP server.

#### Scenario: Integration dispatches host model work
- **WHEN** semantic enrichment requires model reasoning
- **THEN** the host integration dispatches the work using the host's available agent or model mechanisms

### Requirement: Integration fan-out gate
The system SHALL delay additional host integrations until deterministic parity and daemon experience benchmarks pass.

#### Scenario: Gate fails
- **WHEN** benchmarks do not demonstrate improved cold query latency and time-to-first-graph against the Python reference
- **THEN** additional host integration work remains blocked pending design or implementation changes

