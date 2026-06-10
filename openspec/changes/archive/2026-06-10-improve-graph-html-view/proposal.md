## Why

The interactive `graph.html` export (`export_graph_html`, `src/engine/export_json.cpp`) renders a live force simulation, but on a real project (688 nodes / 609 edges) it reads as a static rectangle of overlapping confetti and traps the user in a dead-end focus state:

- **The layout carries no structure.** Repulsion is cut off at a local radius (`k*2.6`), gravity pulls every node to center, and community assignments — which the pipeline already computes — are used only for node color, never for position. The equilibrium is "fill the frame uniformly," so computed clusters are invisible and the graph looks frozen.
- **Every hub node is always labelled** (radius ≥ 9), producing hundreds of overlapping labels — the dominant visual noise.
- **There is no way out of a selection.** Clicking a node dims everything else and `selectedId` is never cleared; clicking empty canvas starts a pan, there is no Escape handler, no clear/reset control, and no fit-to-screen. The only way back to the full graph is reloading the page.

These are interaction and layout defects in a generated artifact, not a parity concern — `graph.json`, fragment shape, and ID normalization are untouched.

## What Changes

- **Community-aware layout**: position same-community nodes together (centroid attraction plus community-seeded initial placement) and remove/raise the local repulsion cutoff so the graph spreads into legible regions instead of a uniform blob.
- **Label thinning**: cap always-on labels to a global top-N by degree and reveal the rest progressively on hover, selection, search, and zoom-in — so the canvas is not a wall of text.
- **Selection exit + view controls**: clicking empty canvas and pressing `Escape` clear the current selection; add "Reset view" and "Fit to screen" controls so the user can always return to the whole graph and recenter without reloading. The layout relaxes in open world space (no viewport wall clamp) and auto-fits on load.
- **Light/dark theme**: a theme toggle (defaulting to the OS color-scheme preference) switches the whole view — including the graph canvas, node strokes, edges, and labels — between light and dark.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `deterministic-graph-pipeline`: The interactive HTML export gains structure-revealing layout, bounded labelling, and reversible selection/navigation. The `graph.json` node-link contract, fragment shape, and ID normalization are unchanged.

## Non-Goals

- No change to `graph.json`, the fragment schema, ID normalization, SVG/Obsidian/Cypher exports, or any parity surface.
- No external JS/CSS dependencies or build step — the HTML stays a single self-contained file with inline canvas rendering.
- No server-side or daemon changes; this is purely the generated static artifact.
- No full drill-down/breadcrumb navigation stack in this change (kept as a possible follow-up); scope is highlight + reversible exit + fit, not nested map-style descent.

## Impact

- Affected code: `src/engine/export_json.cpp` (`export_graph_html` only).
- Affected tests: `tests/smoke/export_json_test.cpp` (assert the generated HTML contains the new interaction hooks and layout behavior); optional browser-driven verification of the live view.
- No CMake source-list change (no new files); no new dependencies.
