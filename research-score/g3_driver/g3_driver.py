#!/usr/bin/env python3
"""G3 driver: classify NW-BOOT hangs by opcode class. No SheepShaver C++ mill.

Default command is `run`: no flags. Ctrl-C saves state; the next run continues.
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from classify import classify_text, escalate_action, format_classify, load_taxonomy
from debug_run import debug_sha, repo_root
from mill_escalate import write_escalate
from qwen_lock import score_g3

_STOP = False
DONE_STATES = (
    "escalate_ready",
    "scored",
    "stop-refuse",
    "stop-cap",
    "g3-lock",
    "fail-closed",
)


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
    short = sha[:8].lower()
    cands = [
        Path("/tmp/ss-pr10-%s.log" % short),
        Path("/tmp/ss-pr10-%s.log" % sha),
        repo_root() / "research-score" / ("ss-pr10-%s.log" % short),
        repo_root() / "research-score" / ("ss-pr10-%s.log" % sha),
    ]
    if short == "2d295270":
        cands.append(HERE / "fixtures" / "ss-pr10-2d295270.tail.txt")
    for p in cands:
        if p.is_file():
            return p
    return None


def current_g3_sha() -> Optional[str]:
    root = repo_root()
    subprocess.call(
        ["git", "fetch", "origin", "g3"],
        cwd=str(root),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "origin/g3"],
            cwd=str(root),
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip().lower()
    except subprocess.CalledProcessError:
        tax = load_taxonomy()
        return (tax.get("working_tip") or "").lower() or None


def next_step(st: Dict[str, Any], sha: str) -> str:
    """process | wait | g3-done | skip-e298."""
    short = sha.lower()[:8]
    if short == "e298371e":
        return "skip-e298"
    run = st.get("run") or {}
    if run.get("g3") == "yes":
        return "g3-done"
    tip = (st.get("tips") or {}).get(short) or {}
    if tip.get("action") == "g3-lock" or tip.get("state") == "g3-lock":
        return "g3-done"
    if tip.get("state") in DONE_STATES:
        return "wait"
    return "process"


def process_sha(
    sha: str,
    log: Optional[str] = None,
    dd: Optional[str] = None,
    window: str = "unknown",
    force: bool = False,
) -> int:
    sha = sha.strip()
    tax = load_taxonomy()
    denylist = set(x.lower()[:8] for x in tax.get("denylist") or [])
    st = load_state()
    short = sha.lower()[:8]
    if short == "e298371e":
        print("e298371e denylisted; do not use as tip")
        return 2

    log_path = Path(log) if log else _find_log(sha)
    need_debug = log_path is None
    if short in denylist and not force:
        need_debug = False
        print("denylist sha=%s skip Debug" % short)
        if log_path is None:
            print("no log for %s; not launching SheepShaver" % short)
            st.setdefault("tips", {})
            st["tips"][short] = {
                "tip_sha": sha,
                "state": "fail-closed",
                "action": "missing-log",
                "class": None,
            }
            save_state(st)
            return 2

    if need_debug:
        print("debug %s (SheepShaver hang-cap)" % short)
        r = debug_sha(sha, dd=Path(dd) if dd else None, force=force)
        if not r.get("ok"):
            print("FAIL_CLOSED=%s" % r.get("fail"))
            _record_tip(st, sha, {
                "LIVE_CLASS": None,
                "last_hb": {},
                "g2_live": False,
            }, "fail-closed")
            st["tips"][short]["state"] = "fail-closed"
            st["tips"][short]["fail"] = r.get("fail")
            save_state(st)
            return 2
        log_path = Path(str(r["log"]))

    assert log_path is not None
    print("classify log=%s" % log_path)
    text = _read_log(log_path)
    report = classify_text(text)
    print(format_classify(report))
    lock = score_g3(report, window=window)
    print("QWEN_G3=%s skipped=%s" % (lock["g3"], lock["skipped"]))
    if window == "yes" and report.get("g2_live") and lock["g3"] == "yes":
        print("G3=yes")
        _record_tip(st, sha, report, "g3-lock")
        st["tips"][short]["state"] = "g3-lock"
        st.setdefault("run", {})["g3"] = "yes"
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
        st["tips"][sha[:8]]["escalate"] = str(path)
        if action == "widen":
            st.setdefault("widen", {})
            st["widen"].setdefault(short, {})
            live = report["LIVE_CLASS"]
            st["widen"][short][live] = int(st["widen"][short].get(live, 0)) + 1
        save_state(st)
        if action == "stop-cap":
            print("DO_NOT_MILL_AGAIN")
        return 0
    if action == "stop-refuse":
        print("REFUSE_AS_WAIT LIVE_CLASS=%s" % report["LIVE_CLASS"])
        _record_tip(st, sha, report, "stop-refuse")
        st["tips"][short]["state"] = "stop-refuse"
        save_state(st)
        return 0
    _record_tip(st, sha, report, "classify")
    save_state(st)
    return 0


def cmd_once(args: argparse.Namespace) -> int:
    return process_sha(
        args.sha,
        log=args.log,
        dd=args.dd,
        window=args.window or "unknown",
        force=bool(args.force),
    )


def _handle_stop(signum: int, frame: Any) -> None:
    global _STOP
    _STOP = True
    print("\nCtrl-C: stopping after this step; next run resumes from state.json")


def cmd_run(args: argparse.Namespace) -> int:
    global _STOP
    _STOP = False
    signal.signal(signal.SIGINT, _handle_stop)
    signal.signal(signal.SIGTERM, _handle_stop)
    window = args.window or os.environ.get("G3_WINDOW", "unknown")
    poll = int(os.environ.get("G3_POLL_SEC", "30"))
    st = load_state()
    st.setdefault("run", {})
    last = (st.get("run") or {}).get("last_sha")
    if last:
        print("resume last_sha=%s" % last)
    save_state(st)
    print("run until Ctrl-C. SheepShaver only when a new tip needs a hang-cap.")
    while not _STOP:
        sha = current_g3_sha()
        if not sha:
            print("no origin/g3; wait")
            _sleep_poll(poll)
            continue
        short = sha[:8]
        step = next_step(st, sha)
        st = load_state()
        st.setdefault("run", {})
        st["run"]["last_sha"] = sha
        st["run"]["step"] = step
        save_state(st)
        print("g3=%s step=%s" % (short, step))
        if step == "g3-done":
            print("G3=yes; nothing left")
            return 0
        if step == "skip-e298":
            print("skip e298371e")
            _sleep_poll(poll)
            continue
        if step == "wait":
            tip = (st.get("tips") or {}).get(short) or {}
            print(
                "already %s class=%s; waiting for origin/g3 to move"
                % (tip.get("state"), tip.get("class"))
            )
            esc = tip.get("escalate")
            if esc:
                print("escalate=%s" % esc)
            _sleep_poll(poll)
            continue
        rc = process_sha(
            sha,
            window=window,
            force=bool(getattr(args, "force", False)),
        )
        st = load_state()
        if rc != 0:
            print("step rc=%s; will retry after wait" % rc)
            _sleep_poll(poll)
            continue
        if (st.get("run") or {}).get("g3") == "yes":
            print("G3=yes")
            return 0
    print("stopped. state saved. run again to continue.")
    return 0


def _sleep_poll(sec: int) -> None:
    n = max(1, int(sec))
    for _ in range(n):
        if _STOP:
            return
        time.sleep(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=False)

    p = sub.add_parser("run", help="default: continue until Ctrl-C")
    p.add_argument("--window", default=None, choices=["yes", "no", "unknown"])
    p.add_argument("--force", action="store_true")

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
    cmd = args.cmd or "run"
    if cmd == "run":
        if not hasattr(args, "window"):
            args.window = None
        if not hasattr(args, "force"):
            args.force = False
        return cmd_run(args)
    if cmd == "classify":
        return cmd_classify(args)
    if cmd == "score":
        return cmd_score(args)
    if cmd == "debug":
        return cmd_debug(args)
    if cmd == "once":
        return cmd_once(args)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
