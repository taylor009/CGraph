# Always-On Graph Integration

Use this skill when an always-on host wants graph context or semantic enrichment while keeping provider choice and token spend under host control.

## Contract

Follow the Host Skill Contract in `docs/host-skill-contract.md`.

The native binary provides deterministic graph commands, chunk planning, fragment validation, semantic cache records, and local graph mutation. The host owns model selection and dispatch. This integration has no provider logic and does not require provider credentials.

## Command Routing

Use `cgraph-client` for all graph operations:

- `status` to observe deterministic readiness and `enrichment_state`
- `query` for graph search
- `path` for graph paths
- `explain` for node neighborhoods
- `update .` for full stat-index rescan
- `shutdown` for clean daemon exit

Set `CGRAPH_PROJECT_ROOT` to the project root. Set `CGRAPH_DAEMON` only when the host must use a specific daemon binary.

## Semantic Work

When status or host policy indicates stale semantic inputs, request or compute chunk work according to the contract. Host workers write completed fragments as `chunk_NN.json` files into the semantic drop directory.

The daemon validates every fragment before mutation. Malformed fragments are rejected and leave the graph unchanged. Valid fragments update the semantic cache by content hash.

## Runner

Use `integrations/always-on/cgraph-always-on.sh` as the reference loop for host processes that need periodic graph status checks and optional deterministic refreshes.
