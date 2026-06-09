#!/usr/bin/env sh
set -eu

CGRAPH_CLIENT="${CGRAPH_CLIENT:-cgraph-client}"
CGRAPH_PROJECT_ROOT="${CGRAPH_PROJECT_ROOT:-$(pwd)}"
CGRAPH_INTERVAL_SECONDS="${CGRAPH_INTERVAL_SECONDS:-30}"
CGRAPH_REFRESH_ON_START="${CGRAPH_REFRESH_ON_START:-1}"
CGRAPH_ONCE="${CGRAPH_ONCE:-0}"

run_client() {
  if [ "${CGRAPH_DAEMON:-}" != "" ]; then
    "${CGRAPH_CLIENT}" --root "${CGRAPH_PROJECT_ROOT}" --daemon "${CGRAPH_DAEMON}" "$@"
  else
    "${CGRAPH_CLIENT}" --root "${CGRAPH_PROJECT_ROOT}" "$@"
  fi
}

if [ "${CGRAPH_ONCE}" = "1" ]; then
  run_client status
  exit $?
fi

if [ "${CGRAPH_REFRESH_ON_START}" = "1" ]; then
  run_client update '{"path":"."}'
fi

while :; do
  run_client status
  sleep "${CGRAPH_INTERVAL_SECONDS}"
done
