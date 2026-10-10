#!/bin/bash
# Same-machine A/B of the guest workload (tools/perf/workload.py): alternates runs of two builds (or one build with two
# environments) and prints the CPU seconds of each phase, so a change is judged on host CPU time, not on a sampling profile.
#
#   tools/perf/ab_workload.sh OUTDIR RUNS "A-label|APP|ENV..." "B-label|APP|ENV..."
#
#   e.g.  tools/perf/ab_workload.sh /tmp/ab 3 "base|/path/base.app/Contents/MacOS/SheepShaver|" "new|/path/new.app/Contents/MacOS/SheepShaver|"
#         tools/perf/ab_workload.sh /tmp/ab 3 "old|$APP|NW_JIT_LEGACY=arena" "new|$APP|"
# ENV is a space-separated list of K=V given to the emulator. Each run is a full boot + gui + idle (about 100 s).
set -u
OUT="$1"; RUNS="$2"; A="$3"; B="$4"
mkdir -p "$OUT"
HERE="$(cd "$(dirname "$0")" && pwd)"
run() {   # label app env round
    local label="$1" app="$2" envs="$3" round="$4" args=()
    for kv in $envs; do args+=(--env "$kv"); done
    python3 "$HERE/workload.py" "$OUT" --app "$app" --label "$label.$round" --no-profile --gui-rounds 3 --idle 15 ${args[@]+"${args[@]}"} > /dev/null 2>&1
    python3 - "$OUT/$label.$round.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("%-14s boot %6.2f s   gui %6.2f s   idle %5.2f s" % (d["label"], d["boot_cpu"], d["gui_cpu"], d["idle_cpu"]))
PY
}
IFS='|' read -r la pa ea <<< "$A"
IFS='|' read -r lb pb eb <<< "$B"
for r in $(seq 1 "$RUNS"); do
    run "$la" "$pa" "$ea" "$r"
    run "$lb" "$pb" "$eb" "$r"
done
python3 - "$OUT" "$la" "$lb" <<'PY'
import glob, json, statistics, sys
out, la, lb = sys.argv[1:4]
def col(l, k): return [json.load(open(f))[k] for f in sorted(glob.glob(f"{out}/{l}.*.json"))]
print()
for k in ("boot_cpu", "gui_cpu", "idle_cpu"):
    a, b = col(la, k), col(lb, k)
    if a and b:
        print("%-9s %s median %6.2f   %s median %6.2f   change %+5.1f%%" % (k, la, statistics.median(a), lb, statistics.median(b), 100 * (statistics.median(b) / statistics.median(a) - 1)))
PY
