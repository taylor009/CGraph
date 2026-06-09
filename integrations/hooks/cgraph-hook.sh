#!/usr/bin/env sh
set -eu

CGRAPH_CLIENT="${CGRAPH_CLIENT:-cgraph-client}"
CGRAPH_PROJECT_ROOT="${CGRAPH_PROJECT_ROOT:-$(pwd)}"
CGRAPH_OPERATION="${1:-${CGRAPH_OPERATION:-status}}"

case "${CGRAPH_OPERATION}" in
  query|path|explain|update|status|shutdown)
    ;;
  *)
    echo "unsupported cgraph hook operation: ${CGRAPH_OPERATION}" >&2
    exit 2
    ;;
esac

if [ $# -gt 0 ]; then
  shift
fi

if [ "${CGRAPH_DAEMON:-}" != "" ]; then
  exec "${CGRAPH_CLIENT}" --root "${CGRAPH_PROJECT_ROOT}" --daemon "${CGRAPH_DAEMON}" "${CGRAPH_OPERATION}" "$@"
fi

exec "${CGRAPH_CLIENT}" --root "${CGRAPH_PROJECT_ROOT}" "${CGRAPH_OPERATION}" "$@"
