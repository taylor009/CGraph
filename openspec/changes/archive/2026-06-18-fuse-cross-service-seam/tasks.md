## 1. Promote the node-link loader

- [x] 1.1 Moved `parse_node_link_graph` (public, declared in `export_json.hpp` as the inverse of
      `to_node_link_json`) + its `parse_build_state`/`parse_node_link_node`/`parse_node_link_edge`
      helpers (file-local) into `export_json.cpp`.
- [x] 1.2 `daemon_lifecycle.cpp` now calls the public loader (include already present);
      behavior-preserving.
- [x] 1.3 `cgraph_daemon_ops_test` stays green — the daemon fast-load path is unchanged.

## 2. Fuse (engine)

- [x] 2.1 `seam_test.cpp`: `fuse_seam(frag, {{"backend", graph}})` — backend nodes tagged
      `community="backend"`; seam service/endpoint/schema nodes tagged with their community; no
      `code-ref` node survives; `CONSUMED_AT` targets the real `backend::scoreModel`; the backend's
      own CALLS edge survives; edges deduped.
- [x] 2.2 `seam_test.cpp`: `fuse_seam(frag, {})` (backend graph omitted) → `ok=false`, error set,
      empty graph.
- [x] 2.3 Implemented `fuse_seam` in `seam.cpp`/`seam.hpp` (merge + community tag + drop shadows +
      dedup edges + fail-loud endpoint check), reusing the promoted loader.

## 3. CLI subcommand + render

- [x] 3.1 `cli/main.cpp`: added `seam fuse --seam SEAM --graph NAME=graph.json[…] --out DIR`
      (repeatable `--graph`); writes `--out/graph.json` (`to_node_link_json`) + `--out/graph.html`
      (`export_graph_html`); usage line + `seam <gen|fuse>` dispatch added.
- [x] 3.2 Live smoke: `seam gen` → fragment; `seam fuse` → `graph.html` (35 KB) + `graph.json`,
      communities `{backend: 3, ml-api: 3}`, 0 code-ref nodes; omitting the backend graph fails loud
      ("2 edge endpoint(s) missing … CONSUMED_AT: target=backend::scoreModel").

## 4. Verify

- [x] 4.1 `ctest --preset default -R cgraph_seam_test` → passed.
- [x] 4.2 Full suite `ctest --preset default` → 60/60; parity goldens unchanged (loader move is
      behavior-preserving; fuse touches no parity surface).
