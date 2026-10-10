#!/bin/bash
# The Sound Manager's record window (SimpleSound's File > New), opened and cancelled by a guest app with no hand on the
# mouse (guest/sheepshears/recdlg.c): it probes the input as SimpleSound does, calls SndRecordToFile, and its filter
# procedure answers "Cancel" after five seconds. Opening the window opens the sound input and an output channel; closing
# it closes both. The "microphone" is a test tone. Passes when the call returns "cancelled" and the guest stays up.
#
# This is the path that took SimpleSound (and then the whole guest) down while SPBOpenDevice handed out a positive
# reference: the window passes the reference to the Device Manager, where 2 is the System file.
#
#   tools/shears/tests/recdlg.sh [pref=value ...]     pref=value replaces or adds a prefs line
# Environment variables (NW_MIC_TRACE=1, ...) are passed on to the emulator.
#
# Set-up and optional overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; [ -n "${RECDLG_KEEP:-}" ] && echo "kept: $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT

shears_prepare "$WORK" RecDlg.bin
grep -q "^mic " "$WORK/prefs" || echo "mic true" >> "$WORK/prefs"
sed -i '' -e 's/^nosound true/nosound false/' "$WORK/prefs"
for kv in "$@"; do
    k="${kv%%=*}"; v="${kv#*=}"
    grep -v "^$k " "$WORK/prefs" > "$WORK/prefs.new"; echo "$k $v" >> "$WORK/prefs.new"; mv "$WORK/prefs.new" "$WORK/prefs"
done
NW_VERBOSE=1 NW_MIC_TONE=440 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
P=$!
disown "$P"
i=0
while [ $i -lt 360 ]; do
    grep -aq "RD done\|SysError #" "$WORK/log" && break
    kill -0 $P 2>/dev/null || break
    sleep 0.5; i=$((i+1))
done
sleep 3     # a crash that follows the last dispose shows up here
grep -a "SHEARS-GUEST: RD\|SysError #\|audio-mixer\|NW-MIC: SPB\|NW-MIC: rec" "$WORK/log" | cut -c1-160
if grep -aq "SysError #" "$WORK/log"; then echo "record window: CRASHED"; exit 1; fi
if grep -aq "RD done" "$WORK/log" && grep -aq "RD after err=-128" "$WORK/log" && grep -aq "RD probe open err=0" "$WORK/log"; then echo "record window: passed"; exit 0; fi
echo "record window: FAILED (the guest app did not finish, or the window did not open and cancel)"; exit 2
