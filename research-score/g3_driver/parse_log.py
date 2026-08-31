#!/usr/bin/env python3
"""Parse SheepShaver Debug NW-BOOT logs. Decode hex only; tags are not evidence."""
from __future__ import annotations

import re
from typing import Any, Dict, List, Optional, Tuple

PC_LO = 0x50325000
PC_HI = 0x50327000

RE_HB = re.compile(
    r"NW-BOOT heartbeat pc=([0-9a-fA-F]+) msr=([0-9a-fA-F]+) same=(\d+)"
)
RE_PC = re.compile(r"\bpc=([0-9a-fA-F]+)")
RE_OP = re.compile(r"\bop=([0-9a-fA-F]+)")
RE_NXT = re.compile(r"\bnxt=([0-9a-fA-F]+)")
RE_MSR = re.compile(r"\bmsr=([0-9a-fA-F]+)")
RE_MILL = re.compile(r"\bmill=(\d+)")
RE_DSI_N = re.compile(r"DSI n=(\d+)")
RE_XLATE_MISS = re.compile(
    r"xlatehow=miss ea=([0-9a-fA-F]+)"
)


def _u(s: Optional[str]) -> Optional[int]:
    if s is None:
        return None
    return int(s, 16)


def in_cluster_pc(pc: Optional[int]) -> bool:
    if pc is None:
        return False
    return PC_LO <= pc < PC_HI


def _is_nw_g(line: str) -> bool:
    return "NW-BOOT G" in line or line.startswith("NW-BOOT heartbeat")


def parse_log(text: str) -> Dict[str, Any]:
    lines = text.splitlines()
    heartbeats: List[Dict[str, Any]] = []
    mill_pairs: List[Dict[str, Any]] = []
    mill_vals: List[int] = []
    g2_hit_line = None
    g2_live = False
    empty_300 = False
    second_dsi = False
    dsi_n_max = 0
    ee_forced = False
    pic_idle = False
    hang_04cecd36 = False
    miss_eas: List[int] = []
    handler_cut = -1
    last_dec_leave_idx = -1
    msr_pin_17efbb80 = False
    illegal_300 = False

    for i, raw in enumerate(lines):
        line = raw.rstrip("\n")
        for m in RE_MILL.finditer(line):
            mill_vals.append(int(m.group(1)))

        hm = RE_HB.search(line)
        if hm:
            heartbeats.append(
                {
                    "pc": int(hm.group(1), 16),
                    "msr": int(hm.group(2), 16),
                    "same": int(hm.group(3)),
                    "op": None,
                    "nxt": None,
                    "line": line,
                    "idx": i,
                }
            )

        if "picspin idle" in line:
            pic_idle = True
        if "hang 04cecd36" in line:
            hang_04cecd36 = True
        if "illegal pc=00000300" in line.lower() or "illegal pc=00000300" in line:
            illegal_300 = True
            empty_300 = True
        if "empty 0x300" in line or "empty 0x300" in line.lower():
            empty_300 = True
        if "plant DSI vector 0x300" in line and "op=00000000" in line:
            empty_300 = True
        if "17efbb80" in line and "pin" in line.lower():
            msr_pin_17efbb80 = True
        if "or-in EE" in line or "or in EE" in line:
            ee_forced = True

        dm = RE_DSI_N.search(line)
        if dm:
            dsi_n_max = max(dsi_n_max, int(dm.group(1)))
        xm = RE_XLATE_MISS.search(line)
        if xm:
            miss_eas.append(int(xm.group(1), 16))

        if "first DSI SRR0=PC DR on HIT" in line or "DR on HIT no second DSI" in line:
            g2_hit_line = line
            g2_live = True
        if re.search(r"G2: DSI SRR0=[0-9a-fA-F]+ DAR=", line):
            if g2_hit_line is None:
                g2_hit_line = line
            g2_live = True
        if "first DSI SRR0=" in line and "DRhit=1" in line:
            if g2_hit_line is None:
                g2_hit_line = line
            g2_live = True

        if "DEC handler left" in line or "DEC rfi restore" in line:
            handler_cut = i
        if "DEC leave" in line:
            pm = RE_PC.search(line)
            pc = _u(pm.group(1)) if pm else None
            if in_cluster_pc(pc):
                last_dec_leave_idx = i

        if "DEC leave" in line:
            pm = RE_PC.search(line)
            om = RE_OP.search(line)
            nm = RE_NXT.search(line)
            pc = _u(pm.group(1)) if pm else None
            op = _u(om.group(1)) if om else None
            nxt = _u(nm.group(1)) if nm else None
            if op is not None or nxt is not None:
                mill_pairs.append(
                    {
                        "pc": pc,
                        "op": op,
                        "nxt": nxt,
                        "line": line,
                        "idx": i,
                    }
                )

    second_dsi = dsi_n_max >= 2

    last_hb = heartbeats[-1] if heartbeats else None

    # Post-leave cut: after last handler-left/rfi, else all cluster mill pairs.
    if handler_cut >= 0:
        post = [p for p in mill_pairs if p["idx"] > handler_cut]
    else:
        post = [p for p in mill_pairs if in_cluster_pc(p.get("pc"))]

    cluster_pairs = []
    for p in post:
        if p.get("op") is None:
            continue
        if p.get("pc") is not None and not in_cluster_pc(p["pc"]):
            continue
        if in_cluster_pc(p.get("pc")) or p.get("pc") is None:
            if in_cluster_pc(p.get("pc")):
                cluster_pairs.append(p)

    dsi_on_store = False
    for ea in miss_eas:
        if ea != 0x10010002:
            dsi_on_store = True

    last_msr = last_hb["msr"] if last_hb else None
    msr_collapse = False
    if last_msr == 0:
        msr_collapse = True
    if msr_pin_17efbb80:
        msr_collapse = True

    if empty_300:
        g2_live = False

    last_80_g = [ln for ln in lines if _is_nw_g(ln)][-80:]

    return {
        "last_hb": last_hb,
        "pairs": mill_pairs,
        "post_leave_pairs": cluster_pairs,
        "cluster": cluster_pairs,
        "g2_hit_line": g2_hit_line,
        "g2_live": g2_live,
        "empty_300": empty_300 or illegal_300,
        "second_dsi": second_dsi,
        "mill_max": max(mill_vals) if mill_vals else 0,
        "ee_forced": ee_forced,
        "pic_idle": pic_idle,
        "hang_04cecd36": hang_04cecd36,
        "dsi_on_store": dsi_on_store,
        "msr_collapse": msr_collapse,
        "last_80_g": last_80_g,
        "handler_cut": handler_cut,
        "last_dec_leave_idx": last_dec_leave_idx,
        "window_heuristic": "unknown",
    }


def pin_g2_packet(parsed: Dict[str, Any]) -> List[str]:
    """Last 80 NW-BOOT G lines plus pinned G2 HIT if dropped."""
    lines = list(parsed.get("last_80_g") or [])
    hit = parsed.get("g2_hit_line")
    if hit and hit not in lines:
        lines = [hit] + lines
    return lines
