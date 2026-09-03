#!/usr/bin/env python3
"""Parse SheepShaver Debug NW-BOOT logs. Decode hex only; tags are not evidence."""
from __future__ import annotations

import os
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

    # xlatehow=miss is not LIVE_CLASS. PIC probe 10010002 is G2, not a store DSI.
    dsi_on_store = False

    last_msr = last_hb["msr"] if last_hb else None
    msr_collapse = False
    if last_msr == 0:
        msr_collapse = True
    if msr_pin_17efbb80:
        msr_collapse = True

    if empty_300:
        g2_live = False

    last_80_g = [ln for ln in lines if _is_nw_g(ln)][-80:]
    reached_68k = any(h.get("pc") == 0x50366084 for h in heartbeats) or (
        "pc=50366084" in text
    )

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
        "reached_68k": reached_68k,
    }


def hangcap_early_fail(text: str, saw_68k: bool = False) -> Optional[str]:
    """Worse-mill signals that can stop the hang-cap wait. KEEP walks are not a fail.

    68k-loss: only after G2 HIT *and* this log reached pc=50366084, then last
    heartbeat is 50325/50326. Do not abort on 50326 before 68k (KEEP mill-1116
    walks ~1200 50326 heartbeats first).
    """
    if not text:
        return None
    if "hang 04cecd36" in text:
        return "hang_04cecd36"
    low = text.lower()
    if "empty 0x300" in low or "illegal pc=00000300" in low:
        return "empty_300"
    if "or-in EE" in text or "or in EE" in text:
        return "ee_forced"
    mill_max = 0
    for m in RE_MILL.finditer(text):
        mill_max = max(mill_max, int(m.group(1)))
    if mill_max:
        return "mill"
    if saw_68k and "pc=50366084" in text:
        g2 = "first DSI" in text and "DRhit=1" in text
        last = None
        for hm in RE_HB.finditer(text):
            last = int(hm.group(1), 16)
        if g2 and last is not None:
            off = last - 0x50000000 if last >= 0x50000000 else last
            if 0x325000 <= off < 0x327000:
                return "68k_loss"
    return None


def hangcap_g0_stuck(text: str, elapsed_sec: float, limit: float = 15.0) -> bool:
    """DecodeROM then silence: no G1, no G2, no heartbeat. mill-4230/4235."""
    if elapsed_sec < float(limit):
        return False
    if not text or "NW-BOOT G0:" not in text:
        return False
    if "G1: HardwareInit" in text:
        return False
    if "heartbeat pc=" in text:
        return False
    if "first DSI" in text:
        return False
    return True


def keep_stable_n() -> int:
    try:
        n = int(os.environ.get("G3_KEEP_STABLE_N", "8"))
    except ValueError:
        n = 8
    return max(2, min(n, 60))


def hangcap_keep_stable(text: str, n: Optional[int] = None) -> bool:
    """True when this log has 68k pc=50366084 and the last n heartbeats are that PC.

    Does not fire on 50325/50326. mill-1116 50326-then-68k is not stable until
    the tail is 50366084.
    """
    if not text or "pc=50366084" not in text:
        return False
    need = keep_stable_n() if n is None else max(2, int(n))
    pcs: List[int] = []
    for hm in RE_HB.finditer(text):
        pcs.append(int(hm.group(1), 16))
    if len(pcs) < need:
        return False
    return all(p == 0x50366084 for p in pcs[-need:])


def pin_g2_packet(parsed: Dict[str, Any]) -> List[str]:
    """Last 80 NW-BOOT G lines plus pinned G2 HIT if dropped."""
    lines = list(parsed.get("last_80_g") or [])
    hit = parsed.get("g2_hit_line")
    if hit and hit not in lines:
        lines = [hit] + lines
    return lines
