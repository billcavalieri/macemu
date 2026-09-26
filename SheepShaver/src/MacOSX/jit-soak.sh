#!/bin/bash
# Host soak: launch mill Debug ON, SIGTERM after N seconds, print jit-summary.
# Does not open the QT clip (guest). Diff two summary files by passing a second path.
set -euo pipefail
N=${1:-60}
OUT=${2:-/tmp/g8/jit-summary.txt}
PREV=${3:-}
BIN=/tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver.app/Contents/MacOS/SheepShaver
test -x "$BIN"
pgrep -x SheepShaver && { echo 'SheepShaver already running' >&2; exit 1; }
mkdir -p "$(dirname "$OUT")" /tmp/g8
HOME=/tmp/g8/home NW_JIT=on NW_JIT_SUMMARY="$OUT" \
  "$BIN" --config /tmp/prefs-hd >/tmp/g8/soak.log 2>&1 &
pid=$!
sleep "$N"
kill -TERM "$pid" 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do
  kill -0 "$pid" 2>/dev/null || break
  sleep 1
done
kill -KILL "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "=== $OUT ==="
test -f "$OUT" && cat "$OUT"
if [ -n "$PREV" ] && [ -f "$PREV" ]; then
  echo "=== diff $PREV ==="
  diff -u "$PREV" "$OUT" || true
fi
