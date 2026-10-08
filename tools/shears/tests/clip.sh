#!/bin/bash
# Round trip of the clipboard between the Mac pasteboard and a Mac OS 9 guest, through the Scrap Manager patches.
# A guest self-test app (guest/sheepshears/scraptest.c) reads and writes the scrap and reports through Sheep Shears LOG;
# this script sets and reads the real host pasteboard and compares. THE HOST PASTEBOARD IS REPLACED DURING THE TEST;
# its contents are saved first and restored when the script ends.
#
# Set-up and optional overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment
echo "This test replaces the Mac clipboard for about a minute (your clipboard is saved and restored) and opens a SheepShaver window."

WORK="$(mktemp -d)"
PB="$WORK/pbtool"
swiftc -O -o "$PB" "$ROOT/tools/shears/tests/pasteboard_tool.swift" || exit 1
"$PB" save "$WORK/pb.save"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; "$PB" restore "$WORK/pb.save"; rm -rf "$WORK"; }
trap cleanup EXIT

shears_prepare "$WORK" ShearsScrapTest.bin

H1=$'héllo — “quoted” ünï\nline two'
H2='second host clip'
"$PB" set "$H1"
NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
P=$!
disown "$P"    # the script stops the emulator itself at the end; no "Terminated" notice
waitfor() { local i=0; while [ $i -lt $(( $2 * 2 )) ]; do grep -aq "$1" "$WORK/log" && return 0; kill -0 $P 2>/dev/null || return 1; sleep 0.5; i=$((i+1)); done; return 1; }

fail=0
waitfor "STEP1 host->guest" 240 || { echo "FAIL: the test app never reported (guest did not boot or the tool did not run)"; exit 1; }
waitfor "STEP2 guest->host" 60 || { echo "FAIL: step 2 never reported"; exit 1; }
sleep 1
GOT2="$("$PB" get)"
waitfor "STEP3 sum" 60 || { echo "FAIL: step 3 never reported"; exit 1; }
sleep 1
GOT3="$("$PB" get | wc -c | tr -d ' ')"
"$PB" set "$H2"
waitfor "STEP4 host->guest" 60 || { echo "FAIL: step 4 never reported"; exit 1; }

python3 - "$WORK/log" "$H1" "$H2" "$GOT2" "$GOT3" <<'PY' || fail=1
import re, sys, zlib
log = open(sys.argv[1], errors='replace').read()
h1, h2, got2, got3 = sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
def line(tag):
    m = re.search(r'SHEARS-GUEST: ' + re.escape(tag) + r'[^\n]*', log)
    return m.group(0) if m else ''
ok = True
def check(name, cond, detail=''):
    global ok
    print(('PASS  ' if cond else 'FAIL  ') + name + ('' if cond else '  ' + detail)); ok &= bool(cond)
def seen(tag):
    l = line(tag)
    m = re.search(r'len=(\d+) sum=([0-9a-f]+) hex=([0-9a-f]*)', l)
    return (int(m.group(1)), m.group(2), m.group(3)) if m else None
for tag, text, name in (('STEP1', h1, 'host -> guest text'), ('STEP4', h2, 'host -> guest, second copy')):
    exp = text.replace('\n', '\r').encode('mac_roman')
    s = seen(tag)
    check(name, s == (len(exp), '%08x' % zlib.adler32(exp), exp[:36].hex()), f'saw {s}, expected {(len(exp), "%08x" % zlib.adler32(exp), exp[:36].hex())}')
exp2 = 'guest éü — “quoted”' + ('\r' if False else '\n') + 'second line'
check('guest -> host text', got2 in (exp2, exp2.replace('\n', '\r')), f'host has {got2!r}')
check('guest -> host 1 MB', got3 == 1024 * 1024, f'host text is {got3} bytes')
sys.exit(0 if ok else 1)
PY
echo "(log: $(grep -ac 'SHEARS-GUEST' "$WORK/log") guest lines)"
[ $fail -eq 0 ] && echo "clipboard round trip: passed" || { echo "clipboard round trip: FAILED"; grep -a "SHEARS-GUEST" "$WORK/log" | head -20; }
exit $fail
