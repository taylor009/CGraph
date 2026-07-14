#!/bin/sh
# Scheduled enrichment drainer (host side). Installed as the
# com.cgraph.enrich-drain LaunchAgent by `cgraph drain install`.
#
# For every supervisor-tracked repo: read enrichment_pending from the daemon
# (near-free status op) and exit without any model work when it is zero;
# otherwise run the cgraph-enrich skill loop headlessly, capped to CHUNK_CAP
# chunks this run — the next interval continues where this one stopped.
#
# All model selection, spend, and agent dispatch live HERE, not in the binary
# (docs/host-skill-contract.md). Overridables:
#   CGRAPH_HOST_CLI    host coding-agent CLI (default: claude)
#   CGRAPH_DRAIN_MODEL model for fragment authoring (default: sonnet —
#                      mechanical extraction, per reduce-opus-cost-routing)
set -eu

CGRAPH_BIN="${1:?usage: cgraph-enrich-drain.sh CGRAPH_BIN CLIENT_BIN [CHUNK_CAP]}"
CLIENT_BIN="${2:?usage: cgraph-enrich-drain.sh CGRAPH_BIN CLIENT_BIN [CHUNK_CAP]}"
CHUNK_CAP="${3:-10}"
HOST_CLI="${CGRAPH_HOST_CLI:-claude}"
MODEL="${CGRAPH_DRAIN_MODEL:-sonnet}"

if ! command -v "$HOST_CLI" >/dev/null 2>&1; then
  # launchd PATH is minimal; resolve through the user's login shell, which has
  # the real PATH (shims, homebrew, ~/.local/bin).
  resolved=$(/bin/zsh -lc "command -v $HOST_CLI" 2>/dev/null | tail -n 1 || true)
  if [ -n "$resolved" ] && [ -x "$resolved" ]; then
    HOST_CLI="$resolved"
  else
    echo "cgraph-enrich-drain: host CLI '$HOST_CLI' not found; set CGRAPH_HOST_CLI" >&2
    exit 1
  fi
fi

"$CGRAPH_BIN" daemon status | sed -n 's/^  \[[a-z]*\][[:space:]]*//p' | while IFS= read -r repo; do
  [ -d "$repo" ] || continue
  # </dev/null everywhere in the loop body: anything that inherits stdin would
  # otherwise eat the rest of the repo list coming through the while-read pipe.
  pending=$("$CLIENT_BIN" --root "$repo" status 2>/dev/null </dev/null \
    | sed -n 's/.*"enrichment_pending": *\([0-9][0-9]*\).*/\1/p' | head -n 1)
  [ -n "$pending" ] || continue
  [ "$pending" -gt 0 ] || continue
  echo "cgraph-enrich-drain: $repo has $pending pending input(s); draining (cap $CHUNK_CAP chunks)"
  # Prompt goes through stdin: --allowedTools is variadic and would swallow a
  # positional prompt, and an inherited stdin would eat the repo-list pipe.
  prompt="Run the cgraph-enrich skill loop for this repo: '${CGRAPH_BIN} enrich-plan --root . --out cgraph-out', then author and atomically drop the fragment for each chunk in cgraph-out/semantic-drop/plan.json per the cgraph-enrich skill. Use the fragment filenames the CURRENT plan gives; after dropping, merge with '${CGRAPH_BIN} enrich-ingest --root . --out cgraph-out' before re-planning. Author at most ${CHUNK_CAP} chunks this run, highest-value docs first, then stop; a later run continues. Never edit source files; only write chunk fragments named by the plan."
  (cd "$repo" && printf '%s' "$prompt" | "$HOST_CLI" -p \
      --model "$MODEL" \
      --permission-mode acceptEdits \
      --allowedTools "Bash(${CGRAPH_BIN}:*),Bash(${CLIENT_BIN}:*),Bash(mv:*),Read,Write,Glob,Grep") \
    || echo "cgraph-enrich-drain: $repo drain run failed" >&2
done
