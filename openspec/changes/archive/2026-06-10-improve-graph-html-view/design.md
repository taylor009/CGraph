## Context

`export_graph_html` (`src/engine/export_json.cpp:104-789`) emits a single self-contained HTML file: inline CSS, the graph serialized as `graphData`, and a vanilla-JS `<canvas>` force simulation. There is no build step and no external dependency. The deterministic pipeline already computes community assignments and degree/centrality; the view only consumes community for color. This change is confined to the generated JS/CSS — no C++ data-structure or export-contract changes.

The three defects diagnosed from the live 688-node export:

1. **Structureless layout.** `simulationTick` (`:443-507`) applies repulsion only within `cutoff = k*2.6` (local), constant center gravity (0.045), and edge springs. With a uniform initial seed (`layout`, `:424-441`) the stable state is a uniform frame-filling cloud. Community is never a positional force.
2. **Label wall.** `draw` (`:647`) labels every node with radius ≥ 9; on a real graph hundreds qualify.
3. **Irreversible selection.** `selectNode` (`:694-711`) sets `selectedId`; nothing clears it. Empty-canvas pointerdown starts a pan (`:731`). No key handler, no controls.

## Goals / Non-Goals

**Goals**
- Make computed community structure visible as spatial regions.
- Keep labels legible at every zoom level.
- Guarantee the user can always return to the unfocused full-graph view without reloading.
- Preserve determinism: same graph → same layout (no `Math.random`; seeded jitter only, as today).

**Non-Goals**
- No parity-surface changes, no new exports, no external libraries, no daemon changes.
- No nested drill-down/breadcrumb navigation (possible follow-up).

## Decisions

### Layout: community centroid attraction + community-seeded placement
Add a per-tick force pulling each node toward the running centroid of its community, and seed initial positions by community (each community gets an anchor placed around a ring; members start near their anchor with seeded jitter). Raise or remove the repulsion cutoff so non-adjacent communities push apart into distinct regions rather than relaxing locally inside one blob. Keep center gravity weak (or scale it down) so communities separate instead of all collapsing inward.

- *Why centroid attraction over a fixed community grid:* it composes with the existing FR forces and stays deterministic, while letting edge structure still shape intra-community layout. A hard grid would override the simulation and lose cross-community edge cues.
- *Performance:* the graph is O(n²) per tick today (688² with a cutoff `continue`). Removing the cutoff makes every pair compute force. At ~700 nodes this is acceptable (~480k ops × 2 ticks/frame). For larger graphs a spatial grid / Barnes–Hut is the future lever; **out of scope here**, but the layout code should keep the per-pair force isolated so it can be swapped. If removing the cutoff regresses settle time noticeably, keep a *large* cutoff (e.g. `k*8`) rather than the current tight one.

### Labels: global top-N budget + progressive reveal
Replace the `radius ≥ 9` rule with: always label the top-N nodes by degree (N a small constant, e.g. 24), and additionally label any node that is hovered, selected, in the active highlight set, matched by search, or when `transform.scale` exceeds a threshold (zoomed in). This keeps the overview clean and rewards zooming with more detail.

### No wall clamp + auto-fit on load
The original `simulationTick` clamped every node to `[24, sim.width-24] x [24, sim.height-24]`. That was acceptable for the old uniform-cloud layout but, combined with the wider repulsion and community forces above, it pins outward-pushed nodes flat against the viewport edges — producing a dense rectangular border ("the box"). Remove the clamp entirely: the layout relaxes in open world space, kept bounded by gravity + community cohesion. To compensate for the now-larger-than-viewport layout, `runSimulation` auto-fits the whole graph once the initial simulation settles (`autoFitPending`), and cancels that auto-fit the instant the user pans, zooms, or selects, so a view in use is never yanked. Pan, zoom, and Fit to screen are the navigation model, not a hard boundary.

### Selection exit + view controls
- Empty-canvas pointerdown (no node hit) clears `selectedId` (and `hoverId`) before starting the pan.
- `Escape` key clears selection and search highlight and redraws.
- Add two controls in the stage overlay: **Reset view** (clear selection + reset `transform` to identity) and **Fit to screen** (compute the node bounding box and set `transform` to center+scale it into the viewport). Fit-to-screen doubles as the recenter affordance the current view lacks entirely.

## Test strategy

The generated artifact is HTML+JS with no runtime in the C++ test process, so coverage is two-layered:

1. **Deterministic string-contract smoke tests** (`tests/smoke/export_json_test.cpp`, the directly-testable layer, written first / red-green): assert the generated HTML contains the new behavior hooks so regressions in the generator are caught in CI without a browser. Concretely, assert the output of `export_graph_html` contains:
   - a community-centroid / community-seed force (e.g. a `communityCentroid`/`community` positional term, distinct from the color-only use),
   - an `Escape`/`keydown` selection-clear handler and an empty-canvas deselect path,
   - "Reset view" and "Fit to screen" control markup/handlers,
   - a bounded-label construct (a top-N degree budget rather than a bare `r >= 9` gate).
   These are observable substrings of a deterministic generator, so they are concrete and stable.

2. **Behavior that cannot be unit-tested in-process** — the actual visual layout separation, label legibility at zoom, and that Escape/empty-click/fit truly restore the full view — is validated by **browser-driven verification** of the regenerated `cgraph-out/graph.html` (Playwright/`browser` skill): load the file, click a node, assert non-highlighted node opacity drops, press Escape / click empty, assert the highlight clears; trigger Fit and assert the transform recenters. This is recorded as an explicit verification step in tasks, since it is not part of `ctest`.

Determinism is preserved by continuing to use `seededUnit` (no `Math.random`); a smoke assertion guards against accidental `Math.random` introduction.

## Risks / Trade-offs

- **Settle time / CPU** if the cutoff is fully removed on large graphs → mitigation: large-but-finite cutoff fallback; keep force kernel swappable for future Barnes–Hut.
- **Community seeding could overlap** for many small communities → mitigation: ring placement scaled by community count; intra-community jitter prevents exact overlap.
- **Top-N labels might hide a node the user expects** → mitigation: hover/search/zoom always reveal, so nothing is permanently invisible.

## Migration

None. The change only alters the generated `graph.html`; regenerating the export (one-shot CLI or daemon export) produces the improved view. No data migration, no config, no consumer changes.
