#!/usr/bin/env python3
"""Write a Grok Build class-mill prompt. This PR does not mill C++."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional

from classify import classify_pair

HERE = Path(__file__).resolve().parent

REFUSE_VERBATIM = [
    "cmp+li at 503256f4",
    "backward bne at 503264fc",
    "stw+mfsr at 50326564 900107d4 / 7c0604a6",
]


def unique_rows(pairs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    seen = set()
    rows = []
    for p in pairs or []:
        op = p.get("op")
        nxt = p.get("nxt")
        if op is None:
            continue
        key = (op, nxt)
        if key in seen:
            continue
        seen.add(key)
        cls = classify_pair(op, nxt)
        rows.append(
            {
                "op": "%08x" % op,
                "nxt": ("%08x" % nxt) if nxt is not None else None,
                "pc": ("%08x" % p["pc"]) if p.get("pc") is not None else None,
                "class": cls,
            }
        )
    return rows


def file_for_class(live: str) -> str:
    if live in ("empty-vector", "dsi-on-store"):
        return "ppc-mmu.cpp"
    if live == "msr-collapse":
        return "ppc-cpu.cpp"
    return "ppc-cpu.cpp"


def write_escalate(
    sha: str,
    report: Dict[str, Any],
    do_not_mill_again: bool = False,
    dest: Optional[Path] = None,
) -> Path:
    live = report.get("LIVE_CLASS") or "NEW"
    last_hb = report.get("last_hb") or {}
    rows = unique_rows(report.get("cluster") or report.get("parsed", {}).get("cluster") or [])
    path = dest or (HERE / ("escalate-%s.md" % sha[:8]))
    hb_pc = last_hb.get("pc")
    hb_msr = last_hb.get("msr")
    lines = []
    a = lines.append
    a("# G3 class mill prompt")
    a("")
    a("Base branch is **g3**, not arm64-jit, not a closed PR number.")
    a("Working tip was scored SHA `%s`." % sha[:8])
    a("")
    a("## LIVE_CLASS")
    a("")
    a("- LIVE_CLASS: `%s`" % live)
    a("- LAST_HB_CLASS: `%s`" % report.get("LAST_HB_CLASS"))
    a("- STILL_CLASS: `%s`" % report.get("STILL_CLASS"))
    a("- NEW: `%s`" % ("yes" if report.get("NEW") else "no"))
    a("- last-hb pc: `%s`" % (("%08x" % hb_pc) if hb_pc is not None else "none"))
    a("- last-hb msr: `%s`" % (("%08x" % hb_msr) if hb_msr is not None else "none"))
    a("- last scored SHA: `%s`" % sha[:8])
    a("- file to edit: `%s`" % file_for_class(live))
    a("")
    a("Name the class and mill one complete for the cluster.")
    a("")
    a("## Unique (op,nxt,class) rows in 50325/50326")
    a("")
    for r in rows:
        a(
            "- pc=%s op=%s nxt=%s class=%s"
            % (r["pc"], r["op"], r["nxt"], r["class"])
        )
    if not rows:
        a("- (none)")
    a("")
    a("## Refuse-as-wait (do not mill these as wait-cmp-fwd-bc)")
    a("")
    for s in REFUSE_VERBATIM:
        a("- %s" % s)
    a("")
    a("Do not treat 50326564 900107d4/7c0604a6 as a wait.")
    a("Do not or-in EE, do not smash r8/r0, do not fall through 503264fc.")
    if do_not_mill_again:
        a("")
        a("**DO_NOT_MILL_AGAIN** — widen cap hit for this LIVE_CLASS on this tip.")
    a("")
    a("Closed PRs #5–#10 are history. Mill C++ later off g3.")
    path.write_text("\n".join(lines) + "\n")
    return path
