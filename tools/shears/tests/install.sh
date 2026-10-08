#!/bin/bash
# The installer, headless. A clone of the disk gets the installer application, its folder "Sheep Shears Files" and the
# flag file "Sheep Shears Auto Install" in Startup Items, so the installer runs at start-up without a window and
# installs the tool and the control panel. The test then checks that the tool came up (the host sees it) and, after the
# host has shut the guest down, that both files are on the disk with the right types. Nothing is typed or clicked.
#
# Set-up and optional overrides: see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment
echo "This test installs Sheep Shears into a copy of the guest disk and checks the result."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
shears_prepare "$WORK"
B="$ROOT/build/sheepshears"
( export PATH="$PREFIX/bin:$PATH"
  hmount "$WORK/disk.hfv" > /dev/null && hcd "System Folder" || exit 1
  printf x > "$WORK/flag" && hcopy -r "$WORK/flag" ":Sheep Shears Auto Install" || exit 1
  hcd "Startup Items" || exit 1
  hcopy -m "$B/SheepShearsInstaller.bin" ":" && hrename SheepShearsInstaller "Install Sheep Shears" || exit 1
  hmkdir "Sheep Shears Files" || exit 1
  hcopy -m "$B/SheepShears.bin" ":Sheep Shears Files:" && hrename ":Sheep Shears Files:SheepShears" ":Sheep Shears Files:Sheep Shears Tool" || exit 1
  hcopy -m "$B/SheepShearsPanel.bin" ":Sheep Shears Files:" && hrename ":Sheep Shears Files:SheepShearsPanel" ":Sheep Shears Files:Sheep Shears" || exit 1
  hattrib -t appc ":Sheep Shears Files:Sheep Shears" || exit 1
  humount > /dev/null ) || { echo "could not put the installer on the disk"; exit 1; }

NW_VERBOSE=1 NW_SHEARS_SHUTDOWN_AFTER=5 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
P=$!
disown "$P"
fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }
waitfor() { local i=0; while [ $i -lt $(( $2 * 2 )) ]; do grep -aq "$1" "$WORK/log" && return 0; kill -0 $P 2>/dev/null || return 1; sleep 0.5; i=$((i+1)); done; return 1; }

waitfor "installer: done" 240; check "the installer ran and reports success" $?
waitfor "guest tool connected" 60; check "the tool it installed and started reached the host" $?
i=0; while [ $i -lt 240 ] && kill -0 $P 2>/dev/null; do sleep 0.5; i=$((i+1)); done
! kill -0 $P 2>/dev/null; check "the guest shut down and the emulator quit" $?
sleep 1
LISTING="$( export PATH="$PREFIX/bin:$PATH"; hmount "$WORK/disk.hfv" > /dev/null && hls -l ":System Folder:Control Panels:Sheep Shears" ":System Folder:Startup Items:Sheep Shears Tool" 2>&1; humount > /dev/null 2>&1 )"
echo "$LISTING" | grep -q "appc/ShSp"; check "the control panel is in Control Panels (type appc)" $?
echo "$LISTING" | grep -q "APPL/ShSh"; check "the tool is in Startup Items" $?
echo "---"; grep -a "Sheep Shears\|SHEARS-GUEST" "$WORK/log" | head -12; echo "$LISTING"
[ $fail -eq 0 ] && echo "install test: passed" || echo "install test: FAILED"
exit $fail
