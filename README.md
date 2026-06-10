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

CGraph is an early native implementation. The public command surface is present,
but the project does not currently include an installer or packaged release
flow. Build it from source with CMake and vcpkg.

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

## Requirements

- CMake 3.25 or newer
- Ninja
- A C++20 compiler
- vcpkg, with `VCPKG_ROOT` set
- vcpkg dependencies from `vcpkg.json`: `curl`, `igraph`, `nlohmann-json`, and
  `utf8proc`

The default CMake preset uses:

```text
$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

## Build

Configure and build the default debug preset:

```sh
cmake --preset default
cmake --build --preset default
```

The default preset writes build output to:

```text
build/default
```

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers
```

Build fuzzer targets:

```sh
cmake --preset fuzzers
cmake --build --preset fuzzers
```

The fuzzer preset requires a Clang toolchain with the libFuzzer runtime. Some
Apple Command Line Tools installations do not provide that runtime; use an
upstream LLVM/Clang toolchain when the fuzzer configure step fails for that
reason.

## Test

Run the default smoke suite:

```sh
ctest --preset default
```

Run the sanitizer suite:

```sh
ctest --preset sanitizers
```

Run fuzzer smoke tests after configuring the fuzzer preset:

```sh
ctest --preset fuzzers
```

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

Optional daemon flags:

```sh
graphd --root PATH --idle-timeout SECONDS
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

`cgraph-mcp` reads JSON requests from standard input, routes them through the
same daemon operation handler used by the thin client, and writes JSON responses
to standard output. Invalid JSON receives a JSON-RPC parse error response.

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
