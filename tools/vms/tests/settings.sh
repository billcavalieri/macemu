#!/bin/bash
# The per-VM settings sheet must not damage a prefs file. A prefs file with two disks, a CD, comments, a blank line and
# a key the sheet has no control for is added to the library; the sheet is opened and saved without touching a control
# (diagnostics hooks NW_OPEN_SHEET=settings, NW_SETTINGS_SAVE=1) and the file must come back byte for byte. (The old
# sheet kept only the last `disk` line and dropped the comments.) No VM is started and nothing is clicked.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test opens and saves the settings sheet of a library entry; no virtual machine is started."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/lib" "$WORK/src"
cat > "$WORK/src/prefs" <<PREFS
# my Mac OS 9 machine
disk /Users/someone/os9.qcow2
disk /Users/someone/data.hfv
cdrom /Users/someone/Mac OS 9.2.1.toast
rom 

screen win/1024/768
ramsize 536870912
future_key some value we do not know
jit true
PREFS
cp "$WORK/src/prefs" "$WORK/expected"

fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }
NW_VERBOSE=1 NW_VM_LIBRARY="$WORK/lib" NW_MANAGER_ADD="$WORK/src/prefs" NW_OPEN_SHEET=settings NW_SETTINGS_SAVE=1 \
    "$SHEEPSHAVER_APP" > "$WORK/log" 2>&1 &
P=$!; disown
i=0; while [ $i -lt 60 ] && ! grep -aq "NW-SETTINGS saved" "$WORK/log"; do sleep 0.5; i=$((i+1)); done
grep -aq "NW-SETTINGS saved" "$WORK/log"; check "the settings sheet opened and saved" $?
saved="$(ls -d "$WORK"/lib/*/prefs 2>/dev/null | head -1)"
[ -n "$saved" ] && cmp -s "$saved" "$WORK/expected"; check "saving without a change leaves the prefs file byte for byte as it was (two disks, comments and unknown keys kept)" $?
[ -n "$saved" ] && [ "$(grep -c '^disk ' "$saved")" = 2 ]; check "both disks are still there" $?
[ $fail -eq 0 ] && echo "settings test: passed" || { echo "settings test: FAILED"; [ -n "$saved" ] && diff "$WORK/expected" "$saved"; }
exit $fail
