#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

namespace {

bool status_has_state(cgraph::DaemonState& state, cgraph::EnrichmentState enrichment_state, const char* expected) {
  state.enrichment_state = enrichment_state;
  const auto response = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  return response["ok"] == true && response["result"]["enrichment_state"] == expected;
}

}  // namespace

int main() {
  cgraph::DaemonState state;
  state.enrichment_pending = 2;
  state.enrichment_running = 1;
  state.enrichment_stale = 3;
  state.enrichment_failed = 4;

  if (!status_has_state(state, cgraph::EnrichmentState::Idle, "idle") ||
      !status_has_state(state, cgraph::EnrichmentState::Pending, "pending") ||
      !status_has_state(state, cgraph::EnrichmentState::Running, "running") ||
      !status_has_state(state, cgraph::EnrichmentState::Stale, "stale") ||
      !status_has_state(state, cgraph::EnrichmentState::Failed, "failed")) {
    return 1;
  }

  const auto response = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (response["result"]["enrichment_pending"] != 2 || response["result"]["enrichment_running"] != 1 ||
      response["result"]["enrichment_stale"] != 3 || response["result"]["enrichment_failed"] != 4) {
    return 1;
  }

  return 0;
}
