#!/bin/bash
# The manager: the library process (started with no --config) launches two VMs, each as its own process. A diagnostics-only
# hook (NW_MANAGER_LAUNCH, with NW_MANAGER_LOGDIR for the VMs' output) starts them, and NW_SHEARS_SHUTDOWN_AFTER makes each
# shut itself down once its Sheep Shears tool is up. The test passes when both VMs reach the tool, each exactly once,
# both then quit by themselves, and the manager is still running afterwards.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test starts the SheepShaver library window and two VMs from it."

WORK="$(mktemp -d)"
M=""
cleanup() { [ -n "$M" ] && kill "$M" 2>/dev/null; sleep 1; [ -n "$M" ] && kill -9 "$M" 2>/dev/null; pkill -f "$WORK/VMs" 2>/dev/null; [ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK"/logs/* "$SHEARS_KEEP_LOGS"/ 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/home" "$WORK/logs" "$WORK/VMs/a" "$WORK/VMs/b"
shears_prepare "$WORK/VMs/a" SheepShears.bin
shears_prepare "$WORK/VMs/b" SheepShears.bin

fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }
HOME="$WORK/home" NW_VERBOSE=1 NW_SHEARS_SHUTDOWN_AFTER=6 NW_MANAGER_LOGDIR="$WORK/logs" \
    NW_MANAGER_LAUNCH="$WORK/VMs/a/prefs,$WORK/VMs/a/prefs,$WORK/VMs/b/prefs" "$SHEEPSHAVER_APP" > "$WORK/manager.log" 2>&1 &
M=$!; disown
waitfor() { local i=0; while [ $i -lt $(( $2 * 2 )) ]; do grep -aq "guest tool connected" "$WORK/logs/$1.log" 2>/dev/null && return 0; sleep 0.5; i=$((i+1)); done; return 1; }
waitfor a 300; check "VM a, started by the manager, reaches the Sheep Shears tool" $?
waitfor b 300; check "VM b, started by the manager, reaches the Sheep Shears tool" $?
sleep 2
[ "$(grep -ac 'NW-BOOT NewWorld Boot' "$WORK/logs/a.log")" = 1 ]; check "starting a VM that is already running does not start it twice" $?
i=0; while [ $i -lt 360 ] && pgrep -f "$WORK/VMs/[ab]/prefs --vm-id" > /dev/null; do sleep 0.5; i=$((i+1)); done
! pgrep -f "$WORK/VMs/[ab]/prefs --vm-id" > /dev/null; check "both VM processes quit by themselves after the guest shut down" $?
kill -0 $M 2>/dev/null; check "the manager is still running after its VMs quit" $?
[ -s "$WORK/VMs/a/nvram.flash" ] && [ -s "$WORK/VMs/b/nvram.flash" ]; check "each VM kept its NVRAM in its own folder" $?
# The menu bars (listed in each process's output with diagnostics on): the library has the standard shortcuts; a VM's
# menus have none, because Mac OS 9 applications use Command-Q, -W, -N, -comma and -M themselves
grep -aq "^NW-MENU SheepShaver / Quit SheepShaver  cmd-q$" "$WORK/manager.log" && grep -aq "^NW-MENU File / New Virtual Machine…  cmd-n$" "$WORK/manager.log" \
    && grep -aq "^NW-MENU SheepShaver / Settings…  cmd-,$" "$WORK/manager.log"; check "the library's menu bar has Quit, New and Settings with their shortcuts" $?
grep -aq "^NW-MENU Guest / " "$WORK/logs/a.log" && ! grep -aE "^NW-MENU .*  (cmd-)?[^ ]+$" "$WORK/logs/a.log" | grep -q .; check "a VM's menus (Guest menu included) have no keyboard shortcuts" $?
echo "---"; grep -ah "guest tool connected\|NVRAM:" "$WORK"/logs/*.log | head
[ $fail -eq 0 ] && echo "manager test: passed" || echo "manager test: FAILED"
exit $fail
