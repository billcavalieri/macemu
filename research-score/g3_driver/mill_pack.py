#!/usr/bin/env python3
"""Pack hang-cap results for one Grok Build mill pass. No Grok API."""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Optional

from classify import classify_text
from mill_apply import HARD_SKIP_OFFS, leftover_map_remaining
from mill_escalate import REFUSE_VERBATIM
from parse_log import pin_g2_packet

HERE = Path(__file__).resolve().parent
KEEP_G = 16
REVERT_G = 8
MAX_NEXT = 3


def _hx(n: Optional[int]) -> str:
    if n is None:
        return "-"
    return "%x" % int(n)


def _log_for_n(n: int, mill: Dict[str, Any]) -> Optional[Path]:
    p = Path("/tmp/ss-g3-mill-%d.log" % int(n))
    if p.is_file():
        return p
    copy = HERE.parent / ("ss-g3-mill-%d.log" % int(n))
    if copy.is_file():
        return copy
    keep = mill.get("keep_log") or mill.get("base_log")
    if keep and Path(str(keep)).is_file() and Path(str(keep)).name == p.name:
        return Path(str(keep))
    return None


def _summarize_log(path: Optional[Path]) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "log": str(path) if path else None,
        "LIVE_CLASS": None,
        "last_hb_pc": None,
        "last_hb_msr": None,
        "g2_live": None,
        "hang_04cecd36": None,
        "g_lines": [],
        "hang04_line": None,
        "skip_line": None,
    }
    if path is None or not path.is_file():
        return out
    text = path.read_text(errors="replace")
    report = classify_text(text)
    hb = report.get("last_hb") or {}
    out["LIVE_CLASS"] = report.get("LIVE_CLASS")
    out["last_hb_pc"] = hb.get("pc")
    out["last_hb_msr"] = hb.get("msr")
    out["g2_live"] = bool(report.get("g2_live"))
    out["hang_04cecd36"] = bool(report.get("hang_04cecd36"))
    parsed = report.get("parsed") or {}
    g = pin_g2_packet(parsed)
    out["g_lines"] = g
    for line in text.splitlines():
        if out["hang04_line"] is None and "hang 04cecd36" in line:
            out["hang04_line"] = line
        if out["skip_line"] is None and (
            "KEEP hang skip" in line
            or "stw+mfsr skip" in line
            or "poison callback skip" in line
        ):
            out["skip_line"] = line
    return out


def _off_from_skip(skip_line: Optional[str]) -> Optional[int]:
    if not skip_line:
        return None
    m = re.search(r"off=([0-9a-fA-F]+)", skip_line)
    if m:
        return int(m.group(1), 16)
    m = re.search(r"pc=([0-9a-fA-F]+)", skip_line)
    if m:
        pc = int(m.group(1), 16)
        if pc >= 0x50000000:
            return pc - 0x50000000
        return pc
    return None


