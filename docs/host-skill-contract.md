# Host Skill Contract

This contract defines how host integrations use the native graph daemon without embedding model-provider logic in the binary. Hosts own model selection, token spend, and agent dispatch. The native tool owns deterministic graph commands, chunk planning, fragment validation, and local graph mutation.

## Deterministic Graph Commands

Hosts call the thin client or daemon protocol for graph operations. Requests use the current length-prefixed JSON frame format when talking to the daemon directly.

Supported operations:

- `query`: Search graph nodes by query text.
- `path`: Return a graph path between source and target ids.
- `explain`: Return a node and its neighboring edges.
- `update`: Apply deterministic updates. `update .` means full stat-index rescan.
- `status`: Return daemon, graph, cache, and enrichment state.
- `shutdown`: Ask the per-root daemon to exit cleanly.

Hosts should prefer the thin client command surface unless they are implementing an MCP or always-on bridge that already speaks local JSON frames.

## Chunk Plan Dispatch

The native tool emits semantic chunk plans for uncached or stale documentation, media, and semantic inputs. Code extraction stays deterministic and does not require host model work.

Each chunk contains bounded file inputs with:

- source path
- input kind, such as document or media
- content hash
- byte size

Hosts dispatch each chunk to their own agent or model workflow. A completed chunk writes exactly one fragment file named `chunk_NN.json` into the configured semantic drop directory, where `NN` is the chunk index. Cached content is skipped when a valid cache record exists for the same content hash and fragment path.

## Semantic Fragment Schema

Dropped fragments must use the node-link fragment shape:

- `nodes`: array of node objects with required `id` and `label`
- `edges`: array of edge objects with required `source`, `target`, and `relation`
- `hyperedges`: optional array of hyperedge objects with `id`, `nodes`, and `relation`
- optional `source_file`, `source_location`, `type` or `kind`, `confidence`, `confidence_score`, `properties`, and `warnings`

The native daemon validates every dropped fragment before graph mutation. Malformed JSON or schema violations are rejected and must not alter the graph snapshot. Valid fragments are merged through the daemon single-writer path and update the semantic cache by content hash.

## Disk Success Signals

Hosts signal completion by writing a complete `chunk_NN.json` file to the semantic drop directory. The native watcher discovers created or modified `chunk_NN.json` files and ignores unrelated names.

Recommended host write sequence:

- write to a temporary file in the same directory
- flush and close the file
- atomically rename it to `chunk_NN.json`

Native success is observable through:

- `status.enrichment_state`
- `status.enrichment_pending`
- `status.enrichment_running`
- `status.enrichment_stale`
- `status.enrichment_failed`
- the semantic cache record for the source content hash
- the graph snapshot containing the merged fragment nodes and edges

Failure is observable through rejected validation errors, failed enrichment counts, and unchanged graph snapshots. Hosts should retry by writing a corrected `chunk_NN.json` file with the same chunk index.
