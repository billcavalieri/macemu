#!/bin/bash
# The host asks the guest to shut down properly. Boots a clone of the disk with the Sheep Shears tool in Startup Items;
# a diagnostics-only hook (NW_SHEARS_SHUTDOWN_AFTER) makes the host request a shutdown a few seconds after the tool
# appears. The test passes when the emulator quits by itself: the tool asked the Finder to shut down, Mac OS finished,
# and the guest powered off through the PMU. Nothing is typed or clicked, and your clipboard is not touched.
#
# Set-up and optional overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment
echo "This test starts SheepShaver, waits for the Sheep Shears tool, then shuts the guest down from the host."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
shears_prepare "$WORK" SheepShears.bin

NW_VERBOSE=1 NW_SHEARS_SHUTDOWN_AFTER=5 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
P=$!
disown "$P"
waitfor() { local i=0; while [ $i -lt $(( $2 * 2 )) ]; do grep -aq "$1" "$WORK/log" && return 0; kill -0 $P 2>/dev/null || return 1; sleep 0.5; i=$((i+1)); done; return 1; }

fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }

waitfor "guest tool connected" 240; check "host sees the tool running in the guest" $?
waitfor "shutdown requested" 30; check "host requests the shutdown" $?
waitfor "the guest was asked to shut down" 30; check "the tool acknowledged: the Finder has the request" $?
# Mac OS now quits the applications and powers off; the emulator exits on its own.
i=0; while [ $i -lt 240 ] && kill -0 $P 2>/dev/null; do sleep 0.5; i=$((i+1)); done
! kill -0 $P 2>/dev/null; check "the emulator quit by itself after the guest shut down" $?
grep -aq "PMU shutdown" "$WORK/log"; check "the guest powered off through the PMU" $?
echo "---"; grep -a "Sheep Shears\|SHEARS-GUEST\|PMU shutdown" "$WORK/log" | head -12
[ $fail -eq 0 ] && echo "shutdown test: passed" || echo "shutdown test: FAILED"
exit $fail