def _synth_from_log(n: int, mill: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    path = _log_for_n(n, mill)
    if path is None:
        return None
    summ = _summarize_log(path)
    hang04 = bool(summ.get("hang_04cecd36"))
    g2 = summ.get("g2_live")
    result = "REVERT" if hang04 or g2 is False else "KEEP"
    hang_off = _off_from_skip(summ.get("skip_line"))
    if hang_off is None and summ.get("last_hb_pc"):
        pc = int(summ["last_hb_pc"])
        hang_off = pc - 0x50000000 if pc >= 0x50000000 else pc
    return {
        "n": n,
        "kind": "skip-hang" if summ.get("skip_line") and "hang skip" in (summ.get("skip_line") or "") else None,
        "hang_off": hang_off,
        "result": result,
        "g3": "no",
        "qwen": {},
        "grok": {},
        "_synth": True,
    }


def _night_attempts(mill: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Every hang-cap this night: attempts[] plus mill-N logs not yet recorded."""
    by_n: Dict[int, Dict[str, Any]] = {}
    for a in mill.get("attempts") or []:
        n = int(a.get("n") or 0)
        if n:
            by_n[n] = a
    n_max = int(mill.get("n") or 0)
    if not n_max and by_n:
        n_max = max(by_n)
    for n in range(1, n_max + 1):
        if n in by_n:
            continue
        syn = _synth_from_log(n, mill)
        if syn:
            by_n[n] = syn
    return [by_n[n] for n in sorted(by_n)]


def _attempt_row(a: Dict[str, Any], mill: Dict[str, Any]) -> Dict[str, Any]:
    n = int(a.get("n") or 0)
    summ = _summarize_log(_log_for_n(n, mill))
    result = a.get("result")
    g_lines = summ.get("g_lines") or []
    if result == "REVERT":
        g_keep = g_lines[-REVERT_G:]
    else:
        g_keep = g_lines[-KEEP_G:]
    q = a.get("qwen") or {}
    g = a.get("grok") or {}
    return {
        "n": n,
        "kind": a.get("kind"),
        "hang_off": a.get("hang_off"),
        "result": result,
        "g3": a.get("g3"),
        "LIVE_CLASS": summ.get("LIVE_CLASS"),
        "last_hb_pc": summ.get("last_hb_pc"),
        "last_hb_msr": summ.get("last_hb_msr"),
        "g2_live": summ.get("g2_live"),
        "hang_04cecd36": summ.get("hang_04cecd36"),
        "skip_line": summ.get("skip_line"),
        "hang04_line": summ.get("hang04_line"),
        "g_tail": g_keep,
        "qwen_in": int(q.get("in") or 0),
        "qwen_out": int(q.get("out") or 0),
        "qwen_total": int(q.get("total") or 0),
        "grok_in": int(g.get("in") or 0),
        "grok_out": int(g.get("out") or 0),
        "grok_total": int(g.get("total") or 0),
        "elapsed_sec": a.get("elapsed_sec"),
        "started_at": a.get("started_at"),
        "ended_at": a.get("ended_at"),
    }


def pack_from_state(st: Dict[str, Any]) -> Dict[str, Any]:
    mill = st.get("mill") or {}
    keep_log = mill.get("keep_log") or mill.get("base_log")
    keep = _summarize_log(Path(str(keep_log)) if keep_log else None)
    keep["g_lines"] = (keep.get("g_lines") or [])[-KEEP_G:]
    raw = _night_attempts(mill)
    attempts = [_attempt_row(a, mill) for a in raw]
    tokens = mill.get("tokens") or {}
    return {
        "goal": "G3 is 9.2.1 installer WINDOW and live G2 HIT. Do not claim G3.",
        "job": [
            "You are milling SheepShaver toward Mac OS 9.2.1 G3. This pack is the whole run.",
            "G0-G2 are locked. G3 needs installer WINDOW and live G2 HIT. Overlay/Balloon Help/SDL2 present n=1 is not G3.",
            "Do not only analyze. Mill C++: add canned kinds in research-score/g3_driver/mill_apply.py and the ppc-cpu.cpp patch they apply.",
            "This is a Grok Build turn, not an API call. Then the user runs ./research-score/g3_driver/run to hang-cap. Do not launch SheepShaver yourself unless asked.",
            "KEEP/REVERT already decided: hang 04cecd36 or G2 loss is worse. Do not remill reverted kinds or hang_offs.",
            "LIVE_CLASS=wait-cmp-fwd-bc with unknown-hb is not a wait mill. 50326564 900107d4/7c0604a6 is stw+mfsr, not wait-cmp.",
            "Do not remill e298371e. Do not merge arm64-jit. Do not commit ROM/disk. Do not mill ppc-mmu for 68fff0dc.",
            "Host window/cursor/close/click probe already KEEP. Do not break open .app launch or QuitEmulator on close.",
            "Qwen is G3 lock only. Do not ask Qwen to pick the mill.",
            "After mill-22/35 68k pc=50366084, mill leftover skip-68k from the r24 map until the map is empty, then canned successor / Grok Build escalate. Do not mill skip-hang 50326 while saw_68k. Do not +2 walk ROM. Never idle except Grok Build escalate, Ctrl-C, or G3.",
        ],
        "file": "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp",
        "hard_skip_offs": ["%x" % o for o in sorted(HARD_SKIP_OFFS)],
        "refuse_as_wait": list(REFUSE_VERBATIM),
        "do_not": [
            "skip 0x3264fc",
            "skip-list 50325",
            "remill reverted hang_off / skip-pair / skip-mfsr / unstick-stw",
            "r24 divert off CODE 0 JT",
            "or-in EE",
            "mill ppc-mmu for 68fff0dc",
            "skip-68k UI path 0x5c86c-0x5c8c0 (GetCCursor/DialogDispatch/SetPort/DisposeDialog)",
            "skip-68k A-lines GetCCursor/DialogDispatch/SetPort/DisposeDialog/CloseRgn/OpenResFile/GetResource/InitCursor/GetEOF/GetFPos/Read/SetFPos",
            "remill look-again KEEP OpenResFile/GetResource/InitCursor/GetFPos/GetEOF",
            "skip-68k $a190 data 0x16de8-0x16e20",
        ],
        "tested": list(mill.get("tested") or []),
        "reverted": list(mill.get("reverted_kinds") or []),
        "keep_log": keep_log,
        "keep_pc": mill.get("keep_pc"),
        "keep": keep,
        "attempts": attempts,
        "tokens": tokens,
        "reply": [
            "Up to %d mills, in test order. Do not remill reverted." % MAX_NEXT,
            "MILL 1: KIND=... HANG_OFF=... FILE=ppc-cpu.cpp REASON=...",
            "MILL 2: KIND=... HANG_OFF=... FILE=ppc-cpu.cpp REASON=...",
            "MILL 3: KIND=... HANG_OFF=... FILE=ppc-cpu.cpp REASON=...",
        ],
    }


def format_pack_md(pack: Dict[str, Any]) -> str:
    a = []
    a.append("# G3 grok pack (bulk after hang-caps)")
    a.append("")
    a.append(str(pack.get("goal")))
    ns = [r.get("n") for r in (pack.get("attempts") or []) if r.get("n")]
    span = "%s..%s" % (min(ns), max(ns)) if ns else "-"
    a.append(
        "Pack: %s hang-caps (mill %s). Full series from this run, not last mill only. "
        "Script does not call Grok. Up to %d next mills from this pack."
        % (len(pack.get("attempts") or []), span, MAX_NEXT)
    )
    a.append("")
    a.append("## Job (new session: do this)")
    a.append("")
    for s in pack.get("job") or []:
        a.append("- %s" % s)
    a.append("")
    a.append("## HARD")
    a.append("")
    a.append("- file: `%s`" % pack.get("file"))
    for o in pack.get("hard_skip_offs") or []:
        a.append("- do not skip `0x%s`" % o)
    for s in pack.get("do_not") or []:
        a.append("- do not %s" % s)
    a.append("")
    a.append("## Refuse-as-wait")
    a.append("")
    for s in pack.get("refuse_as_wait") or []:
        a.append("- %s" % s)
    a.append("")
    a.append("## KEEP baseline")
    a.append("")
    keep = pack.get("keep") or {}
    a.append("- keep_log: `%s`" % (pack.get("keep_log") or "-"))
    a.append("- keep_pc: `%s`" % _hx(pack.get("keep_pc")))
    a.append("- last_hb pc=`%s` msr=`%s`" % (_hx(keep.get("last_hb_pc")), _hx(keep.get("last_hb_msr"))))
    a.append("- LIVE_CLASS: `%s`" % (keep.get("LIVE_CLASS") or "-"))
    a.append("- g2_live: `%s` hang_04cecd36: `%s`" % (keep.get("g2_live"), keep.get("hang_04cecd36")))
    a.append("")
    a.append("## Tested / reverted")
    a.append("")
    a.append("- tested: `%s`" % ", ".join(pack.get("tested") or []) or "-")
    a.append("- reverted: `%s`" % ", ".join(pack.get("reverted") or []) or "-")
    a.append("")
    a.append("## Attempts")
    a.append("")
    a.append("| n | kind | hang_off | result | elapsed | last_hb | hang04 | g2 | qwen_tot |")
    a.append("|---|------|----------|--------|---------|---------|--------|----|----------|")
    for row in pack.get("attempts") or []:
        el = row.get("elapsed_sec")
        el_s = "-" if el is None else "%.1fs" % float(el)
        a.append(
            "| %s | %s | %s | %s | %s | %s | %s | %s | %s |"
            % (
                row.get("n"),
                row.get("kind"),
                _hx(row.get("hang_off")),
                row.get("result"),
                el_s,
                _hx(row.get("last_hb_pc")),
                row.get("hang_04cecd36"),
                row.get("g2_live"),
                row.get("qwen_total"),
            )
        )
    tok = pack.get("tokens") or {}
    q = tok.get("qwen") or {}
    g = tok.get("grok") or {}
    a.append("")
    a.append(
        "TOKENS sum grok in=%s out=%s total=%s"
        % (g.get("in") or 0, g.get("out") or 0, g.get("total") or 0)
    )
    a.append(
        "TOKENS sum qwen in=%s out=%s total=%s"
        % (q.get("in") or 0, q.get("out") or 0, q.get("total") or 0)
    )
    a.append("")
    a.append("## KEEP last G lines")
    a.append("")
    a.append("```")
    for line in keep.get("g_lines") or []:
        a.append(line)
    a.append("```")
    a.append("")
    a.append("## Every mill")
    a.append("")
    rows = pack.get("attempts") or []
    if not rows:
        a.append("(none)")
    for row in rows:
        a.extend(_mill_section(row))
    a.append("## Reply")
    a.append("")
    for s in pack.get("reply") or []:
        a.append("- %s" % s)
    a.append("")
    return "\n".join(a)


def _mill_section(row: Dict[str, Any]) -> list:
    lines = [
        "### mill-%s %s hang_off=%s last_hb=%s"
        % (
            row.get("n"),
            row.get("result") or "-",
            _hx(row.get("hang_off")),
            _hx(row.get("last_hb_pc")),
        ),
        "",
        "- kind=`%s` LIVE_CLASS=`%s` g2=`%s` hang04=`%s`"
        % (
            row.get("kind"),
            row.get("LIVE_CLASS"),
            row.get("g2_live"),
            row.get("hang_04cecd36"),
        ),
    ]
    if row.get("hang04_line"):
        lines.append("- `%s`" % row["hang04_line"])
    if row.get("skip_line"):
        lines.append("- `%s`" % row["skip_line"])
    lines.append("```")
    for line in row.get("g_tail") or []:
        lines.append(line)
    lines.append("```")
    lines.append("")
    return lines


def append_pack_log(pack: Dict[str, Any], dest: Path) -> None:
    """Append-only mill sections. pack.md stays a full rewrite."""
    existing = dest.read_text(errors="replace") if dest.is_file() else ""
    extra = []
    for row in pack.get("attempts") or []:
        n = row.get("n")
        mark = "### mill-%s " % n
        if mark in existing:
            continue
        extra.extend(_mill_section(row))
    if not extra:
        return
    if not existing:
        extra = ["# G3 pack log (append-only)\n", ""] + extra
    dest.write_text(existing + "\n".join(extra) + ("\n" if extra else ""))


def write_pack(
    st: Dict[str, Any],
    dest: Optional[Path] = None,
    slim: bool = False,
) -> Path:
    if slim:
        dest = Path(dest) if dest else (HERE / "pack-slim.md")
        dest.write_text(format_pack_slim_md(st))
        return dest
    pack = pack_from_state(st)
    dest = dest or (HERE / "pack.md")
    dest = Path(dest)
    dest.write_text(format_pack_md(pack))
    js = dest.with_suffix(".json")
    js.write_text(json.dumps(pack, indent=2) + "\n")
    append_pack_log(pack, dest.with_name(dest.stem + "-log.md"))
    return dest


def append_attempt_pack_log(st: Dict[str, Any], attempt: Dict[str, Any]) -> None:
    mill = st.get("mill") or {}
    row = _attempt_row(attempt, mill)
    append_pack_log({"attempts": [row]}, HERE / "pack-log.md")


def format_pack_slim_md(st: Dict[str, Any]) -> str:
    mill = st.get("mill") or {}
    keep_log = mill.get("keep_log") or mill.get("base_log")
    keep = _summarize_log(Path(str(keep_log)) if keep_log else None)
    g_keep = []
    for line in keep.get("g_lines") or []:
        if "68k map r24=" in line or "68k spin r24=" in line:
            continue
        g_keep.append(line)
    g_keep = g_keep[-KEEP_G:]
    attempts = list(mill.get("attempts") or [])[-20:]
    rows = [_attempt_row(a, mill) for a in attempts]
    remain, prefixes, n_remain = leftover_map_remaining(
        mill, mill.get("tested"), mill.get("reverted_kinds"), limit=20
    )
    keep_n = int(mill.get("keep_count") or 0)
    revert_n = int(mill.get("revert_count") or 0)
    n = int(mill.get("n") or 0)
    g2_lines = [ln for ln in (keep.get("g_lines") or []) if "first DSI" in ln and "DRhit=1" in ln]
    a = []
    a.append("# G3 grok pack (slim, Grok Build escalate)")
    a.append("")
    a.append("G3 is 9.2.1 installer WINDOW and live G2 HIT. Do not claim G3.")
    a.append("One mill. Grok Build in this chat, not an API. Do not launch SheepShaver unless asked.")
    a.append("Histogram skip-68k empty or escalate. Do not emit skip-68k +2. Do not mill skip-hang 50326 while saw_68k.")
    a.append("")
    a.append("## Job")
    a.append("")
    a.append("- Mill C++ on allowlist: ppc-cpu.cpp and/or research-score/g3_driver/mill_apply.py.")
    a.append("- Then the user runs ./research-score/g3_driver/run to hang-cap.")
    a.append("- KEEP/REVERT already decided: hang04 or G2 loss or 68k-loss is worse. Do not remill reverted.")
    a.append("- Qwen is G3 lock only. Do not ask Qwen to pick the mill.")
    a.append("")
    a.append("## HARD")
    a.append("")
    a.append("- file: `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp`")
    for o in sorted(HARD_SKIP_OFFS):
        a.append("- do not skip `0x%x`" % o)
    for s in (
        "skip 0x3264fc",
        "skip-list 50325",
        "remill reverted hang_off / skip-pair / skip-mfsr / unstick-stw",
        "r24 divert off CODE 0 JT",
        "or-in EE",
        "mill ppc-mmu for 68fff0dc",
        "remill e298371e",
        "commit ROM/disk",
        "skip-68k UI path 0x5c86c-0x5c8c0 (GetCCursor/DialogDispatch/SetPort/DisposeDialog)",
        "skip-68k A-lines GetCCursor/DialogDispatch/SetPort/DisposeDialog/CloseRgn/OpenResFile/GetResource/InitCursor/GetEOF/GetFPos/Read/SetFPos",
        "remill look-again KEEP OpenResFile/GetResource/InitCursor/GetFPos/GetEOF",
        "skip-68k $a190 data 0x16de8-0x16e20",
    ):
        a.append("- do not %s" % s)
    a.append("")
    a.append("## Refuse-as-wait")
    a.append("")
    for s in REFUSE_VERBATIM:
        a.append("- %s" % s)
    a.append("")
    a.append("## KEEP baseline")
    a.append("")
    a.append("- keep_n: `%s` keep_pc: `%s`" % (mill.get("n") or "-", _hx(mill.get("keep_pc"))))
    a.append("- keep_log: `%s`" % (keep_log or "-"))
    a.append("- last_hb pc=`%s` msr=`%s`" % (_hx(keep.get("last_hb_pc")), _hx(keep.get("last_hb_msr"))))
    a.append("- LIVE_CLASS: `%s`" % (keep.get("LIVE_CLASS") or "-"))
    a.append("- g2_live: `%s` hang_04cecd36: `%s`" % (keep.get("g2_live"), keep.get("hang_04cecd36")))
    a.append("- skip-68k KEEP=`%s` REVERT=`%s` n=`%s`" % (keep_n, revert_n, n))
    a.append("- map remaining: `%s` (4096 unique cap, not whole ROM)" % n_remain)
    a.append("")
    a.append("## Attempts (last 20)")
    a.append("")
    a.append("| n | kind | hang_off | result | last_hb | hang04 | g2 |")
    a.append("|---|------|----------|--------|---------|--------|----|")
    for row in rows:
        a.append(
            "| %s | %s | %s | %s | %s | %s | %s |"
            % (
                row.get("n"),
                row.get("kind"),
                _hx(row.get("hang_off")),
                row.get("result"),
                _hx(row.get("last_hb_pc")),
                row.get("hang_04cecd36"),
                row.get("g2_live"),
            )
        )
    if not rows:
        a.append("| - | - | - | - | - | - | - |")
    a.append("")
    a.append("## Map remaining")
    a.append("")
    a.append("- remaining=`%s`" % n_remain)
    if prefixes:
        buckets = " ".join("%s:%s" % (k, prefixes[k]) for k in sorted(prefixes)[:16])
        a.append("- prefixes: `%s`" % buckets)
    if remain:
        a.append("- next: `%s`" % ", ".join("%x" % o for o in remain))
    else:
        a.append("- next: (empty)")
    a.append("")
    a.append("## Pinned G2")
    a.append("")
    a.append("```")
    if g2_lines:
        a.append(g2_lines[-1])
    else:
        a.append("(none)")
    a.append("```")
    a.append("")
    a.append("## KEEP last G lines")
    a.append("")
    a.append("```")
    for line in g_keep:
        a.append(line)
    if not g_keep:
        a.append("(none)")
    a.append("```")
    a.append("")
    a.append("## Last 3 mills")
    a.append("")
    last3 = rows[-3:]
    if not last3:
        a.append("(none)")
    for row in last3:
        gtail = list(row.get("g_tail") or [])[-REVERT_G:]
        row = dict(row)
        row["g_tail"] = gtail
        a.extend(_mill_section(row))
    a.append("## Reply")
    a.append("")
    a.append("- One mill. Do not remill reverted. Do not skip-68k +2.")
    a.append("- MILL 1: KIND=... FILE=ppc-cpu.cpp")
    a.append("- REASON: ...")
    a.append("- unified diff on allowlisted files only")
    a.append("")
    return "\n".join(a)
