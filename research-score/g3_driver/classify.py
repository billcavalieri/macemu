#!/usr/bin/env python3
"""Classify hang by LIVE_CLASS (last live pair), not cluster residue."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from parse_log import in_cluster_pc, parse_log

HERE = Path(__file__).resolve().parent


def load_taxonomy() -> Dict[str, Any]:
    return json.loads((HERE / "taxonomy.json").read_text())


def primary(op: int) -> int:
    return (op >> 26) & 0x3F


def xo(op: int) -> int:
    return (op >> 1) & 0x3FF


def bc_disp(op: int) -> int:
    bd = (op >> 2) & 0x3FFF
    if bd & 0x2000:
        bd -= 0x4000
    return bd << 2


def is_cmpi(op: int) -> bool:
    return primary(op) == 11


def is_cmp_x(op: int) -> bool:
    return primary(op) == 31 and xo(op) == 0


def is_rlwinm(op: int) -> bool:
    return primary(op) == 21


def is_cr_setter(op: int) -> bool:
    return is_cmpi(op) or is_cmp_x(op) or is_rlwinm(op)


def is_bc(op: int) -> bool:
    return primary(op) == 16


def is_addi(op: int) -> bool:
    return primary(op) == 14


def is_stw(op: int) -> bool:
    return primary(op) == 36


def is_mfsr(op: int) -> bool:
    return primary(op) == 31 and xo(op) == 595


def is_mfspr(op: int) -> bool:
    return primary(op) == 31 and xo(op) == 339


def is_mtspr(op: int) -> bool:
    return primary(op) == 31 and xo(op) == 467


def is_spr_xfer(op: int) -> bool:
    return is_mfsr(op) or is_mfspr(op) or is_mtspr(op)


def classify_pair(op: Optional[int], nxt: Optional[int]) -> str:
    """First match wins on the LIVE pair. Decode hex only."""
    if op is None:
        return "unknown-hb"
    if nxt is not None:
        if is_stw(op) and is_spr_xfer(nxt):
            return "false-stw-spr"
        if is_cmpi(op) and is_addi(nxt):
            return "false-cmp-li"
    if is_bc(op) and bc_disp(op) < 0:
        return "false-back-bc"
    if nxt is not None and is_cr_setter(op) and is_bc(nxt) and bc_disp(nxt) > 0:
        return "wait-cmp-fwd-bc"
    return "NEW"


def last_hb_class(last_hb: Optional[Dict[str, Any]]) -> str:
    if not last_hb:
        return "unknown-hb"
    if last_hb.get("op") is None:
        return "unknown-hb"
    return classify_pair(last_hb.get("op"), last_hb.get("nxt"))


def _freq_pair(pairs: List[Dict[str, Any]]) -> Optional[Tuple[int, Optional[int]]]:
    keyed = [
        (p["op"], p.get("nxt"))
        for p in pairs
        if p.get("op") is not None
    ]
    if not keyed:
        return None
    hist = Counter(keyed)
    maxc = max(hist.values())
    cands = [k for k, c in hist.items() if c == maxc]
    for op, nxt in cands:
        if classify_pair(op, nxt) == "wait-cmp-fwd-bc":
            return (op, nxt)
    # last occurring among max-count
    last = None
    for k in keyed:
        if k in cands:
            last = k
    return last


def live_class_from_parsed(parsed: Dict[str, Any]) -> str:
    pairs = parsed.get("post_leave_pairs") or []
    last_hb = parsed.get("last_hb")

    # Overrides win over opcode classes.
    if parsed.get("empty_300"):
        return "empty-vector"
    if parsed.get("dsi_on_store"):
        return "dsi-on-store"
    if parsed.get("msr_collapse"):
        return "msr-collapse"

    if last_hb and last_hb.get("op") is not None:
        return classify_pair(last_hb.get("op"), last_hb.get("nxt"))

    hb_pc = last_hb["pc"] if last_hb else None
    if hb_pc is not None:
        matching = [
            p
            for p in pairs
            if p.get("pc") == hb_pc and p.get("op") is not None
        ]
        if matching:
            p = matching[-1]
            return classify_pair(p.get("op"), p.get("nxt"))

    freq = _freq_pair(pairs)
    if freq is None:
        return "unknown-hb"
    return classify_pair(freq[0], freq[1])


def still_class(live: str) -> str:
    return live


def classify_text(text: str) -> Dict[str, Any]:
    parsed = parse_log(text)
    live = live_class_from_parsed(parsed)
    lhb = last_hb_class(parsed.get("last_hb"))
    tax = load_taxonomy()
    known = set(tax.get("known_complete") or [])
    refuse = set(tax.get("refuse_as_wait") or [])
    new = live == "NEW"
    return {
        "parsed": parsed,
        "LIVE_CLASS": live,
        "LAST_HB_CLASS": lhb,
        "STILL_CLASS": still_class(live),
        "NEW": new,
        "known_complete": live in known,
        "refuse_as_wait": live in refuse,
        "g2_live": parsed.get("g2_live"),
        "g2_hit_line": parsed.get("g2_hit_line"),
        "pic_idle": parsed.get("pic_idle"),
        "hang_04cecd36": parsed.get("hang_04cecd36"),
        "cluster": parsed.get("cluster"),
        "last_hb": parsed.get("last_hb"),
    }


def format_classify(report: Dict[str, Any]) -> str:
    new = "yes" if report.get("NEW") else "no"
    return (
        "LAST_HB_CLASS=%s LIVE_CLASS=%s STILL_CLASS=%s NEW=%s"
        % (
            report["LAST_HB_CLASS"],
            report["LIVE_CLASS"],
            report["STILL_CLASS"],
            new,
        )
    )


def escalate_action(report: Dict[str, Any], state: Dict[str, Any], tip_sha: str) -> str:
    """Return escalate | widen | stop-refuse | stop-cap | classify."""
    live = report["LIVE_CLASS"]
    tax = load_taxonomy()
    known = set(tax.get("known_complete") or [])
    refuse = set(tax.get("refuse_as_wait") or [])
    sha = (tip_sha or "").lower()[:8]

    if live in ("empty-vector", "dsi-on-store", "msr-collapse"):
        return "escalate"
    if live in refuse:
        return "stop-refuse"
    if live == "NEW":
        return "escalate"
    if live in known:
        last_hb = report.get("last_hb") or {}
        last_pair_is_live = False
        if last_hb.get("op") is not None:
            last_pair_is_live = classify_pair(last_hb.get("op"), last_hb.get("nxt")) == live
        # unknown-hb: LIVE_CLASS still known-complete -> still that hang
        if last_pair_is_live or last_hb.get("op") is None:
            widen = (state.get("widen") or {}).get(sha, {})
            n = int(widen.get(live, 0))
            if n >= int(tax.get("widen_cap_per_class_per_tip") or 1):
                return "stop-cap"
            return "widen"
    return "classify"
