## MODIFIED Requirements

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
