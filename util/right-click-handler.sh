#!/bin/sh
# Simple example right-click handler for rethread.
# It reads the serialized payload from $RETHREAD_CONTEXT_PAYLOAD and forwards it
# to the legacy `menu x` command so existing scripts keep working.

set -eu

payload="${RETHREAD_CONTEXT_PAYLOAD:-${RETHREAD_MENU_PAYLOAD:-}}"
if [ -z "$payload" ]; then
  exit 0
fi

handler_command="${RETHREAD_CONTEXT_WRAPPER:-${RETHREAD_MENU_WRAPPER:-menu x}}"

printf '%s' "$payload" | /bin/sh -c "$handler_command"
