#!/usr/bin/env python3
"""Run 25 forward QD / CWindow mills with G3_WINDOW=yes hang-cap each."""
from __future__ import annotations

import gzip
import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPU = ROOT / "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp"
sys.path.insert(0, str(ROOT / "research-score" / "g3_driver"))

HANGCAP = Path.home() / ".grok/long-running-background-tasks/g3_apply_hangcap.sh"

NEVER = {
    "pef-setprm", "pef-rgbfg", "pef-cblit", "pef-cbhost",
    "pef-a8ecp", "pef-aa8ec", "pef-paintrm", "pef-welqd",
    "pef-peak430", "pef-restore430", "pef-setemptyrm", "pef-setemptyrst",
    "pef-welloop", "pef-sub2050", "pef-sdit2050", "pef-alrt1",
}

# G3_WINDOW confirm baseline, then forward CWindow/QD (fixed-anchor rm mills first).
QUEUE = [
    "pef-wplug802",
    "pef-frameroundrm",
    "pef-swtitle",
    "pef-mdret",
    "pef-mdtoc",
    "pef-layer",
    "pef-fillc",
    "pef-a991c",
    "pef-hsz64",
    "pef-ctllst",
    "pef-cupgnd",
    "pef-welmd",
    "pef-cb32b", "pef-cb32c", "pef-cb32d",
    "pef-gportrm", "pef-cwmgrprm", "pef-movetrm", "pef-textwrm",
    "pef-saversm", "pef-fillcrm", "pef-offsetrm", "pef-unionrm",
    "pef-newrgnrm", "pef-copyrgnrm", "pef-openrgnrm", "pef-closergnrm",
    "pef-emptyrgnrm", "pef-invalrm",
    "pef-getpenrm", "pef-setpenrm", "pef-getwvarrm",
    "pef-rgbfgh", "pef-drawmenurm", "pef-tickcountrm",
    "pef-cb32e", "pef-cb32f", "pef-closergnrm",
]

RESULTS: list[dict] = []


def reset_driver_state() -> None:
    """Allow another hang-cap after G3_WINDOW=yes sets run.g3=yes."""
    p = ROOT / "research-score/g3_driver/state.json"
    s = json.loads(p.read_text())
    run = s.setdefault("run", {})
    run.pop("g3", None)
    run["step"] = "hangcap"
    mill = s.setdefault("mill", {})
    mill["pending_hangcap"] = True
    mill.pop("last_fail", None)
    tips = s.setdefault("tips", {})
    for tip in tips.values():
        if isinstance(tip, dict):
            if tip.get("action") == "g3-lock":
                tip["action"] = "mill"
            if tip.get("state") == "g3-lock":
                tip["state"] = "open"
    p.write_text(json.dumps(s, separators=(",", ":")))


def in_tree(kind: str) -> bool:
    tok = kind.replace("pef-", "", 1)
    marker = "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok
    try:
        return marker in CPU.read_text()
    except OSError:
        return False


def fb_stats(n: int) -> dict:
    pgm = Path(f"/tmp/ss-g3-mill-{n}-fb.pgm")
    if not pgm.exists():
        return {"nonzero": 0, "bbox": None}
    with open(pgm, "rb") as f:
        f.readline()
        while True:
            line = f.readline()
            if not line.startswith(b"#"):
                break
        w, h = map(int, line.split())
        f.readline()
        data = f.read()
    nz = sum(1 for b in data if b != 0)
    xs, ys = [], []
    for y in range(h):
        row = data[y * w : (y + 1) * w]
        for x, b in enumerate(row):
            if b:
                xs.append(x)
                ys.append(y)
    bbox = (min(xs), min(ys), max(xs), max(ys)) if xs else None
    return {"nonzero": nz, "bbox": bbox}


def analyze_log(n: int) -> dict:
    row: dict = {"n": n}
    log = Path(f"/tmp/ss-g3-mill-{n}.log.gz")
    if not log.exists() or log.stat().st_size < 200:
        row["empty_log"] = True
        return row
    with gzip.open(log, "rb") as f:
        txt = f.read().decode("utf-8", "replace")
    for tag in ("msr-collapse", "68k-hang", "unknown-hb", "wait-cmp-fwd-bc"):
        if f"LIVE_CLASS={tag}" in txt:
            row["live"] = tag
            break
    row["g3_window"] = "G3_WINDOW=yes" in txt or "window=yes" in txt.lower()
    row["g2_hit"] = "G2 HIT" in txt or "G2: picspin" in txt
    row["bound_ble"] = txt.count("CopyBits bound cap")
    m = re.search(r"cb32\w s=\S+ w=(\d+)", txt)
    row["cb32w"] = int(m.group(1)) if m else 0
    row["sys2050"] = "System Error 2050" in txt or "NumToString" in txt and "2050" in txt
    row["wplug802"] = "wplug802 pc=" in txt
    row["skipditl"] = "skipditl DrawDialog" in txt
    row["cwindow129"] = "GetNewCWindow id=129" in txt
    row.update(fb_stats(n))
    return row


def build_queue(limit: int = 25) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for kind in QUEUE:
        if kind in NEVER or kind in seen:
            continue
        seen.add(kind)
        out.append(kind)
        if len(out) >= limit:
            break
    return out


def run_mill(kind: str, *, g3_window: bool) -> dict:
    print(
        f"\n===== {kind} (G3_WINDOW={'yes' if g3_window else 'no'}) in_tree={in_tree(kind)} =====",
        flush=True,
    )
    reset_driver_state()
    env = os.environ.copy()
    env["G3_NO_SKIP68K"] = "1"
    env["G3_GROK_BUILD"] = "0"
    env["PYTHONUNBUFFERED"] = "1"
    if g3_window:
        env["G3_WINDOW"] = "yes"
    else:
        env.pop("G3_WINDOW", None)
    r = subprocess.run(
        ["bash", str(HANGCAP), kind],
        cwd=ROOT,
        capture_output=True,
        text=True,
        env=env,
    )
    out = r.stdout + r.stderr
    print(out, flush=True)
    reset_driver_state()
    m = re.search(r"DONE: (KEEP|REVERT) leftover:(\S+)", out)
    decision = m.group(1) if m else "FAIL"
    logs = sorted(
        glob.glob("/tmp/ss-g3-mill-*.log.gz"),
        key=lambda p: Path(p).stat().st_mtime,
        reverse=True,
    )
    n = 0
    for lp in logs:
        if Path(lp).stat().st_size >= 200:
            n = int(Path(lp).name.split("-")[3].split(".")[0])
            break
    row = {"kind": kind, "decision": decision, "in_tree": in_tree(kind), **analyze_log(n)}
    RESULTS.append(row)
    return row


def main() -> None:
    queue = build_queue(25)
    print("QUEUE (%d):" % len(queue), queue, flush=True)
    for i, kind in enumerate(queue, 1):
        row = run_mill(kind, g3_window=(i == 1))
        print("RESULT[%d/%d]:" % (i, len(queue)), row, flush=True)
    print("\n===== SUMMARY (%d) =====" % len(RESULTS))
    for r in RESULTS:
        print(r)
    if RESULTS:
        best = max(RESULTS, key=lambda x: x.get("nonzero", 0))
        print("BEST_FB:", best)
        wins = [r for r in RESULTS if r.get("g3_window") or r.get("g2_hit")]
        print("G3_WINDOW/G2_HIT runs:", len(wins), wins[:5])


if __name__ == "__main__":
    main()
