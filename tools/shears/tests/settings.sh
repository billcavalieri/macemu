#!/bin/bash
# The settings file. A clone of the disk gets the tool and a "Sheep Shears Prefs" file that leaves only the clipboard on.
# The tool reads it when it starts and tells the host, which logs what is in effect. Then the file is changed to switch
# everything off, the tool restarts, and the host must follow. Nothing is typed or clicked.
#
# Set-up and optional overrides: see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment
echo "This test starts SheepShaver twice with different Sheep Shears settings."

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
shears_prepare "$WORK" SheepShears.bin

# 'SHPF', version 1, the features that are ON (1 = edge release, 2 = clipboard)
writeprefs() {
    python3 -c "import struct,sys; open(sys.argv[1],'wb').write(struct.pack('>III', 0x53485046, 1, int(sys.argv[2])))" "$WORK/shprefs" "$1"
    ( export PATH="$PREFIX/bin:$PATH"; hmount "$WORK/disk.hfv" > /dev/null && hcd "System Folder" && hcd Preferences \
      && { hdel "Sheep Shears Prefs" 2> /dev/null; hcopy -r "$WORK/shprefs" ":Sheep Shears Prefs"; } && humount > /dev/null ) || { echo "could not write the settings file"; exit 1; }
}

fail=0
check() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi; }
run_once() {    # run_once LOGNAME EXPECTED-LINE; one retry when the guest never got as far as the tool (the known start-up flake)
    run_boot "$1"
    grep -aq "guest tool connected" "$WORK/$1" || run_boot "$1"
    grep -aq "$2" "$WORK/$1"
}
run_boot() {
    NW_VERBOSE=1 NW_SHEARS_SHUTDOWN_AFTER=4 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/$1" 2>&1 &
    P=$!; disown "$P"
    local i=0; while [ $i -lt 480 ] && kill -0 $P 2>/dev/null; do sleep 0.5; i=$((i+1)); done
    kill -0 $P 2>/dev/null && { kill $P; sleep 1; kill -9 $P 2>/dev/null; }
    P=""
}

writeprefs 1; run_once log1 "Sheep Shears: edge release on, clipboard off"; check "edge release only: edge release on, clipboard off" $?
writeprefs 2; run_once log2 "Sheep Shears: edge release off, clipboard on"; check "clipboard only: edge release off, clipboard on" $?
writeprefs 0; run_once log3 "Sheep Shears: edge release off, clipboard off"; check "everything off: edge release off, clipboard off" $?
echo "---"; grep -ah "Sheep Shears" "$WORK"/log? | head -30
[ -n "${SHEARS_KEEP_LOGS:-}" ] && cp "$WORK"/log? "$SHEARS_KEEP_LOGS"/
[ $fail -eq 0 ] && echo "settings test: passed" || echo "settings test: FAILED"
exit $fail
