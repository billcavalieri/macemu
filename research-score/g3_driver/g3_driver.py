#!/usr/bin/env python3
"""G3 driver: classify NW-BOOT hangs by opcode class. No SheepShaver C++ mill."""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, Optional

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from classify import classify_text, escalate_action, format_classify, load_taxonomy
from debug_run import debug_sha, repo_root
from mill_escalate import write_escalate
from qwen_lock import score_g3


def _state_path() -> Path:
    return HERE / "state.json"


def load_state() -> Dict[str, Any]:
    p = _state_path()
    if not p.is_file():
        return {"denylist": [], "widen": {}, "tips": {}}
    return json.loads(p.read_text())


def save_state(st: Dict[str, Any]) -> None:
    _state_path().write_text(json.dumps(st, indent=2) + "\n")


def _read_log(path: Path) -> str:
    return path.read_text(errors="replace")


def cmd_classify(args: argparse.Namespace) -> int:
    text = _read_log(Path(args.log))
    report = classify_text(text)
    print(format_classify(report))
    return 0


def _record_tip(st: Dict[str, Any], sha: str, report: Dict[str, Any], action: str) -> None:
    last_hb = report.get("last_hb") or {}
    st.setdefault("tips", {})
    st["tips"][sha[:8]] = {
        "class": report.get("LIVE_CLASS"),
        "tip_sha": sha,
        "live_pc": last_hb.get("pc"),
        "live_op": last_hb.get("op"),
        "live_nxt": last_hb.get("nxt"),
        "action": action,
        "next_sha": None,
        "class_moved": False,
        "g2_live": bool(report.get("g2_live")),
        "window": None,
        "fb_dirty": None,
        "state": "classified",
    }


def cmd_score(args: argparse.Namespace) -> int:
    text = _read_log(Path(args.log))
    report = classify_text(text)
    print(format_classify(report))
    window = args.window or "unknown"
    lock = score_g3(report, window=window)
    print("QWEN_G3=%s skipped=%s window=%s" % (lock["g3"], lock["skipped"], window))
    if window == "yes" and report.get("g2_live") and lock["g3"] == "yes":
        print("G3=yes")
    else:
        print("G3=no")
    st = load_state()
    _record_tip(st, args.sha, report, "score")
    st["tips"][args.sha[:8]]["window"] = window
    st["tips"][args.sha[:8]]["state"] = "scored"
    save_state(st)
    return 0


def cmd_debug(args: argparse.Namespace) -> int:
    dd = Path(args.dd) if args.dd else None
    r = debug_sha(args.sha, dd=dd, force=True)
    print(json.dumps({k: v for k, v in r.items() if k != "log"}))
    print("log=%s" % r.get("log"))
    return 0 if r.get("ok") else 2


def _find_log(sha: str) -> Optional[Path]:
    short = sha[:8]
    cands = [
        Path("/tmp/ss-pr10-%s.log" % short),
        Path("/tmp/ss-pr10-%s.log" % sha),
        repo_root() / "research-score" / ("ss-pr10-%s.log" % short),
        HERE / "fixtures" / "ss-pr10-2d295270.tail.txt",
    ]
    for p in cands:
        if p.is_file():
            return p
    return None


def cmd_once(args: argparse.Namespace) -> int:
    sha = args.sha.strip()
    tax = load_taxonomy()
    denylist = set(x.lower()[:8] for x in tax.get("denylist") or [])
    st = load_state()
    short = sha.lower()[:8]
    force = bool(args.force)

    log_path = Path(args.log) if args.log else _find_log(sha)
    need_debug = log_path is None
    if short in denylist and not force:
        need_debug = False
        if log_path is None:
            # still classify if we can find any log; else stop
            print("denylist sha=%s skip Debug" % short)
            if short == "e298371e":
                print("e298371e denylisted; do not use as tip")
                return 2
            # e25a61f1: classify existing research log if present
            alt = repo_root() / "research-score" / ("ss-pr10-%s.log" % short)
            if alt.is_file():
                log_path = alt
            else:
                log_path = HERE / "fixtures" / "ss-pr10-2d295270.tail.txt"

    if need_debug:
        r = debug_sha(sha, dd=Path(args.dd) if args.dd else None, force=force)
        if not r.get("ok"):
            print("FAIL_CLOSED=%s" % r.get("fail"))
            return 2
        log_path = Path(str(r["log"]))

    assert log_path is not None
    text = _read_log(log_path)
    report = classify_text(text)
    print(format_classify(report))
    window = args.window or "unknown"
    lock = score_g3(report, window=window)
    print("QWEN_G3=%s skipped=%s" % (lock["g3"], lock["skipped"]))
    if window == "yes" and report.get("g2_live") and lock["g3"] == "yes":
        print("G3=yes")
        _record_tip(st, sha, report, "g3-lock")
        save_state(st)
        return 0

    action = escalate_action(report, st, sha)
    print("ACTION=%s" % action)
    do_not = action == "stop-cap"
    if action in ("escalate", "widen", "stop-cap"):
        path = write_escalate(sha, report, do_not_mill_again=do_not)
        print("escalate=%s" % path)
        _record_tip(st, sha, report, action)
        st["tips"][sha[:8]]["state"] = "escalate_ready"
        if action == "widen":
            st.setdefault("widen", {})
            st["widen"].setdefault(short, {})
            live = report["LIVE_CLASS"]
            st["widen"][short][live] = int(st["widen"][short].get(live, 0)) + 1
        save_state(st)
        if action == "stop-cap":
            print("DO_NOT_MILL_AGAIN")
            return 0
        return 0
    if action == "stop-refuse":
        print("REFUSE_AS_WAIT LIVE_CLASS=%s" % report["LIVE_CLASS"])
        _record_tip(st, sha, report, "stop-refuse")
        save_state(st)
        return 0
    _record_tip(st, sha, report, "classify")
    save_state(st)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("classify")
    p.add_argument("--log", required=True)

    p = sub.add_parser("score")
    p.add_argument("--log", required=True)
    p.add_argument("--sha", required=True)
    p.add_argument("--window", default="unknown", choices=["yes", "no", "unknown"])

    p = sub.add_parser("debug")
    p.add_argument("--sha", required=True)
    p.add_argument("--dd", default=None)

    p = sub.add_parser("once")
    p.add_argument("--sha", required=True)
    p.add_argument("--log", default=None)
    p.add_argument("--dd", default=None)
    p.add_argument("--window", default="unknown", choices=["yes", "no", "unknown"])
    p.add_argument("--force", action="store_true")

    args = ap.parse_args()
    if args.cmd == "classify":
        return cmd_classify(args)
    if args.cmd == "score":
        return cmd_score(args)
    if args.cmd == "debug":
        return cmd_debug(args)
    if args.cmd == "once":
        return cmd_once(args)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
