## ADDED Requirements

### Requirement: End-to-end retrieval quality gate
The system SHALL gate end-to-end retrieval quality with a committed smoke test that, for each
symbol-granularity row of the committed eval fixture, sends the row's free-text query to the
`context` op via `q` only (no focal id injection, engine defaults for gather, packing, and
depth), computes mean grade-2 recall at fixed token budgets, and fails when recall drops below
a committed measured baseline minus a fixed tolerance. Rows whose query fails to resolve a
focal SHALL count as zero recall rather than being skipped. The gate SHALL run as part of the
default CTest suite and SHALL NOT read mutable build output (it reads only the committed
fixture).

#### Scenario: Regression on the resolution path fails the suite
- **WHEN** a change causes free-text queries to stop resolving to their focal symbols (for
  example, the entity/lexical resolution path is broken) and the smoke suite runs
- **THEN** the end-to-end gate's measured recall drops below the committed baseline minus
  tolerance and the test fails

#### Scenario: Default-path behavior is what the gate measures
- **WHEN** the gate issues its `context` requests
- **THEN** the requests carry only the query text and budget, so the engine's actual default
  gather and packing configuration is what is measured

#### Scenario: Unresolved rows are counted, not skipped
- **WHEN** a fixture row's query resolves no focal node
- **THEN** the row contributes zero recall to the mean instead of being excluded
