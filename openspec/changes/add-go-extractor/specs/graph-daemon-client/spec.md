## ADDED Requirements

### Requirement: Daemon status reports unextracted coverage
The `status` op payload SHALL include `unextracted`: a map of language name -> count of detected
files no registered extractor handles. Full rescans recompute the map from the detection walk;
the Tier-1 fast-load start computes it from the detection walk that path already performs;
incremental updates adjust it as unsupported-language files are added or removed (full rescans
self-heal any drift).

#### Scenario: Status surfaces the hole a warning used to hide
- **WHEN** a daemon serves a project containing `.cs` or `.blade.php` files
- **THEN** `status` reports them under `unextracted` (e.g. `{"csharp": 12, "php-blade": 3}`)
  instead of only a per-fragment warning inside the index

#### Scenario: Fast-load start still reports coverage
- **WHEN** a daemon starts via the Tier-1 persisted-graph path (no rescan)
- **THEN** `unextracted` reflects the current tree, computed from the manifest-check detection
  walk
