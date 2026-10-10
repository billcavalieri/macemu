#!/bin/bash
# Sound input from the host microphone, through the Sound Manager's SPB calls. A guest self-test app
# (guest/sheepshears/miccheck.c) opens the input device and records; the "microphone" is a 440 Hz test tone the emulator
# generates (NW_MIC_TONE), so the test needs no real microphone and no permission. The guest reports through Sheep Shears
# LOG; this script checks that the pitch it heard and the sizes are right. Nothing on your Mac is touched except a
# SheepShaver window that opens for the run.
#
# Set-up and optional overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; [ -n "${MIC_KEEP:-}" ] && echo "kept: $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT

shears_prepare "$WORK" MicCheck.bin
sed -i '' -e 's/^nosound true/nosound false/' "$WORK/prefs"
# MIC_OFF=1 is a control run: sound input stays off (the emulator changes nothing in the guest) and the same guest app runs.
# The checks fail, of course; what matters then is the log, to tell a guest problem from one the sound input code causes.
if [ -n "${MIC_OFF:-}" ]; then
    NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
else
    echo "mic true" >> "$WORK/prefs"
    NW_VERBOSE=1 NW_MIC_TONE=440 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
fi
P=$!
disown "$P"
waitfor() { local i=0; while [ $i -lt $(( $2 * 2 )) ]; do grep -aq "$1" "$WORK/log" && return 0; kill -0 $P 2>/dev/null || return 1; sleep 0.5; i=$((i+1)); done; return 1; }

fail=0
waitfor "MIC done" 300 || { echo "FAIL: the test app never finished (guest did not boot, or a call hung)"; grep -a "SHEARS-GUEST: MIC\|NW-MIC" "$WORK/log" | tail -20; exit 1; }

python3 - "$WORK/log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], errors='replace').read()
ok = True
def check(name, cond, detail=''):
    global ok
    print(('PASS  ' if cond else 'FAIL  ') + name + ('' if cond else '  ' + detail)); ok &= bool(cond)
def line(pat):
    m = re.search(r'SHEARS-GUEST: (' + pat + r'[^\n]*)', log)
    return m.group(1) if m else ''
def num(l, key):
    m = re.search(key + r'=(-?[0-9a-fx]+)', l)
    return int(m.group(1), 16 if 'x' in m.group(1) or key in ('ref', 'rate', 'value') else 10) if m else None
gv = num(line('MIC Gestalt snd'), 'value')
check("Gestalt 'snd ' says there is a sound input device (bits 4 to 7)", gv is not None and (gv & 0xf0) == 0xf0, line('MIC Gestalt snd'))
check("the device list has \"SheepBlaster\"", "MIC device name 'SheepBlaster'" in log, line('MIC device'))
check("a second device index is refused", num(line('MIC device 2'), 'err') not in (0, None), line('MIC device 2'))
check("SPBOpenDevice for writing works", num(line('MIC open'), 'err') == 0, line('MIC open'))
check("the default rate is 22254.5 Hz", num(line('MIC rate'), 'rate') == 0x56ee8ba3, line('MIC rate'))
check("the default sample size is 8 bit, mono", num(line('MIC size'), 'size') == 8 and num(line('MIC channels'), 'chans') == 1, line('MIC size') + line('MIC channels'))
check("a second writer is refused with siDeviceBusyErr (-227)", num(line('MIC second writer'), 'err') == -227, line('MIC second writer'))
for label, bytes_exp in (('async', 22254), ('sync', 88200)):
    l = line('MIC ' + label + ' done')
    count, pitch, peak = num(l, 'count'), num(l, 'pitch_x10'), num(l, 'peak')
    st = line('MIC ' + label + ' start')
    if label == 'async':
        check("async: the SPB's error field is above 0 while the recording runs", num(st, 'err') == 0 and (num(st, 'pberr') or 0) > 0, st)
        check("async recording completed with its completion routine", num(l, 'done') == 1 and num(l, 'error') == 0, l)
    else:
        check("sync recording returned without an error", num(l, 'error') == 0, l)
    check(f"{label}: about {bytes_exp} bytes recorded", count is not None and abs(count - bytes_exp) <= bytes_exp * 0.08, l)
    check(f"{label}: the 440 Hz tone is heard (pitch within 2%)", pitch is not None and 4310 <= pitch <= 4490, l)
    check(f"{label}: the level is real (peak)", peak is not None and peak > (30 if label == 'async' else 6000), l)
for label, lo, hi in (('file', 20500, 24000), ('filestop', 15000, 40000)):
    l = line('MIC ' + label + ' done')
    eof, pitch, peak = num(l, 'eof'), num(l, 'pitch_x10'), num(l, 'peak')
    check(f"{label}: SPBRecordToFile wrote the samples to the file ({lo}..{hi} bytes)", eof is not None and lo <= eof <= hi, l)
    check(f"{label}: the file holds the 440 Hz tone", pitch is not None and 4310 <= pitch <= 4490 and (peak or 0) > 30, l)
    check(f"{label}: the completion routine ran without an error", num(l, 'done') == 1 and num(l, 'error') == 0, l)
l = line('MIC stream interrupts')
check("continuous recording: interrupt routine called several times", (num(l, 'interrupts') or 0) >= 3, l)
check("continuous recording: SPBStopRecording ran the completion routine", num(l, 'completion') == 1, l)
check("SPBCloseDevice works", num(line('MIC close'), 'err') == 0, line('MIC close'))
sys.exit(0 if ok else 1)
PY
[ $fail -eq 0 ] && echo "sound input: passed" || { echo "sound input: FAILED"; grep -a "SHEARS-GUEST: MIC\|NW-MIC" "$WORK/log" | head -60; }
exit $fail
