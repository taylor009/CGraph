# semantic-fragment-ingest — delta for host-enrichment-driver

## ADDED Requirements

### Requirement: Host skills are installable from the binary
The CLI SHALL provide `cgraph skills <install|status|uninstall>`: `install` symlinks the
canonical `integrations/skills/cgraph` and `integrations/skills/cgraph-enrich` directories
into the host skill paths (`~/.claude/skills/` and the shared agents skills path) without
copying content; `status` reports each expected link's presence and target; `uninstall`
removes only links that point into this repository. Install SHALL be idempotent.

#### Scenario: Install then discover in another repo
- **WHEN** `cgraph skills install` runs and a new host session starts in a supervisor-tracked repository
- **THEN** `cgraph skills status` shows both links resolving into this repository and the session lists the `cgraph` and `cgraph-enrich` skills

#### Scenario: Re-install is a no-op
- **WHEN** `cgraph skills install` runs while correct links already exist
- **THEN** the links are unchanged and the command succeeds

### Requirement: Pending enrichment is drained autonomously and bounded
A scheduled host-side drainer SHALL exist that, per tracked repository, reads
`status.enrichment_pending` and exits without any model dispatch when it is zero; when
positive it SHALL run the enrich loop headlessly with a per-run chunk cap, leaving
remaining chunks for subsequent runs. Model selection and spend SHALL remain host-owned.

#### Scenario: Pending repo drains to zero
- **WHEN** the drainer (or a manual `/cgraph-enrich` invocation) runs on a repository whose daemon reports `enrichment_pending > 0`
- **THEN** across one or more runs `status` reaches `enrichment_pending: 0` and `enrichment_failed: 0`, and the semantic block reports non-zero `doc_nodes` and `doc_code_edges`

#### Scenario: Steady state costs nothing
- **WHEN** the drainer fires on a repository with `enrichment_pending: 0`
- **THEN** it exits after the status check with no host model invocation

#### Scenario: Stale doc re-enters the next run
- **WHEN** a previously enriched document changes on disk
- **THEN** the next plan lists it as stale and only that input is re-enriched

#### Scenario: Re-run is idempotent
- **WHEN** the drainer runs again with no input changes
- **THEN** the plan reports all inputs as cache hits and no host model work is performed
