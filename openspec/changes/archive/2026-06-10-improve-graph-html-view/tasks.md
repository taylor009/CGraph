## 1. Selection exit and view controls

- [x] 1.1 Add/extend `export_json_test.cpp` assertions (red first): generated HTML contains an `Escape`/`keydown` clear-selection handler, an empty-canvas deselect path, and "Reset view" + "Fit to screen" controls
- [x] 1.2 Implement empty-canvas pointerdown clearing `selectedId`/`hoverId` before starting the pan
- [x] 1.3 Implement `Escape` keydown handler that clears selection + search highlight and redraws
- [x] 1.4 Implement "Reset view" (clear selection, reset transform) and "Fit to screen" (bounding-box center+scale) controls in the stage overlay
- [x] 1.5 Run `ctest --preset default -R export_json_test`; confirm new assertions pass

## 2. Bounded labelling

- [x] 2.1 Add `export_json_test.cpp` assertion (red first): generated HTML uses a top-N degree label budget rather than a bare `r >= 9` gate
- [x] 2.2 Replace the `radius >= 9` label rule with: always-label top-N by degree, plus reveal on hover/selection/active-highlight/search/zoom-in
- [x] 2.3 Run `ctest --preset default -R export_json_test`; confirm assertions pass

## 3. Community-aware layout

- [x] 3.1 Add `export_json_test.cpp` assertions (red first): generated HTML contains a community-centroid/community-seed positional force and contains no `Math.random` (determinism guard)
- [x] 3.2 Implement community-seeded initial placement (per-community ring anchor + seeded intra-community jitter) in `layout`
- [x] 3.3 Implement per-tick community-centroid attraction in `simulationTick`; scale down center gravity so communities separate
- [x] 3.4 Raise/remove the repulsion cutoff (large-but-finite fallback) so distinct communities push into separate regions; keep the per-pair force kernel isolated for future Barnes-Hut
- [x] 3.5 Run `ctest --preset default -R export_json_test`; confirm assertions pass
- [x] 3.6 Remove the per-tick viewport wall clamp (it pinned outward-pushed nodes into a rectangular border) and auto-fit the graph once the initial layout settles, cancelled on first user pan/zoom/select. Re-verified: edge pile-up dropped from a dense border to 1-2 extreme nodes per side; layout spreads to a ~2001x1873 world; load auto-fits to scale ~0.355.

## 5. Light/dark theme

- [x] 5.1 Add `export_json_test.cpp` assertions (red first): generated HTML contains a theme toggle, a `data-theme` attribute hook, canvas color CSS variables (`--canvas-bg`), and a `prefers-color-scheme: dark` default
- [x] 5.2 Add dark theme CSS variables (`:root[data-theme="dark"]` + `prefers-color-scheme` default) and route the canvas background, node stroke, edge, and label colors through CSS variables
- [x] 5.3 Add a theme toggle control that flips `data-theme`, refreshes the canvas palette from CSS, and redraws; default to the OS preference on load
- [x] 5.4 Browser-verify: toggling switches `data-theme` to dark, canvas bg becomes `#0d1117`, and the canvas palette (text `#e6edf3`, node stroke `#0d1117`, edge `#48505a`) follows the theme. Confirmed.

## 4. End-to-end verification

- [x] 4.1 Regenerate the export: `export VCPKG_ROOT="$PWD/.vcpkg" && cmake --build build/default && build/default/src/cli/cgraph --root . --out cgraph-out`
- [x] 4.2 Browser-verify `cgraph-out/graph.html` (Playwright): communities render as separated colored regions; overview is not a label wall; clicking a node dims others; `Escape` and empty-click restore the full view; "Fit to screen" recenters. Observed on the live 656-node export: inter-community mean distance 635px vs intra-community 170px (separation ratio 3.74x); selecting a hub dims unrelated nodes (`nodeIsDim` true), `clearSelection` returns `selectedId` to "" and undims; `fitToScreen` recentered transform from (9999,9999,scale 0.3) to (42,30,scale 0.934); overview shows ~24 labels (the top-degree budget), not every node.
- [x] 4.3 Run the full smoke suite: `ctest --preset default --output-on-failure` — 53/53 passed
