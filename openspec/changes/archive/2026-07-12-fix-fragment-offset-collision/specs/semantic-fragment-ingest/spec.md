## ADDED Requirements

### Requirement: New fragment names never collide with existing drops
When the chunk plan assigns filenames for new fragments, each name SHALL be strictly greater than
the highest existing fragment index in the drop directory, so a new enrichment pass never reuses an
occupied fragment number and never overwrites a fragment authored by an earlier pass. This SHALL
hold even when existing fragment numbers are non-contiguous (gaps from a partial earlier pass). With
no existing fragments, numbering SHALL start at zero.

#### Scenario: Non-contiguous existing fragments do not cause a collision
- **WHEN** the drop directory contains `chunk_00.json` and `chunk_05.json` (a gap), and a plan emits
  at least one new chunk
- **THEN** the new fragment's filename has an index greater than 5 and matches no existing file

#### Scenario: Cold directory starts at zero
- **WHEN** the drop directory contains no fragments and a plan emits chunks
- **THEN** the first new fragment is `chunk_00.json`

#### Scenario: Accumulation across passes
- **WHEN** successive plans each emit new fragments into the same drop directory
- **THEN** each pass's fragment names are strictly past all prior fragments, so the directory
  accumulates and no earlier fragment is overwritten
