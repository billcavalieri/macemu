#!/bin/bash
# SheepBench boot tests. Each boots a clone of the test disk without a window (--embedded --background), with SheepBench
# in Startup Items and a "SheepBench Auto" file in the System Folder that makes it run at once, and checks the report.
#   tools/sheepbench/test.sh [mode] [SUITES=cpu,memory] [pref=value ...]
#     switch     (default) bench_switch on: the JIT-off pass runs; percent of native and the JIT speedup are checked
#     noswitch   bench_switch off: the app must not get the switch; no JIT-off values
#     corrupt    NW_BENCH_CORRUPT=1 makes the host's native checksums wrong: the rows must turn FAILED
#     nosig      NW_BENCH_NO_SIG=1 hides the host's signature: the app must never call the host (it would quit a stock
#                SheepShaver), report the host as unknown, write its report to the disk and shut the guest down
#     all        all of the above
# Set-up and overrides (SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM, ...): see tools/shears/lib.sh.
# SHEEPBENCH_KEEP=1 keeps the work folders; SHEEPBENCH_KEEP_REPORT=PATH copies the report there (also when a check
# fails); SHEEPBENCH_QUICK=0 runs the full measurement rule (slower); SHEEPBENCH_TIMEOUT=seconds (default 900).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../shears/lib.sh"
mode="${1:-switch}"
[ $# -gt 0 ] && shift
SUITES="cpu,memory"
PREFS=()
for a in "$@"; do
    case "$a" in SUITES=*) SUITES="${a#SUITES=}" ;; *) PREFS+=("$a") ;; esac
done

if [ "$mode" = all ]; then
    rc=0
    for m in switch noswitch corrupt nosig; do echo "== $m"; "$0" "$m" "$@" || rc=1; done
    exit $rc
fi
case "$mode" in switch|noswitch|corrupt|nosig) ;; *) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;; esac

shears_check_environment
for f in SheepShaver/src/sheepbench_host.cpp SheepShaver/src/MacOSX/SheepApp/Shears/BenchHost.swift \
         SheepShaver/src/MacOSX/SheepApp/Shears/BenchProtocol.swift SheepShaver/src/main.cpp; do
    if [ "${SHEARS_ALLOW_STALE:-0}" != 1 ] && [ "$ROOT/$f" -nt "$SHEEPSHAVER_APP" ]; then
        echo "The app is older than $f. Rebuild it in Xcode first (or set SHEARS_ALLOW_STALE=1)." >&2
        exit 1
    fi
done
"$HERE/build.sh" --apps-only || exit 1

WORK="$(mktemp -d)"
P=""
cleanup() { [ -n "$P" ] && kill "$P" 2>/dev/null; sleep 1; [ -n "$P" ] && kill -9 "$P" 2>/dev/null
            [ "${SHEEPBENCH_KEEP:-0}" = 1 ] && echo "kept: $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT

shears_prepare "$WORK" ../sheepbench/SheepBench.bin
quick="${SHEEPBENCH_QUICK:-1}"
{
    echo "suites ${SUITES//,/ }"
    echo "jit 1"
    echo "gpu 1"
    echo "quick $quick"
    [ "$mode" = nosig ] && { echo "report 1"; echo "shutdown 1"; }
} > "$WORK/auto.txt"
( export PATH="$PREFIX/bin:$PATH"
  hmount "$WORK/disk.hfv" > /dev/null && hcopy -t "$WORK/auto.txt" ":System Folder:SheepBench Auto" && humount > /dev/null ) ||
    { echo "could not write the auto file"; exit 1; }
setpref() { grep -v "^$1 " "$WORK/prefs" > "$WORK/prefs.new"; echo "$1 $2" >> "$WORK/prefs.new"; mv "$WORK/prefs.new" "$WORK/prefs"; }
[ "$mode" = noswitch ] && setpref bench_switch false || setpref bench_switch true
for kv in "${PREFS[@]+"${PREFS[@]}"}"; do setpref "${kv%%=*}" "${kv#*=}"; done

ENVV=(NW_VERBOSE=1)
[ "$mode" = corrupt ] && ENVV+=(NW_BENCH_CORRUPT=1)
[ "$mode" = nosig ] && ENVV+=(NW_BENCH_NO_SIG=1)
env "${ENVV[@]}" "$SHEEPSHAVER_APP" --config "$WORK/prefs" --vm-id "sbench$$" --vm-dir "$WORK" --embedded --background \
    > "$WORK/log" 2>&1 &
P=$!
disown "$P"

limit=${SHEEPBENCH_TIMEOUT:-900}
start=$SECONDS
while [ $((SECONDS - start)) -lt "$limit" ]; do
    kill -0 "$P" 2>/dev/null || break
    grep -aq "SysError #" "$WORK/log" && break
    [ "$mode" != nosig ] && grep -aq "SHEARS-GUEST: SHEEPBENCH DONE" "$WORK/log" && break
    sleep 1
done
sleep 2
echo "elapsed $((SECONDS - start)) s"
grep -a "SheepBench:\|SHEARS-GUEST: SHEEPBENCH\|SysError #" "$WORK/log" | cut -c1-200
if grep -aq "SysError #" "$WORK/log"; then echo "sheepbench $mode: the guest CRASHED"; exit 1; fi

case "$mode" in
nosig)
    kill -0 "$P" 2>/dev/null && { echo "sheepbench nosig: the guest did not shut down in time"; exit 1; }
    P=""
    grep -aq "SHEARS-GUEST: SHEEPBENCH" "$WORK/log" && { echo "sheepbench nosig: the app called the host"; exit 1; }
    ( export PATH="$PREFIX/bin:$PATH"
      hmount "$WORK/disk.hfv" > /dev/null && hcopy -r ":System Folder:SheepBench Report" "$WORK/report.txt"; humount > /dev/null ) ||
        { echo "sheepbench nosig: no report on the disk"; exit 1; }
    python3 "$HERE/check_report.py" "$WORK/report.txt" --expect-no-host --suites "$SUITES" || exit 1
    ;;
*)
    grep -aq "SHEARS-GUEST: SHEEPBENCH DONE" "$WORK/log" || { echo "sheepbench $mode: the app did not finish"; tail -20 "$WORK/log"; exit 1; }
    sleep 1
    tsv="$(ls "$WORK"/SheepBench/*.tsv 2>/dev/null | head -1)"
    [ -n "$tsv" ] || { echo "sheepbench $mode: the host wrote no report"; exit 1; }
    [ -f "${tsv%.tsv}.json" ] || { echo "sheepbench $mode: no JSON next to the report"; exit 1; }
    python3 -c "import json,sys; d=json.load(open(sys.argv[1])); assert d['results'] and d['info']" "${tsv%.tsv}.json" ||
        { echo "sheepbench $mode: the JSON does not parse"; exit 1; }
    [ -n "${SHEEPBENCH_KEEP_REPORT:-}" ] && cp "$tsv" "$SHEEPBENCH_KEEP_REPORT"
    case "$mode" in
    switch)   python3 "$HERE/check_report.py" "$tsv" --expect-host --expect-jit-off --suites "$SUITES" || exit 1 ;;
    noswitch) python3 "$HERE/check_report.py" "$tsv" --expect-host --expect-no-jit-off --suites "$SUITES" || exit 1 ;;
    corrupt)  python3 "$HERE/check_report.py" "$tsv" --expect-failures --suites "$SUITES" || exit 1 ;;
    esac
    ;;
esac
echo "sheepbench $mode: passed"
