## ADDED Requirements

### Requirement: File extraction
The system SHALL extract a fragment, raw calls, and raw relations from each detected project file
using the language-appropriate extractor. Extraction across files MAY execute concurrently, and
the resulting sequence of per-file extraction results SHALL be identical to extracting the same
files serially in detection order.

#### Scenario: Parallel extraction matches serial output
- **WHEN** a set of detected files is extracted concurrently
- **THEN** the per-file results are produced in detection order and each result is identical to
  extracting that file on its own, so the merged graph is byte-identical to a serial build

#### Scenario: Unextractable file is isolated
- **WHEN** one file fails to extract (missing, too large, or no registered extractor)
- **THEN** its result carries the warning and an empty fragment, and the other files in the batch
  are unaffected
