#!/usr/bin/env python3
"""G3 mill-and-test: mill C++ toward OS 9.2.1, hang-cap after a mill.

Default command is `run`: no flags. Ctrl-C saves state; the next run continues.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import signal
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from classify import classify_text, escalate_action, format_classify, load_taxonomy
from debug_run import (
    debug_sha,
    hangcap_sec,
    hangcap_working_tree,
    mill_app,
    mill_binary_match,
    mill_dd_default,
    mill_spec_dd,
    promote_spec_dd,
    repo_root,
    use_runtime_68k,
    xcodebuild_mill,
)
from mill_apply import apply as mill_apply_class
from mill_apply import is_applied as mill_is_applied
from mill_apply import (
    OFF_68K,
    cpu_path,
    hang_off_millable,
    hang_rom_off,
    infer_saw_68k,
    keep_is_68k,
    last_millable_hang_off,
    force_skip_hang_off,
    next_skip_68k_off,
    patch_cpu_skip_68k,
    skip_68k_key,
    mill_moved,
    mill_worse,
    next_kind,
    next_leftover,
    next_skip_hang_off,
    skip_hang_key,
    stash_files,
    tested_keys,
)
from mill_apply import _log_for_n_simple
from mill_apply import revert as mill_revert
from mill_escalate import write_escalate
from grok_build import (
    grok_bin,
    grok_build_enabled,
    grok_max_calls,
    run_grok_build,
)
from mill_pack import append_attempt_pack_log, write_pack
from qwen_lock import add_usage, format_tokens, score_g3, zero_usage

_STOP = False


def _iso_now() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def _fmt_sec(sec: Optional[float]) -> str:
    if sec is None:
        return "-"
    s = float(sec)
    if s < 0:
        s = 0.0
    if s < 60:
        return "%.1fs" % s
    m, r = divmod(s, 60.0)
    if m < 60:
        return "%dm%.0fs" % (int(m), r)
    h, m = divmod(m, 60.0)
    return "%dh%dm" % (int(h), int(m))


def _session_avg(mill: Dict[str, Any]) -> Tuple[int, float, Optional[float]]:
    n = int(mill.get("session_n") or 0)
    sm = float(mill.get("session_sum") or 0.0)
    avg = (sm / n) if n else None
    return n, sm, avg


def _state_path() -> Path:
    return HERE / "state.json"


def load_state() -> Dict[str, Any]:
    p = _state_path()
    if not p.is_file():
        return {"denylist": [], "widen": {}, "tips": {}}
    return json.loads(p.read_text())


def save_state(st: Dict[str, Any]) -> None:
    mill = st.get("mill")
    if isinstance(mill, dict) and isinstance(mill.get("attempts"), list):
        att = mill["attempts"]
        if "keep_count" not in mill:
            mill["keep_count"] = sum(1 for a in att if a.get("result") == "KEEP")
            mill["revert_count"] = sum(1 for a in att if a.get("result") == "REVERT")
        mill["attempts"] = att[-20:]
    _state_path().write_text(json.dumps(st, separators=(",", ":")) + "\n")


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
    print(format_tokens("qwen", lock.get("usage"), "lock" + (" skipped" if lock.get("skipped") else "")))
    print(format_tokens("grok", zero_usage(), "mill canned"))
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
    """process | mill | hangcap | score-log | g3-done | skip-e298.

    Never wait for origin/g3. Refuse-as-wait still mills skip/execute, not wait-cmp.
    """
    short = sha.lower()[:8]
    if short == "e298371e":
        return "skip-e298"
    run = st.get("run") or {}
    if run.get("g3") == "yes":
        return "g3-done"
    mill = st.get("mill") or {}
    if mill.get("last_fail") and mill.get("n"):
        p = Path("/tmp/ss-g3-mill-%d.log" % int(mill["n"]))
        if p.is_file():
            return "score-log"
    if mill.get("pending_hangcap"):
        return "hangcap"
    tip = (st.get("tips") or {}).get(short) or {}
    if tip.get("action") == "g3-lock" or tip.get("state") == "g3-lock":
        return "g3-done"
    live = mill.get("live_class") or tip.get("class")
    if live:
        kind = next_kind(live, mill.get("tested"))
        if kind and mill_is_applied(live, kind=kind) and (
            ("%s:%s" % (live, kind)) not in tested_keys(mill.get("tested"), live)
        ):
            return "hangcap"
        return "mill"
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
    print(format_tokens("qwen", lock.get("usage"), "lock" + (" skipped" if lock.get("skipped") else "")))
    print(format_tokens("grok", zero_usage(), "mill canned"))
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
    if action in ("escalate", "widen", "stop-cap", "mill"):
        path = write_escalate(sha, report, do_not_mill_again=do_not)
        print("escalate=%s" % path)
        _record_tip(st, sha, report, action)
        st["tips"][sha[:8]]["state"] = "mill" if action == "mill" else "escalate_ready"
        st["tips"][sha[:8]]["escalate"] = str(path)
        if action == "widen":
            st.setdefault("widen", {})
            st["widen"].setdefault(short, {})
            live = report["LIVE_CLASS"]
            st["widen"][short][live] = int(st["widen"][short].get(live, 0)) + 1
        if action == "mill":
            st.setdefault("mill", {})
            st["mill"]["live_class"] = report["LIVE_CLASS"]
            print("MILL skip-pair LIVE_CLASS=%s (not wait-cmp)" % report["LIVE_CLASS"])
        save_state(st)
        if action == "stop-cap":
            print("DO_NOT_MILL_AGAIN")
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


def _classify_log(log_path: Path, window: str) -> Dict[str, Any]:
    text = _read_log(log_path)
    report = classify_text(text)
    print(format_classify(report))
    lock = score_g3(report, window=window)
    print("QWEN_G3=%s skipped=%s window=%s" % (lock["g3"], lock["skipped"], window))
    print(format_tokens("qwen", lock.get("usage"), "lock" + (" skipped" if lock.get("skipped") else "")))
    print(format_tokens("grok", zero_usage(), "mill canned"))
    report["_lock"] = lock
    if window == "yes" and report.get("g2_live") and lock["g3"] == "yes":
        print("G3=yes")
        report["_g3"] = "yes"
    else:
        print("G3=no")
        report["_g3"] = "no"
    return report


def _sum_who(attempts: Any, who: str) -> Dict[str, int]:
    tot = zero_usage()
    for a in attempts or []:
        tot = add_usage(tot, a.get(who))
    return tot


def format_attempts_table(attempts: Any) -> str:
    fmt = "%-4s %-12s %-8s %-7s %8s %8s %8s %8s %8s %8s %8s"
    rows = [
        fmt
        % (
            "n",
            "kind",
            "hang_off",
            "result",
            "elapsed",
            "grok_in",
            "grok_out",
            "grok_tot",
            "qwen_in",
            "qwen_out",
            "qwen_tot",
        )
    ]
    for a in attempts or []:
        off = a.get("hang_off")
        off_s = ("%x" % int(off)) if off is not None else "-"
        g = a.get("grok") or zero_usage()
        q = a.get("qwen") or zero_usage()
        rows.append(
            fmt
            % (
                str(a.get("n") or ""),
                str(a.get("kind") or "")[:12],
                off_s,
                str(a.get("result") or ""),
                _fmt_sec(a.get("elapsed_sec")),
                int(g.get("in") or 0),
                int(g.get("out") or 0),
                int(g.get("total") or 0),
                int(q.get("in") or 0),
                int(q.get("out") or 0),
                int(q.get("total") or 0),
            )
        )
    gtot = _sum_who(attempts, "grok")
    qtot = _sum_who(attempts, "qwen")
    rows.append(
        fmt
        % (
            "SUM",
            "",
            "",
            "",
            "",
            gtot["in"],
            gtot["out"],
            gtot["total"],
            qtot["in"],
            qtot["out"],
            qtot["total"],
        )
    )
    return "\n".join(rows)


def _record_attempt(
    st: Dict[str, Any],
    report: Dict[str, Any],
    live: str,
    kind: str,
    result: str,
) -> None:
    mill = st.setdefault("mill", {})
    lock = report.get("_lock") or {}
    qwen_u = lock.get("usage") or zero_usage()
    grok_u = zero_usage()
    if kind == "grok-escalate":
        grok_u = mill.pop("grok_last_usage", None) or zero_usage()
    off = mill.get("hang_off")
    t1 = time.time()
    t0 = mill.pop("attempt_t0", None)
    elapsed = (t1 - float(t0)) if t0 is not None else None
    if elapsed is not None:
        mill["session_n"] = int(mill.get("session_n") or 0) + 1
        mill["session_sum"] = float(mill.get("session_sum") or 0.0) + elapsed
    sess_n, _sess_sum, sess_avg = _session_avg(mill)
    attempt = {
        "n": int(mill.get("n") or 0),
        "kind": kind,
        "live": live,
        "hang_off": off,
        "result": result,
        "g3": report.get("_g3") or "no",
        "grok": dict(grok_u),
        "qwen": dict(qwen_u),
        "qwen_skipped": bool(lock.get("skipped")),
        "started_at": mill.get("attempt_started"),
        "ended_at": _iso_now(),
        "elapsed_sec": None if elapsed is None else round(elapsed, 1),
    }
    mill.pop("attempt_started", None)
    mill.setdefault("attempts", []).append(attempt)
    if result == "KEEP":
        mill["keep_count"] = int(mill.get("keep_count") or 0) + 1
    elif result == "REVERT":
        mill["revert_count"] = int(mill.get("revert_count") or 0) + 1
    tok = mill.setdefault("tokens", {"grok": zero_usage(), "qwen": zero_usage()})
    tok["grok"] = add_usage(tok.get("grok"), grok_u)
    tok["qwen"] = add_usage(tok.get("qwen"), qwen_u)
    print(
        "TIME mill=%s elapsed=%s avg=%s n=%s session_start=%s"
        % (
            attempt["n"],
            _fmt_sec(elapsed),
            _fmt_sec(sess_avg),
            sess_n,
            mill.get("session_started") or "-",
        )
    )
    print(
        "TOKENS mill=%s kind=%s hang_off=%s result=%s"
        % (attempt["n"], kind, ("%x" % int(off)) if off is not None else "-", result)
    )
    print(format_tokens("grok", grok_u, "mill canned"))
    role = "lock skipped" if lock.get("skipped") else "lock"
    print(format_tokens("qwen", qwen_u, role))
    gtot = mill["tokens"]["grok"]
    qtot = mill["tokens"]["qwen"]
    print("TOKENS sum grok in=%d out=%d total=%d" % (gtot["in"], gtot["out"], gtot["total"]))
    print("TOKENS sum qwen in=%d out=%d total=%d" % (qtot["in"], qtot["out"], qtot["total"]))
    try:
        append_attempt_pack_log(st, attempt)
    except OSError:
        pass
    mill["pack_n"] = int(mill.get("n") or 0)


def _record_mill_report(
    st: Dict[str, Any],
    report: Dict[str, Any],
    log_path: Path,
    live_class: Optional[str] = None,
) -> None:
    st.setdefault("mill", {})
    mill = st["mill"]
    mill["live_class"] = live_class or report.get("LIVE_CLASS")
    mill["last_log"] = str(log_path)
    mill["g2_live"] = bool(report.get("g2_live"))
    last_hb = report.get("last_hb") or {}
    mill["live_pc"] = last_hb.get("pc")
    mill["live_op"] = last_hb.get("op")
    mill["live_nxt"] = last_hb.get("nxt")
    if report.get("_g3") == "yes":
        st.setdefault("run", {})["g3"] = "yes"


def _tested_key(live: str, kind: str, hang_off: Optional[int] = None) -> str:
    if kind == "skip-hang" and hang_off is not None:
        return skip_hang_key(int(hang_off))
    if kind == "skip-68k" and hang_off is not None:
        return skip_68k_key(int(hang_off))
    return "%s:%s" % (live, kind)


def _hang_off_arg(kind: Optional[str], mill: Dict[str, Any]) -> Optional[int]:
    if kind in ("skip-hang", "skip-68k"):
        return mill.get("hang_off")
    return None


def _spec_next_skip_68k(mill: Dict[str, Any], current_off: Optional[int], spec: Dict[str, Any]) -> None:
    """Build skip-68k N+1 in mill-spec derived data. Does not touch mill stash."""
    tested = list(mill.get("tested") or [])
    if current_off is not None:
        key = skip_68k_key(int(current_off))
        if key not in tested:
            tested = tested + [key]
    nxt = next_skip_68k_off(mill, tested, mill.get("reverted_kinds"))
    if nxt is None:
        spec["ok"] = False
        return
    cpu = cpu_path()
    try:
        orig = cpu.read_text()
    except OSError:
        spec["ok"] = False
        return
    dd = mill_spec_dd()
    spec["off"] = int(nxt)
    spec["dd"] = str(dd)
    try:
        cpu.write_text(patch_cpu_skip_68k(orig, int(nxt)))
        rc = xcodebuild_mill(dd, clean=False, xc_log=Path("/tmp/ss-g3-mill-xcodebuild-spec.log"))
        app = mill_app(dd)
        spec["ok"] = rc == 0 and mill_binary_match(app, kind="skip-68k", hang_off=int(nxt))
        if not spec["ok"] and rc == 0:
            rc = xcodebuild_mill(dd, clean=True, xc_log=Path("/tmp/ss-g3-mill-xcodebuild-spec.log"))
            spec["ok"] = rc == 0 and mill_binary_match(
                mill_app(dd), kind="skip-68k", hang_off=int(nxt)
            )
    except Exception:
        spec["ok"] = False
    finally:
        try:
            cpu.write_text(orig)
        except OSError:
            pass


def _force_leftover_mill(mill: Dict[str, Any]) -> Tuple[str, int]:
    """Never idle via +2 walk. skip-hang 50326 only before 68k. Map empty -> canned / Grok Build."""
    mill["live_class"] = "leftover"
    mill["stuck"] = None
    saw_68k = bool(mill.get("saw_68k") or keep_is_68k(mill.get("keep_pc")))
    if saw_68k:
        mill["saw_68k"] = True
        off = next_skip_68k_off(mill, mill.get("tested"), mill.get("reverted_kinds"))
        if off is not None:
            mill["hang_off"] = off
            mill["kind"] = "skip-68k"
            return "skip-68k", int(off)
        kind = next_leftover(
            mill.get("tested"),
            mill.get("reverted_kinds"),
            hang_off=mill.get("hang_off"),
            saw_68k=True,
            mill=mill,
        )
        if kind == "skip-68k":
            mill["kind"] = "grok-escalate"
            mill["hang_off"] = None
            mill["stuck"] = "grok-escalate"
            return "grok-escalate", 0
        if kind in ("cfm-aa5a", "trap-68k", "reenter-68k"):
            mill["kind"] = kind
            mill["hang_off"] = None
            return kind, 0
        mill["kind"] = "grok-escalate"
        mill["hang_off"] = None
        mill["stuck"] = "grok-escalate"
        return "grok-escalate", 0
    off = force_skip_hang_off(
        mill.get("hang_off"),
        mill.get("tested"),
        mill.get("reverted_kinds"),
        current_off=mill.get("hang_off"),
    )
    mill["hang_off"] = off
    mill["kind"] = "skip-hang"
    return "skip-hang", int(off)


def _write_grok_build_pack(st: Dict[str, Any]) -> Path:
    path = write_pack(st, slim=True)
    mill = st.setdefault("mill", {})
    mill["pack_path"] = str(path)
    mill["pack_n"] = int(mill.get("n") or 0)
    print("PACK grok-build slim %s" % path)
    return path


def _begin_grok_escalate(st: Dict[str, Any]) -> Optional[int]:
    """Grok Build escalate. Return 0 to exit cmd_run, None to hang-cap/retry in-loop."""
    mill = st.setdefault("mill", {})
    mill["live_class"] = "leftover"
    mill["kind"] = "grok-escalate"
    calls = int(mill.get("grok_calls") or 0)
    cap = grok_max_calls()
    if mill.get("grok_waiting"):
        if calls >= cap:
            mill["stuck"] = "histogram-empty"
            mill["grok_waiting"] = False
            mill["pending_hangcap"] = False
            _write_grok_build_pack(st)
            save_state(st)
            print("stuck=histogram-empty grok_calls=%s. Mill C++ or Ctrl-C." % calls)
            return 0
        mill["grok_waiting"] = False
        mill["grok_calls"] = calls + 1
        mill["pending_hangcap"] = True
        mill["stuck"] = None
        print("Grok Build hang-cap working tree (call %s)" % mill["grok_calls"])
        save_state(st)
        return None
    path = _write_grok_build_pack(st)
    if grok_build_enabled() and grok_bin() is not None:
        stash_files()
        print("Grok Build headless grok -p slim=%s" % path)
        save_state(st)
        r = run_grok_build(path)
        mill["grok_last_rc"] = r.get("rc")
        mill["grok_last_reason"] = r.get("reason")
        tok = mill.setdefault("tokens", {"grok": zero_usage(), "qwen": zero_usage()})
        tok["grok"] = add_usage(tok.get("grok"), r.get("usage"))
        print(
            "Grok Build applied=%s ok=%s rc=%s reason=%s"
            % (r.get("applied"), r.get("ok"), r.get("rc"), r.get("reason"))
        )
        print(format_tokens("grok", r.get("usage"), "build"))
        if r.get("applied"):
            mill["grok_calls"] = calls + 1
            mill["grok_waiting"] = False
            mill["pending_hangcap"] = True
            mill["stuck"] = None
            mill["grok_fail_n"] = 0
            mill["grok_last_usage"] = r.get("usage")
            save_state(st)
            print("Grok Build mill in tree; hang-cap")
            return None
        mill["grok_fail_n"] = int(mill.get("grok_fail_n") or 0) + 1
        save_state(st)
        if mill["grok_fail_n"] >= 2:
            mill["stuck"] = "grok-build-fail"
            mill["grok_waiting"] = True
            mill["pending_hangcap"] = False
            save_state(st)
            print("stuck=grok-build-fail. See /tmp/ss-g3-grok-build.log")
            return 0
        print("Grok Build mill missing; retry leftover")
        return None
    mill["grok_waiting"] = True
    mill["stuck"] = "grok-escalate"
    mill["pending_hangcap"] = False
    print(
        "Grok Build: no grok binary. Analyze %s and mill C++. Then ./run."
        % path
    )
    save_state(st)
    return 0


def _mark_tested(
    mill: Dict[str, Any],
    live: str,
    kind: str,
    hang_off: Optional[int] = None,
) -> None:
    mill.setdefault("tested", [])
    key = _tested_key(live, kind, hang_off)
    if key not in mill["tested"]:
        mill["tested"].append(key)


def _score_hangcap_log(
    st: Dict[str, Any],
    log_path: Path,
    live: str,
    kind: str,
    window: str,
) -> Dict[str, Any]:
    mill = st.setdefault("mill", {})
    before = None
    base = mill.get("base_log") or mill.get("last_log")
    if base and Path(str(base)).is_file() and Path(str(base)).resolve() != log_path.resolve():
        before = classify_text(_read_log(Path(str(base))))
    print("classify log=%s" % log_path)
    report = _classify_log(log_path, window)
    worse = before is not None and mill_worse(
        before,
        report,
        keep_pc=mill.get("keep_pc"),
        saw_68k=bool(mill.get("saw_68k") or keep_is_68k(mill.get("keep_pc"))),
        ss_alive_sec=mill.get("ss_alive_sec"),
        window=window,
    )
    hang_off = mill.get("hang_off")
    _mark_tested(mill, live, kind, hang_off=hang_off)
    mill["last_fail"] = None
    mill["stuck"] = None
    mill["pending_hangcap"] = False
    mill["kind"] = kind
    if worse:
        print("REVERT mill LIVE_CLASS=%s kind=%s (worse; hang 04cecd36/G2/mill=)" % (live, kind))
        _record_attempt(st, report, live, kind, "REVERT")
        mill_revert()
        mill.setdefault("reverted_kinds", [])
        rk = _tested_key(live, kind, hang_off)
        if rk not in mill["reverted_kinds"]:
            mill["reverted_kinds"].append(rk)
        nxt = next_kind(live, mill.get("tested"))
        if nxt:
            print("next mill kind=%s (not wait-cmp, not ppc-mmu)" % nxt)
            mill["live_class"] = live
            mill["kind"] = nxt
            r = mill_apply_class(live, kind=nxt)
            print("MILL applied=%s ok=%s reason=%s kind=%s" % (
                r.get("applied"), r.get("ok"), r.get("reason"), nxt
            ))
            if r.get("ok"):
                mill["pending_hangcap"] = True
                mill.setdefault("applied", [])
                ak = "%s:%s" % (live, nxt)
                if ak not in mill["applied"]:
                    mill["applied"].append(ak)
        _record_mill_report(st, report, log_path, live_class=live)
        return report
    moved = before is None or mill_moved(before, report)
    print("KEEP mill LIVE_CLASS=%s kind=%s moved=%s now=%s" % (
        live, kind, moved, report.get("LIVE_CLASS")
    ))
    _record_attempt(st, report, live, kind, "KEEP")
    mill["base_log"] = str(log_path)
    mill["keep_log"] = str(log_path)
    mill["keep_hangcapped"] = True
    last_hb = report.get("last_hb") or {}
    if last_hb.get("pc") is not None:
        if keep_is_68k(last_hb.get("pc")):
            mill["saw_68k"] = True
        if not (keep_is_68k(mill.get("keep_pc")) and not keep_is_68k(last_hb.get("pc"))):
            mill["keep_pc"] = last_hb.get("pc")
    _record_mill_report(st, report, log_path)
    return report


def cmd_run(args: argparse.Namespace) -> int:
    global _STOP
    _STOP = False
    signal.signal(signal.SIGINT, _handle_stop)
    signal.signal(signal.SIGTERM, _handle_stop)
    window = args.window or os.environ.get("G3_WINDOW", "unknown")
    poll = int(os.environ.get("G3_POLL_SEC", "15"))
    st = load_state()
    st.setdefault("run", {})
    st.setdefault("mill", {})
    last = (st.get("run") or {}).get("last_sha")
    if last:
        print("resume last_sha=%s mill=%s" % (last, (st.get("mill") or {}).get("live_class")))
    mill0 = st["mill"]
    mill0["session_started"] = _iso_now()
    mill0["session_t0"] = time.time()
    mill0["session_n"] = 0
    mill0["session_sum"] = 0.0
    print("session start %s (attempt times reset)" % mill0["session_started"])
    save_state(st)
    print("mill-and-test until Ctrl-C. Hang-cap only after a mill. Resume from state.json.")
    while not _STOP:
        st = load_state()
        st.setdefault("run", {})
        st.setdefault("mill", {})
        mill = st["mill"]
        if mill.get("live_class") is None and (st.get("run") or {}).get("g3") != "yes":
            sha = current_g3_sha()
            if sha:
                st["run"]["last_sha"] = sha
                if sha[:8] == "e298371e":
                    print("skip e298371e as tip; mill working tree")
                    st["run"]["last_sha"] = "e25a61f1933ef8a385089d2bc7e11d9bf0f2901c"
                    mill["live_class"] = mill.get("live_class") or "leftover"
                    save_state(st)
                    continue
            else:
                tax = load_taxonomy()
                sha = (tax.get("working_tip") or "e25a61f1").lower()
                st["run"]["last_sha"] = sha
        sha = (st.get("run") or {}).get("last_sha") or current_g3_sha() or "e25a61f1"
        step = next_step(st, sha)
        st["run"]["step"] = step
        save_state(st)
        print("g3=%s step=%s live=%s" % (sha[:8], step, mill.get("live_class")))
        if step == "g3-done":
            print("G3=yes; nothing left")
            return 0
        if step == "skip-e298":
            print("skip e298371e as tip; mill working tree")
            mill["live_class"] = mill.get("live_class") or "leftover"
            st["run"]["last_sha"] = "e25a61f1933ef8a385089d2bc7e11d9bf0f2901c"
            save_state(st)
            continue

        if step == "process":
            log_path = None
            if mill.get("last_log"):
                p = Path(str(mill["last_log"]))
                if p.is_file():
                    log_path = p
            if log_path is None:
                log_path = _find_log(sha)
            if log_path is None:
                mill["live_class"] = mill.get("live_class") or "false-stw-spr"
                print("no log; mill %s first (no SheepShaver until mill)" % mill["live_class"])
                save_state(st)
                continue
            print("classify log=%s (no hang-cap)" % log_path)
            report = _classify_log(log_path, window)
            _record_mill_report(st, report, log_path)
            _record_tip(st, sha, report, "classify")
            save_state(st)
            if report.get("_g3") == "yes":
                print("G3=yes")
                return 0
            continue

        if step == "score-log":
            live = mill.get("live_class") or "false-stw-spr"
            kind = mill.get("kind") or "skip-pair"
            log_path = Path("/tmp/ss-g3-mill-%d.log" % int(mill.get("n") or 1))
            print("score ignored hang-cap log=%s (perl_exit is not a discard if heartbeat)" % log_path)
            report = _score_hangcap_log(st, log_path, live, kind, window)
            save_state(st)
            if report.get("_g3") == "yes":
                print("G3=yes")
                return 0
            continue

        if step == "mill":
            live = mill.get("live_class") or "false-stw-spr"
            if live == "wait-cmp-fwd-bc":
                print("LIVE_CLASS=wait-cmp-fwd-bc already complete; not remilling as skip")
            kind = next_kind(live, mill.get("tested"))
            keep_log = mill.get("keep_log")
            base_log = mill.get("base_log")
            if base_log and str(base_log).startswith("/tmp/ss-g3-mill-"):
                if not keep_log or "ss-pr10-" in str(keep_log):
                    keep_log = base_log
            if not keep_log:
                keep_log = "/tmp/ss-pr10-e25a61f1.log"
            hang_off = hang_rom_off(mill.get("keep_pc") or mill.get("live_pc"))
            if infer_saw_68k(mill):
                mill["saw_68k"] = True
                if not keep_is_68k(mill.get("keep_pc")):
                    for n68 in (35, 22):
                        p68 = _log_for_n_simple(n68)
                        if p68 is None:
                            continue
                        try:
                            tail = p68.read_text(errors="replace")[-12000:]
                        except OSError:
                            continue
                        if "pc=50366084" in tail:
                            mill["keep_log"] = str(p68)
                            mill["keep_pc"] = 0x50366084
                            keep_log = str(p68)
                            hang_off = OFF_68K
                            break
            if keep_is_68k(mill.get("keep_pc") or mill.get("live_pc")):
                hang_off = hang_rom_off(mill.get("keep_pc") or mill.get("live_pc"))
            elif mill.get("saw_68k"):
                pass
            elif not hang_off_millable(hang_off):
                hang_off = last_millable_hang_off(
                    keep_log, mill.get("tested"), mill.get("reverted_kinds")
                )
            if kind is None:
                lp = Path(str(keep_log))
                last = mill.get("last_log")
                # Do not reclassify a reverted mill log (mill-3 skip-mfsr).
                if last and last != keep_log and ("%s:%s" % (live, mill.get("kind") or "")) in (
                    mill.get("reverted_kinds") or []
                ):
                    mill["last_log"] = keep_log
                if lp.is_file() and kind is None:
                    report = classify_text(_read_log(lp))
                    print("KEEP log %s -> %s" % (lp, format_classify(report)))
                    last_hb = report.get("last_hb") or {}
                    if last_hb.get("pc") is not None and not hang_off_millable(
                        hang_rom_off(mill.get("keep_pc"))
                    ):
                        if not (
                            keep_is_68k(mill.get("keep_pc"))
                            and not keep_is_68k(last_hb.get("pc"))
                        ):
                            mill["keep_pc"] = last_hb.get("pc")
                        hang_off = hang_rom_off(mill.get("keep_pc"))
                        if keep_is_68k(mill.get("keep_pc")):
                            hang_off = hang_rom_off(mill.get("keep_pc"))
                        elif mill.get("saw_68k"):
                            pass
                        elif not hang_off_millable(hang_off):
                            hang_off = last_millable_hang_off(
                                keep_log, mill.get("tested"), mill.get("reverted_kinds")
                            )
                    kind = next_kind(live, mill.get("tested"))
            if kind is None:
                kind = next_leftover(
                    mill.get("tested"),
                    mill.get("reverted_kinds"),
                    hang_off=hang_off,
                    saw_68k=bool(mill.get("saw_68k")),
                    mill=mill,
                )
                if kind:
                    live = "leftover"
                    mill["live_class"] = live
                    if kind == "skip-hang":
                        hang_off = next_skip_hang_off(
                            hang_off, mill.get("tested"), mill.get("reverted_kinds")
                        )
                        mill["hang_off"] = hang_off
                        if hang_off is None:
                            kind = None
                    elif kind == "skip-68k":
                        hang_off = next_skip_68k_off(
                            mill, mill.get("tested"), mill.get("reverted_kinds")
                        )
                        mill["hang_off"] = hang_off
                        if hang_off is None:
                            kind = None
                    elif kind in ("cfm-aa5a", "trap-68k", "reenter-68k", "grok-escalate"):
                        mill["hang_off"] = None
                    elif kind in (
                        "keep-68k",
                        "read-noerr",
                        "setfpos-noerr",
                        "slot-26e90",
                        "skip-3265a4",
                        "spin-26e88",
                        "skip-326458",
                    ):
                        mill["hang_off"] = hang_off
                    if kind:
                        print(
                            "leftover mill kind=%s hang_off=%s"
                            % (kind, ("%x" % hang_off) if hang_off else None)
                        )
            if kind is None:
                kind, hang_off = _force_leftover_mill(mill)
                live = "leftover"
                print(
                    "leftover mill kind=%s hang_off=%s"
                    % (kind, ("%x" % hang_off) if hang_off else None)
                )
            if kind in (
                "skip-68k",
                "skip-hang",
                "cfm-aa5a",
                "trap-68k",
                "reenter-68k",
            ):
                mill["grok_waiting"] = False
            if kind == "grok-escalate":
                rc = _begin_grok_escalate(st)
                if rc is not None:
                    return rc
                continue
            tk = _tested_key(live, kind, _hang_off_arg(kind, mill))
            if tk in (mill.get("reverted_kinds") or []):
                print("kind %s already reverted; trying next" % tk)
                mill.setdefault("tested", []).append(tk)
                save_state(st)
                continue
            print("mill LIVE_CLASS=%s kind=%s file=ppc-cpu.cpp" % (live, kind))
            r = mill_apply_class(live, kind=kind, hang_off=_hang_off_arg(kind, mill))
            print("MILL applied=%s ok=%s reason=%s kind=%s" % (
                r.get("applied"), r.get("ok"), r.get("reason"), kind
            ))
            if not r.get("ok") and r.get("reason") not in ("already",):
                print("no canned mill for %s kind=%s; force leftover" % (live, kind))
                mill.setdefault("tested", []).append(tk)
                kind, hang_off = _force_leftover_mill(mill)
                live = "leftover"
                if kind == "grok-escalate":
                    rc = _begin_grok_escalate(st)
                    if rc is not None:
                        return rc
                    continue
                r = mill_apply_class(live, kind=kind, hang_off=hang_off)
                print("MILL applied=%s ok=%s reason=%s kind=%s hang_off=%s" % (
                    r.get("applied"), r.get("ok"), r.get("reason"), kind,
                    ("%x" % hang_off) if hang_off else None,
                ))
                if not r.get("ok") and r.get("reason") not in ("already",):
                    mill.setdefault("tested", []).append(
                        _tested_key(live, kind, hang_off)
                    )
                    save_state(st)
                    continue
                tk = _tested_key("leftover", kind, hang_off)
            if kind in ("skip-hang", "skip-68k") and mill.get("hang_off") is None:
                mill["hang_off"] = hang_off
            mill.setdefault("applied", [])
            if tk not in mill["applied"]:
                mill["applied"].append(tk)
            mill["kind"] = kind
            mill["pending_hangcap"] = True
            mill["stuck"] = None
            mill["attempt_t0"] = time.time()
            mill["attempt_started"] = _iso_now()
            print("TIME mill start n=%s at=%s" % (int(mill.get("n") or 0) + 1, mill["attempt_started"]))
            save_state(st)
            continue

        if step == "hangcap":
            live = mill.get("live_class") or "false-stw-spr"
            kind = mill.get("kind") or next_kind(live, mill.get("tested")) or "skip-pair"
            if kind == "keep" or not mill_is_applied(
                live, kind=kind, hang_off=_hang_off_arg(kind, mill)
            ):
                print("hang-cap skipped: mill kind=%s not in tree; mill leftover" % kind)
                mill["pending_hangcap"] = False
                kind, hang_off = _force_leftover_mill(mill)
                if kind == "grok-escalate":
                    rc = _begin_grok_escalate(st)
                    if rc is not None:
                        return rc
                    continue
                mill_apply_class("leftover", kind=kind, hang_off=hang_off)
                mill["pending_hangcap"] = True
                save_state(st)
                continue
            n = int(mill.get("n") or 0) + 1
            mill["n"] = n
            mill["kind"] = kind
            log_path = Path("/tmp/ss-g3-mill-%d.log" % n)
            cap = hangcap_sec()
            if mill.get("attempt_t0") is None:
                mill["attempt_t0"] = time.time()
                mill["attempt_started"] = _iso_now()
            print(
                "hang-cap working tree log=%s kind=%s (%ss) at=%s"
                % (log_path, kind, cap, _iso_now())
            )
            save_state(st)
            if _STOP:
                break
            spec: Dict[str, Any] = {"ok": False, "off": None, "dd": str(mill_spec_dd())}
            spec_th: Optional[threading.Thread] = None
            skip_build = False
            ready_off = mill.get("spec_ready_off")
            ready_dd = mill.get("spec_ready_dd")
            if (
                kind == "skip-68k"
                and ready_off is not None
                and mill.get("hang_off") is not None
                and int(ready_off) == int(mill.get("hang_off"))
                and ready_dd
                and mill_binary_match(
                    mill_app(Path(str(ready_dd))),
                    kind="skip-68k",
                    hang_off=mill.get("hang_off"),
                )
            ):
                if promote_spec_dd(Path(str(ready_dd)), mill_dd_default()):
                    skip_build = True
                    print(
                        "hang-cap skip-build spec off=%x"
                        % int(ready_off)
                    )
            mill["spec_ready_off"] = None
            mill["spec_ready_dd"] = None
            mill_snap = {
                "tested": list(mill.get("tested") or []),
                "reverted_kinds": list(mill.get("reverted_kinds") or []),
                "keep_log": mill.get("keep_log"),
                "keep_pc": mill.get("keep_pc"),
                "saw_68k": mill.get("saw_68k"),
            }

            def after_launch() -> None:
                nonlocal spec_th
                if kind != "skip-68k" or use_runtime_68k(kind):
                    return
                spec_th = threading.Thread(
                    target=_spec_next_skip_68k,
                    args=(mill_snap, mill.get("hang_off"), spec),
                    daemon=True,
                )
                spec_th.start()

            r = hangcap_working_tree(
                log_path=log_path,
                sec=cap,
                stop=lambda: _STOP,
                kind=kind,
                hang_off=mill.get("hang_off"),
                saw_68k=bool(mill.get("saw_68k") or keep_is_68k(mill.get("keep_pc"))),
                skip_build=skip_build,
                after_launch=after_launch,
            )
            if spec_th is not None:
                spec_th.join(timeout=180)
            if spec.get("ok") and spec.get("off") is not None:
                mill["spec_ready_off"] = int(spec["off"])
                mill["spec_ready_dd"] = spec.get("dd")
                print("spec ready skip-68k off=%x" % int(spec["off"]))
            mill["pending_hangcap"] = False
            mill["ss_alive_sec"] = r.get("ss_alive_sec")
            mill["ss_seen"] = r.get("ss_seen")
            if r.get("early_fail"):
                print("hang-cap early fail=%s (timeout interrupted)" % r["early_fail"])
            if r.get("early_stop"):
                print("hang-cap early stop=%s" % r["early_stop"])
            if not r.get("ok"):
                print("FAIL_CLOSED=%s" % r.get("fail"))
                mill["last_fail"] = r.get("fail") or r.get("early_fail")
                t0 = mill.get("attempt_t0")
                if t0 is not None:
                    print(
                        "TIME mill=%s elapsed=%s (fail) at=%s"
                        % (n, _fmt_sec(time.time() - float(t0)), _iso_now())
                    )
                if r.get("fail") not in (
                    "xcodebuild",
                    "open",
                    "missing-prefs",
                    "missing-rom",
                    "mill-mismatch",
                ) and log_path.is_file():
                    report = _score_hangcap_log(st, log_path, live, kind, window)
                    try:
                        shutil.copy2(log_path, HERE.parent / ("ss-g3-mill-%d.log" % n))
                    except OSError:
                        pass
                    save_state(st)
                    if report.get("_g3") == "yes":
                        print("G3=yes")
                        return 0
                    continue
                save_state(st)
                continue
            if r.get("perl_exit_warn") is not None:
                print("perl_exit=%s (log has heartbeat; scoring anyway)" % r.get("perl_exit_warn"))
            report = _score_hangcap_log(st, log_path, live, kind, window)
            copy = HERE.parent / ("ss-g3-mill-%d.log" % n)
            try:
                shutil.copy2(log_path, copy)
            except OSError:
                pass
            save_state(st)
            if report.get("_g3") == "yes":
                print("G3=yes")
                return 0
            continue

        print("unknown step=%s; mill leftover" % step)
        kind, hang_off = _force_leftover_mill(mill)
        if kind == "grok-escalate":
            rc = _begin_grok_escalate(st)
            if rc is not None:
                return rc
            continue
        mill_apply_class("leftover", kind=kind, hang_off=hang_off)
        mill["pending_hangcap"] = True
        save_state(st)
        continue
    print("stopped. state saved. run again to continue.")
    st = load_state()
    path = _write_grok_build_pack(st)
    save_state(st)
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

    sub.add_parser("tokens", help="print grok/qwen in/out/total per mill attempt")

    p = sub.add_parser("pack", help="write grok pack from hang-cap logs (Grok Build, no API)")
    p.add_argument("--out", default=None)
    p.add_argument("--slim", action="store_true", help="write pack-slim.md for Grok Build")

    p = sub.add_parser("rom", help="disassemble local prefs ROM for mill targeting (does not copy ROM)")
    p.add_argument("--rom", default=None)
    p.add_argument("--off", action="append", default=[])
    p.add_argument("--next", type=int, default=12)
    p.add_argument("--count", type=int, default=12)
    p.add_argument("--no-nk", action="store_true")

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
    if cmd == "tokens":
        return cmd_tokens()
    if cmd == "pack":
        return cmd_pack(args)
    if cmd == "rom":
        from rom_disasm import cmd_rom

        return cmd_rom(args)
    return 2


def cmd_pack(args: argparse.Namespace) -> int:
    st = load_state()
    dest = Path(args.out) if getattr(args, "out", None) else None
    slim = bool(getattr(args, "slim", False))
    path = write_pack(st, dest=dest, slim=slim)
    if slim:
        print("PACK grok-build slim %s" % path)
    else:
        print("PACK grok bulk %s" % path)
        print("PACK json %s" % path.with_suffix(".json"))
        print("PACK log %s" % path.with_name(path.stem + "-log.md"))
    print(format_tokens("grok", zero_usage(), "pack (not called)"))
    return 0


def cmd_tokens() -> int:
    st = load_state()
    mill = st.get("mill") or {}
    attempts = mill.get("attempts") or []
    print(format_attempts_table(attempts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
