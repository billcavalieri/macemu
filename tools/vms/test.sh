#!/bin/bash
# Tests for several VMs at once.   tools/vms/test.sh [unit|nvram|manager|control|mcp|all]
#   nvram     two VMs started directly, then a third that must be refused: own NVRAM files, lock warning, shared CD
#   unit      the control channel's pure code (pixels, keys, pointer planner, protocol): seconds, nothing booted
#   control   one VM driven through its control socket: status, screenshot, pointer, keyboard, shutdown
#   mcp       the MCP server end to end: access rules, allow-list, start/screenshot/pointer/keyboard/shutdown, stdio bridge
#   manager   the library process (no --config) starts two VMs as separate processes and stays up when they quit
#   all       every test above
# They boot copies of your Mac OS 9 disk and open SheepShaver windows; set-up and overrides are described at the top of
# tools/shears/lib.sh. Nothing is clicked or typed.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)/tests"
case "${1:-all}" in
    unit|nvram|manager|control|mcp) "$DIR/$1.sh" ;;
    all) rc=0; for t in unit nvram manager control mcp; do echo "== $t"; "$DIR/$t.sh" || rc=1; done; exit $rc ;;
    *) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
