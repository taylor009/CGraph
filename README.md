# CGraph

CGraph is a native C++ graph analysis engine for source trees. It scans a
project, extracts code structure into a deterministic graph, writes portable
exports, and exposes the same graph through a local daemon, thin client, and MCP
server.

The project is built around a simple split:

- `cgraph` runs a one-shot scan and writes graph artifacts to disk.
- `graphd` keeps a per-root graph available for local query workflows.
- `cgraph-client` sends `query`, `path`, `explain`, `update`, `status`, and
  `shutdown` operations to the daemon.
- `cgraph-mcp` exposes graph operations through JSON-RPC/MCP-style requests.
- Host integrations own model choice and semantic enrichment work; CGraph owns
  deterministic extraction, fragment validation, cache state, and local graph
  mutation.

## Status

CGraph is an early native implementation. The full command surface (CLI, daemon,
thin client, MCP server) is present and tested, but there is no packaged release
or `cmake --install` flow yet — you build it from source with CMake and vcpkg and
run the binaries from the build tree (or symlink them onto your `PATH`). See
[Install & Setup](#install--setup).

## Features

- Deterministic project scanning with root `.gitignore` support and built-in
  skips for generated or dependency directories such as `build`, `dist`,
  `node_modules`, `target`, `vendor`, and `cgraph-out`.
- Tree-sitter-backed extraction for C, C++, Groovy, Java, JavaScript, Kotlin,
  Python, Ruby, Scala, TypeScript, and TSX.
- Regex or structured extraction for Apex, Delphi form/source files, MSBuild/XML
  project files, and MCP config files.
- Graph post-processing for import resolution, raw call resolution, relation
  resolution, semantic deduplication, community detection, and graph analysis.
- Disk exports for `graph.json`, `graph.html`, `graph.svg`, `obsidian.md`,
  `cypher.txt`, and `call-flow.html`.
- Daemon and thin-client workflows for local graph search, shortest paths, node
  explanation, status checks, updates, and shutdown.
- Semantic enrichment workflow based on chunk planning, host-written
  `chunk_NN.json` fragments, schema validation, and content-hash cache records.
- Smoke tests and optional libFuzzer targets.

## Repository Layout

```text
src/cli/          One-shot CLI entrypoint: cgraph
src/daemon/       Daemon entrypoint: graphd
src/client/       Thin client runtime and cgraph-client executable
src/mcp/          MCP request handling and cgraph-mcp executable
src/engine/       Detection, extraction, graph building, analysis, daemon ops
tests/smoke/      CTest smoke coverage for engine, daemon, MCP, and integration paths
tests/fuzz/       Optional libFuzzer targets
integrations/     Host hook and always-on integration scripts
docs/             Host integration contract and benchmark notes
vendor/           Vendored tree-sitter core and grammars
```

## Install & Setup

### Prerequisites

- CMake 3.25 or newer
- Ninja
- A C++20 compiler (recent Clang or GCC; Apple Clang from Xcode Command Line Tools works)
- Git
- vcpkg (a local copy is fine — see step 2). Dependencies `curl`, `igraph`,
  `nlohmann-json`, and `utf8proc` are declared in `vcpkg.json` and built
  automatically on first configure. `tree-sitter` is vendored under
  `vendor/tree-sitter`.

### 1. Clone

```sh
git clone https://github.com/taylor009/CGraph.git
cd CGraph
```

### 2. Point CMake at vcpkg

The presets read the toolchain from `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`,
and `VCPKG_ROOT` is **not** set for you. Use an existing vcpkg checkout:

```sh
export VCPKG_ROOT="/path/to/your/vcpkg"
```

…or bootstrap a local copy inside the repo (this is what the presets expect by
default; `.vcpkg/` is gitignored):

```sh
git clone https://github.com/microsoft/vcpkg .vcpkg
./.vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$PWD/.vcpkg"
```

Add the `export` line to your shell profile (or re-run it each session) — every
`cmake`/`ctest` invocation below needs it.

### 3. Configure and build

```sh
cmake --preset default
cmake --build --preset default
```

The first build also compiles the vcpkg dependencies (igraph, curl, …), so it
takes several minutes; later builds are incremental. Output lands in
`build/default`, with binaries at:

```text
build/default/src/cli/cgraph
build/default/src/daemon/graphd
build/default/src/client/cgraph-client
build/default/src/mcp/cgraph-mcp
```

### 4. Verify

```sh
ctest --preset default                                  # run the smoke suite
build/default/src/cli/cgraph --root . --out cgraph-out  # build a graph of this repo
```

Open `cgraph-out/graph.html` in a browser to confirm the interactive view.

### 5. (Optional) Put the binaries on your PATH

There is no install target, but symlinking the four binaries into a directory
on your `PATH` lets you call them by name (`cgraph`, `graphd`, `cgraph-client`,
`cgraph-mcp`) instead of by build path:

```sh
mkdir -p ~/.local/bin
for b in cli/cgraph daemon/graphd client/cgraph-client mcp/cgraph-mcp; do
  ln -sf "$PWD/build/default/src/$b" ~/.local/bin/
done
# ensure ~/.local/bin is on PATH
```

> Note: MCP client configs (below) should still use absolute paths to the
> binaries, since a client may not inherit your interactive shell's `PATH`.

### Development builds

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

Build and smoke-test fuzzer targets:

```sh
cmake --preset fuzzers
cmake --build --preset fuzzers
ctest --preset fuzzers
```

The fuzzer preset requires a Clang toolchain with the libFuzzer runtime. Some
Apple Command Line Tools installations do not provide that runtime; use an
upstream LLVM/Clang toolchain when the fuzzer configure step fails for that
reason.

## Quick Start

Build a graph for the current repository:

```sh
build/default/src/cli/cgraph --root . --out cgraph-out
```

The command writes:

```text
cgraph-out/graph.json
cgraph-out/graph.html
cgraph-out/graph.svg
cgraph-out/obsidian.md
cgraph-out/cypher.txt
cgraph-out/call-flow.html
```

Open `cgraph-out/graph.html` in a browser for an interactive local graph
view, or consume `cgraph-out/graph.json` from other tools.

## Use with Coding Agents

`cgraph-mcp` is a standard [Model Context Protocol](https://modelcontextprotocol.io)
server over stdio (newline-delimited JSON-RPC, protocol `2024-11-05`). Register it
once and your agent can navigate the codebase through fast graph queries instead
of blind grep/read. It exposes eight tools:

| Tool | Purpose |
| --- | --- |
| `graph_query` | Search nodes by text; ranked by centrality |
| `graph_explain` | A node's neighborhood (callers, callees, imports) |
| `graph_impact` | Transitive blast radius of changing a node |
| `graph_path` | Shortest path between two nodes |
| `graph_context` | Token-budgeted source bundle for a node/query |
| `graph_update` | Force a full rescan (the daemon also watches the tree and updates automatically) |
| `graph_status` | Daemon, graph, and enrichment status |
| `graph_shutdown` | Stop the daemon |

`graph_context` has two gather modes. The default (`gather: "fixed"`) packs the whole k-hop
neighborhood. With a task query in hand, `gather: "adaptive"` keeps the full 2-hop core but expands
the third hop only along query-relevant nodes — on the retrieval eval it lifted grade-2 recall
**+0.057** for **+13%** candidate tokens, versus the **+96%** a full 3-hop gather costs. It needs a
`query`/`q` (the relevance gate is a no-op without one):

```jsonc
// "load the code I need to change payment validation, ~5k tokens"
graph_context { "id": "PaymentValidator", "q": "payment validation", "gather": "adaptive", "budget": 5000 }
// response carries gather:"adaptive", packing:"knapsack", and a reach summary:
//   "reach": { "candidates": 36, "expanded_past_core": 9, "gated_at_core": 14 }
// expanded_past_core == 0 means the gate found nothing relevant past 2 hops (it collapsed to the core).
```

The server resolves the project root from `--root`, then `CLAUDE_PROJECT_DIR`,
then the working directory. It finds the `graphd` daemon binary on its own: an
explicit `--daemon <path-to-graphd>` wins, then `CGRAPH_DAEMON_PATH`, then a
`graphd` installed next to the `cgraph-mcp` binary (the build tree layout also
works). With the binaries side by side you can omit `--daemon` entirely. The
first call triggers a one-time graph build (seconds); while it runs, query
results carry `"graph_state": "building"` so an empty result is never mistaken
for "no match". Subsequent queries are warm (~10ms).

In the examples below, replace `/abs/path/to/CGraph` with this repo's absolute
path.

### Claude Code

Claude Code sets `CLAUDE_PROJECT_DIR` per session, so a single registration works
across every project — no per-project root needed. Add it with the CLI:

```sh
claude mcp add --scope user --transport stdio cgraph \
  -- /abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp \
     --daemon /abs/path/to/CGraph/build/default/src/daemon/graphd
```

Or, to share it with a repo's collaborators, commit a project-scoped `.mcp.json`
at the repo root:

```json
{
  "mcpServers": {
    "cgraph": {
      "command": "/abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp",
      "args": ["--daemon", "/abs/path/to/CGraph/build/default/src/daemon/graphd"]
    }
  }
}
```

Verify with `/mcp` inside Claude Code; you should see `cgraph` connected with its
tools. This repo also ships two host skills under `integrations/skills/`:
`cgraph` (reach for the graph tools first on codebase-structure questions) and
`cgraph-enrich` (the semantic-enrichment loop that drains
`enrichment_pending`). Install both into your host's skill directories with:

```sh
build/default/src/cli/cgraph skills install     # symlinks into ~/.claude/skills + ~/.agents/skills
build/default/src/cli/cgraph skills status      # shows each link and its target
```

To keep enrichment from accumulating unattended, install the scheduled drainer
(status-gated: it costs nothing when `enrichment_pending` is 0):

```sh
build/default/src/cli/cgraph drain install      # daily launchd sweep over tracked repos
```

### Codex CLI

Codex does not set `CLAUDE_PROJECT_DIR`, so the server falls back to the working
directory Codex launches it from (normally the project you opened). Add it with:

```sh
codex mcp add cgraph \
  -- /abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp \
     --daemon /abs/path/to/CGraph/build/default/src/daemon/graphd
```

…or edit `~/.codex/config.toml` directly:

```toml
[mcp_servers.cgraph]
command = "/abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp"
args = ["--daemon", "/abs/path/to/CGraph/build/default/src/daemon/graphd"]
```

To pin a specific project regardless of working directory (e.g. a project-scoped
`.codex/config.toml`), add the root to `args`:

```toml
[mcp_servers.cgraph]
command = "/abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp"
args = [
  "--root", "/abs/path/to/your/project",
  "--daemon", "/abs/path/to/CGraph/build/default/src/daemon/graphd",
]
```

Restart Codex after editing the config and run `/mcp` in the TUI to confirm.

### Cursor, Windsurf, and other MCP clients

Any MCP client that launches a stdio command works. Add a server entry with the
command and args (most use a `mcpServers` JSON block like Claude Code's
`.mcp.json` above):

```json
{
  "mcpServers": {
    "cgraph": {
      "command": "/abs/path/to/CGraph/build/default/src/mcp/cgraph-mcp",
      "args": [
        "--root", "/abs/path/to/your/project",
        "--daemon", "/abs/path/to/CGraph/build/default/src/daemon/graphd"
      ]
    }
  }
}
```

Set `--root` explicitly for clients that do not set `CLAUDE_PROJECT_DIR` or a
project working directory. You can also smoke-test the server by hand:

```sh
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  | build/default/src/mcp/cgraph-mcp --root . \
      --daemon build/default/src/daemon/graphd
```

## CLI

```sh
cgraph [--root PATH] [--out PATH]
cgraph enrich-plan [--root PATH] [--out PATH] [--drop DIR]
cgraph enrich-ingest [--root PATH] [--out PATH] [--drop DIR]
```

Defaults:

- `--root .`
- `--out cgraph-out`
- `--drop` defaults to CGraph's semantic drop directory under the output path

Examples:

```sh
# Build deterministic exports.
build/default/src/cli/cgraph --root /path/to/project --out /tmp/cgraph-out

# Create a semantic chunk plan for host enrichment.
build/default/src/cli/cgraph enrich-plan --root /path/to/project --out /tmp/cgraph-out

# Ingest host-written chunk_NN.json fragments and re-export the graph.
build/default/src/cli/cgraph enrich-ingest --root /path/to/project --out /tmp/cgraph-out
```

## Daemon and Thin Client

Start the graph daemon:

```sh
build/default/src/daemon/graphd --root /path/to/project
```

The daemon watches the project tree while it runs: source edits are folded into
the graph incrementally within a couple of seconds (a large batch, e.g. a branch
switch, collapses into one full rescan), and incremental state is re-persisted to
`cgraph-out/` in the background and on shutdown. `--no-watch` disables this and
leaves updates to explicit `update` ops.

Optional daemon flags:

```sh
graphd --root PATH --idle-timeout SECONDS --no-watch
graphd --benchmark-query --graph PATH --query TEXT
graphd --version
```

Use the thin client:

```sh
build/default/src/client/cgraph-client --root /path/to/project status
build/default/src/client/cgraph-client --root /path/to/project query '{"q":"Parser"}'
build/default/src/client/cgraph-client --root /path/to/project explain '{"id":"Parser"}'
build/default/src/client/cgraph-client --root /path/to/project path '{"source":"A","target":"B"}'
build/default/src/client/cgraph-client --root /path/to/project update '{"path":"."}'
build/default/src/client/cgraph-client --root /path/to/project shutdown
```

If you need a specific daemon binary, pass it explicitly:

```sh
build/default/src/client/cgraph-client \
  --root /path/to/project \
  --daemon build/default/src/daemon/graphd \
  status
```

Client responses are JSON. `status` includes daemon process metadata, graph node
and edge counts, cache hit rate, and semantic enrichment state.

## MCP Server

Build target:

```text
build/default/src/mcp/cgraph-mcp
```

`cgraph-mcp` speaks MCP over stdio: newline-delimited JSON-RPC 2.0 implementing
`initialize`, `tools/list`, `tools/call`, and `notifications/initialized`
(protocol version `2024-11-05`). Tool calls route through the same daemon
operation handler used by the thin client; invalid JSON receives a JSON-RPC
parse error response. For registering it with Claude Code, Codex, and other MCP
clients — and the list of exposed tools — see
[Use with Coding Agents](#use-with-coding-agents).

## Host Integrations

CGraph keeps provider and model concerns outside the native binary. Host
integrations should use `cgraph-client` for graph operations and dispatch any
semantic work through their own agent or model workflow.

The reference hook accepts the deterministic daemon operations:

```sh
integrations/hooks/cgraph-hook.sh status
integrations/hooks/cgraph-hook.sh query '{"q":"GraphSnapshot"}'
```

Useful environment variables:

- `CGRAPH_CLIENT`: client executable, default `cgraph-client`
- `CGRAPH_PROJECT_ROOT`: project root, default current directory
- `CGRAPH_DAEMON`: optional daemon executable path
- `CGRAPH_INTERVAL_SECONDS`: always-on status interval, default `30`
- `CGRAPH_REFRESH_ON_START`: set to `0` to skip the initial update in the
  always-on loop
- `CGRAPH_ONCE`: set to `1` to run one status check and exit

Run the always-on reference loop:

```sh
CGRAPH_CLIENT=build/default/src/client/cgraph-client \
CGRAPH_PROJECT_ROOT=/path/to/project \
integrations/always-on/cgraph-always-on.sh
```

See `docs/host-skill-contract.md` for the full host contract.

## Semantic Enrichment

Semantic enrichment is a host-driven workflow:

1. CGraph emits a chunk plan for uncached or stale semantic inputs.
2. The host processes each chunk with its own model or agent workflow.
3. The host writes exactly one `chunk_NN.json` fragment per completed chunk into
   the semantic drop directory.
4. CGraph validates each fragment before graph mutation.
5. Valid fragments update the graph and semantic cache; malformed fragments are
   rejected without changing the graph snapshot.

Fragments use this node-link shape:

```json
{
  "nodes": [
    {
      "id": "doc:architecture",
      "label": "Architecture",
      "kind": "document"
    }
  ],
  "edges": [
    {
      "source": "doc:architecture",
      "target": "component:engine",
      "relation": "describes"
    }
  ],
  "hyperedges": []
}
```

Required fields:

- Node: `id`, `label`
- Edge: `source`, `target`, `relation`
- Hyperedge: `id`, `nodes`, `relation`

Optional fields include `source_file`, `source_location`, `type` or `kind`,
`confidence`, `confidence_score`, `properties`, and `warnings`.

## Output Formats

- `graph.json`: directed node-link JSON with graph metadata, nodes, and links
- `graph.html`: browser-readable interactive graph view
- `graph.svg`: static graph visualization
- `obsidian.md`: markdown export for Obsidian-style navigation
- `cypher.txt`: Neo4j Cypher statements
- `call-flow.html`: browser-readable call-flow view

## Development Notes

- Keep extraction behavior deterministic in the engine. Provider-specific logic
  belongs in host integrations.
- Add smoke coverage under `tests/smoke/` for engine behavior and integration
  surfaces.
- Add fuzzer coverage under `tests/fuzz/` for parser or extractor hardening.
- Prefer extending the central language configuration and extractor pipeline
  over adding ad-hoc extraction logic in consumers.
