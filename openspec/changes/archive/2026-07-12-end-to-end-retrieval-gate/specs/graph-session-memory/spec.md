## MODIFIED Requirements

### Requirement: Checkpoint recall via the recall op
The daemon SHALL expose a `recall` op (MCP tool `graph_recall`) that accepts optional `query`
and optional `limit` and returns checkpoint nodes ordered newest-first by `created_at`. Each
returned entry SHALL include the checkpoint's body as a source snippet (read from its
`source_file`, subject to the existing snippet caps) and a brief of each code node the
checkpoint `concerns`. When `query` is supplied, results SHALL be filtered by case-insensitive
lexical match over title, tags, and the checkpoint body text. The response size SHALL be
bounded by `limit` and the existing snippet caps.

#### Scenario: Recall returns newest checkpoints first with bodies and links
- **WHEN** multiple checkpoints exist and `recall` is called
- **THEN** they are returned newest-first, each carrying its body snippet and briefs of its
  `concerns` targets, capped by `limit`

#### Scenario: Checkpoint body is snippet-readable through context
- **WHEN** `graph_context` or `explain` is called on a checkpoint node id
- **THEN** the checkpoint body text is returned as the focal source snippet

#### Scenario: Recall matches on body content
- **WHEN** a checkpoint's body contains a term that appears in neither its title nor its tags
  and `recall` is called with that term as `query`
- **THEN** the checkpoint is returned
