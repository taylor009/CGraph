# deterministic-graph-pipeline Specification

## Purpose
TBD - created by archiving change improve-graph-html-view. Update Purpose after archive.
## Requirements
### Requirement: Interactive HTML view reveals community structure
The interactive `graph.html` export SHALL position nodes so that computed community assignments are visible as spatially distinct regions, rather than using community only for color while leaving the layout a uniform frame-filling cloud. The layout SHALL remain deterministic for a given graph (no use of `Math.random`).

#### Scenario: Communities render as separated regions
- **WHEN** the pipeline exports `graph.html` for a graph with multiple detected communities and the view settles
- **THEN** nodes of the same community are drawn closer to one another than to nodes of other communities, so distinct communities read as separate regions

#### Scenario: Layout is not clamped to a viewport box
- **WHEN** the layout settles for a graph larger than the viewport
- **THEN** nodes relax in open space without piling against fixed canvas edges, and the view auto-fits the whole graph so it is visible on load

#### Scenario: Layout is deterministic
- **WHEN** `graph.html` is generated twice for the same graph
- **THEN** the generated layout logic uses only seeded placement (no `Math.random`), so the same graph produces the same layout each load

### Requirement: Interactive HTML view bounds on-screen labels
The interactive `graph.html` export SHALL limit always-on node labels to a bounded set (the highest-degree nodes) and SHALL reveal additional labels progressively on hover, selection, active highlight, search match, and zoom-in, so an overview of a large graph is not an unreadable wall of overlapping text.

#### Scenario: Overview labels are bounded
- **WHEN** `graph.html` is exported for a graph with hundreds of nodes and viewed at the default zoom with no selection or search
- **THEN** only a bounded top-by-degree subset of nodes is labelled, not every node above a fixed radius

#### Scenario: Hidden labels are reachable
- **WHEN** the user hovers, selects, searches, or zooms into a node whose label is hidden in the overview
- **THEN** that node's label becomes visible, so no label is permanently inaccessible

### Requirement: Interactive HTML view selection is reversible
The interactive `graph.html` export SHALL let the user return to the unfocused full-graph view without reloading the page. Clicking empty canvas and pressing `Escape` SHALL clear the current selection and highlight, and the view SHALL provide controls to reset the view and to fit the whole graph to the viewport.

#### Scenario: Escape clears selection
- **WHEN** a node is selected (everything else dimmed) and the user presses `Escape`
- **THEN** the selection and highlight clear and the full graph is shown undimmed

#### Scenario: Clicking empty canvas clears selection
- **WHEN** a node is selected and the user clicks an empty area of the canvas
- **THEN** the selection clears before any pan begins, restoring the unfocused view

#### Scenario: Fit to screen recenters the graph
- **WHEN** the user has panned or zoomed away and activates the fit-to-screen control
- **THEN** the view recenters and scales so the whole graph's bounding box is visible within the viewport

### Requirement: Interactive HTML view supports light and dark themes
The interactive `graph.html` export SHALL render in both a light and a dark theme, defaulting to the operating system's color-scheme preference and providing a control to switch themes. The canvas (background, node strokes, edges, and labels) SHALL follow the active theme, not only the surrounding DOM chrome.

#### Scenario: Theme follows OS preference by default
- **WHEN** `graph.html` is opened with no explicit theme chosen and the OS prefers a dark color scheme
- **THEN** the view renders in dark theme, including the graph canvas

#### Scenario: User can switch themes
- **WHEN** the user activates the theme toggle
- **THEN** the page and the graph canvas switch between light and dark, with node, edge, and label colors updating to remain legible

