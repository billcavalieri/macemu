#!/bin/bash
# Several VMs at once, started directly (no manager).   tools/vms/test.sh nvram
#   two library-style VM folders (.../VMs/<id>/prefs, each with its own disk copy) run at the same time:
#           both reach the Sheep Shears tool and shut down cleanly, each keeps its own NVRAM files, nothing is written to
#           the shared home-folder file; a third VM pointed at a disk that is in use is refused with the lock warning.
# Boots copies of your Mac OS 9 disk; set-up and overrides are described at the top of tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../../shears/lib.sh"
shears_check_environment
echo "This test starts three SheepShaver processes (two at once, then a third that must be refused)."

WORK="$(mktemp -d)"
PIDS=()
cleanup() { [ -n "${SHEARS_KEEP_LOGS:-}" ] && for i in a b c; do cp "$WORK/VMs/$i/log" "$SHEARS_KEEP_LOGS/vm-$i.log" 2>/dev/null; done; for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 1; for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/home" "$WORK/VMs/a" "$WORK/VMs/b" "$WORK/VMs/c"
shears_prepare "$WORK/VMs/a" SheepShears.bin
shears_prepare "$WORK/VMs/b" SheepShears.bin
# c's prefs name a's disk, which a holds locked while it runs
sed "s#^disk .*#disk $WORK/VMs/a/disk.hfv#" "$WORK/VMs/b/prefs" > "$WORK/VMs/c/prefs"

fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }
start() {   # start ID [seconds] -> log in $WORK/VMs/ID/log
    HOME="$WORK/home" NW_VERBOSE=1 NW_SHEARS_SHUTDOWN_AFTER=${2:-6} "$SHEEPSHAVER_APP" --config "$WORK/VMs/$1/prefs" > "$WORK/VMs/$1/log" 2>&1 &
    PIDS+=($!); eval "PID_$1=$!"; disown
}
waitfor() { local i=0; while [ $i -lt $(( $3 * 2 )) ]; do grep -aq "$2" "$WORK/VMs/$1/log" && return 0; sleep 0.5; i=$((i+1)); done; return 1; }

start a; start b
waitfor a "guest tool connected" 300; check "VM a reaches the Sheep Shears tool" $?
waitfor b "guest tool connected" 300; check "VM b reaches the Sheep Shears tool (while a runs)" $?
# a still holds its disk locked here only if it has not shut down yet; start c right away
start c 600
waitfor c "WARNING: Cannot open" 120; check "a VM using a disk that is in use gets the lock warning" $?
kill "$PID_c" 2>/dev/null
for id in a b; do
    eval "pid=\$PID_$id"; i=0; while [ $i -lt 360 ] && kill -0 "$pid" 2>/dev/null; do sleep 0.5; i=$((i+1)); done
    ! kill -0 "$pid" 2>/dev/null; check "VM $id shut down by itself" $?
done
for id in a b; do
    [ -s "$WORK/VMs/$id/nvram.flash" ]; check "VM $id has its own NVRAM flash" $?
done
[ ! -e "$WORK/home/.sheepshaver_nvram" ] && [ ! -e "$WORK/home/.sheepshaver_nvram.flash" ]; check "the shared home-folder NVRAM files were not used" $?
! grep -aq "WARNING: Cannot open" "$WORK/VMs/a/log" "$WORK/VMs/b/log"; check "the shared read-only CD image opens in both VMs (no lock warning in a or b)" $?
echo "---"; grep -ah "NVRAM:\|guest tool connected\|WARNING: Cannot" "$WORK"/VMs/*/log | head
[ $fail -eq 0 ] && echo "multi-VM test: passed" || echo "multi-VM test: FAILED"
exit $fail
