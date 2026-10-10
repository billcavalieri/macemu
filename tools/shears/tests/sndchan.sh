#!/bin/bash
# Sound output teardown: a guest app (guest/sheepshears/sndchan.c) opens a sound channel, plays a quarter-second tone and
# disposes of the channel, four times over. Disposing closes the output device component (SheepBlaster) and the Apple
# Mixer. Passes when all four cycles run and the guest stays up.
#
# KNOWN TO FAIL NOW AND THEN (about one run in four when it was written, October 2026): the guest takes a system error
# (reported at 68k pc 002029fa) during a cycle. It is the crash seen at the end of songs in iTunes, it has nothing to do
# with sound input (which is off here), and this is the quickest way found to bring it out. It is not part of "all".
#
#   tools/shears/tests/sndchan.sh [pref=value ...]     pref=value replaces or adds a prefs line (jit=false, ...)
# Environment variables (NW_JIT_LEGACY=..., ...) are passed on to the emulator.
#
# Set-up and optional overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
set -uo pipefail
source "$(dirname "$0")/../lib.sh"
shears_check_environment

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null; [ -n "${SNDCHAN_KEEP:-}" ] && echo "kept: $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT

shears_prepare "$WORK" SndChan.bin
sed -i '' -e 's/^nosound true/nosound false/' "$WORK/prefs"
for kv in "$@"; do
    k="${kv%%=*}"; v="${kv#*=}"
    grep -v "^$k " "$WORK/prefs" > "$WORK/prefs.new"; echo "$k $v" >> "$WORK/prefs.new"; mv "$WORK/prefs.new" "$WORK/prefs"
done
NW_VERBOSE=1 "$SHEEPSHAVER_APP" --config "$WORK/prefs" > "$WORK/log" 2>&1 &
P=$!
disown "$P"
i=0
while [ $i -lt 360 ]; do
    grep -aq "SC done\|SysError #" "$WORK/log" && break
    kill -0 $P 2>/dev/null || break
    sleep 0.5; i=$((i+1))
done
sleep 3     # a crash that follows the last dispose shows up here
grep -a "SHEARS-GUEST: SC\|SysError #\|audio-mixer\|audio-close" "$WORK/log" | cut -c1-160
if grep -aq "SysError #" "$WORK/log"; then echo "sound channel teardown: CRASHED"; exit 1; fi
if grep -aq "SC done" "$WORK/log" && [ "$(grep -ac 'SC [0-9] dispose err=0' "$WORK/log")" = 4 ]; then echo "sound channel teardown: passed"; exit 0; fi
echo "sound channel teardown: FAILED (the guest app did not finish its four cycles)"; exit 2
