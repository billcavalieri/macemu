#!/bin/bash
# Tests for several VMs at once.   tools/vms/test.sh [unit|nvram|manager|control|display|embedded|mcp|settings|all]
#   nvram     two VMs started directly, then a third that must be refused: own NVRAM files, lock warning, shared CD
#   unit      the control channel's pure code (pixels, keys, pointer planner, protocol): seconds, nothing booted
#   control   one VM driven through its control socket: status, screenshot, pointer, keyboard, shutdown
#   display   an embedded VM (no window): shared-memory frames, the display socket's input and events
#   embedded  the library window shows an embedded VM's picture, forwards input, and picks up a running one
#   mcp       the MCP server end to end: access rules, allow-list, start/screenshot/pointer/keyboard/shutdown, stdio bridge
#   settings  the settings sheet is opened and saved without a change: the prefs file must come back byte for byte
#   manager   the library process (no --config) starts two VMs as separate processes and stays up when they quit
#   all       every test above
# They boot copies of your Mac OS 9 disk and open SheepShaver windows; set-up and overrides are described at the top of
# tools/shears/lib.sh. Nothing is clicked or typed.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)/tests"
case "${1:-all}" in
    unit|nvram|manager|control|display|embedded|mcp|settings) "$DIR/$1.sh" ;;
    all) rc=0; for t in unit settings nvram manager control display embedded mcp; do echo "== $t"; "$DIR/$t.sh" || rc=1; done; exit $rc ;;
    *) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
