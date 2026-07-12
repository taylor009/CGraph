## Context

`plan_enrichment` derives new fragment filenames from `fragment_offset + chunk.index`, where
`fragment_offset = discover_semantic_fragment_drops(drop_dir).size()`
(`semantic_orchestration.cpp:126,140`). Counting assumes contiguous numbering; with a gap the count
is below the highest existing index, so a generated name reuses an occupied number and a host write
clobbers an earlier fragment.

## The fix

`discover_semantic_fragment_drops` returns `SemanticFragmentDrop` records, each with a
`chunk_index` already parsed from its filename (`semantic_drop.hpp:13-16`,
`semantic_drop.cpp:parse_chunk_index`). The offset becomes one past the maximum:

```cpp
const auto existing = discover_semantic_fragment_drops(drop_dir);
std::size_t fragment_offset = 0;
for (const auto& drop : existing) {
  fragment_offset = std::max(fragment_offset, drop.chunk_index + 1);
}
```

- Empty dir -> offset 0 -> first fragment `chunk_00` (unchanged).
- `chunk_00, chunk_01` -> offset 2 (same as count here, contiguous).
- `chunk_00, chunk_01, chunk_05, chunk_06` -> offset 7 (count gave 4 -> collision). New fragments
  start at `chunk_07`, strictly past every existing one.

This reads a value already computed; it adds no parsing and no I/O.

## Why max+1 and not "fill the gaps"

Reusing a gap number (e.g. `chunk_02` when 00,01,05,06 exist) would also avoid collision, but the
manifest's `index` field and the on-disk filename must agree for source attribution
(`ingest` parses `chunk_NN` and looks up `sources_by_chunk[NN]`). Monotonic `max+1` keeps each
pass's fragments in a fresh, non-overlapping range, which is simplest to reason about and matches
the "directory accumulates across passes" contract. Gap-filling buys nothing and complicates
attribution.

## Test strategy

- `semantic_orchestration_test` (red first): seed the drop dir with non-contiguous fragments
  (write valid `chunk_00.json` and `chunk_05.json`), then run `plan_enrichment` on a tree with at
  least one uncached doc and assert the manifest's first new `fragment` is `chunk_06.json` or
  higher (strictly past the max existing index 5) and that no generated name equals an existing
  file. Before the fix this would yield `chunk_02.json` (count 2), demonstrating the collision risk.
- Existing orchestration test (contiguous case) must still pass unchanged — proves the common path
  is unaffected.

### Hard to test directly

The actual data-loss symptom (a host overwriting a file) is a host-side action; the unit test
asserts the *contract* the host relies on — generated names never reuse an existing index — which
is the root cause.
