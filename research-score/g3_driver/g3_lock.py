#!/usr/bin/env python3
"""Deterministic G3 lock. Skip-68k AI is NewWorldView Apple FM via mill-annotations.json."""
from __future__ import annotations

from typing import Any, Dict, Optional


def zero_usage() -> Dict[str, int]:
    return {"in": 0, "out": 0, "total": 0}


def format_tokens(
    who: str,
    usage: Optional[Dict[str, Any]] = None,
    role: str = "",
) -> str:
    u = usage or zero_usage()
    extra = (" (%s)" % role) if role else ""
    return "TOKENS %s in=%d out=%d total=%d%s" % (
        who,
        int(u.get("in") or 0),
        int(u.get("out") or 0),
        int(u.get("total") or 0),
        extra,
    )


def add_usage(a: Optional[Dict[str, Any]], b: Optional[Dict[str, Any]]) -> Dict[str, int]:
    aa = a or zero_usage()
    bb = b or zero_usage()
    return {
        "in": int(aa.get("in") or 0) + int(bb.get("in") or 0),
        "out": int(aa.get("out") or 0) + int(bb.get("out") or 0),
        "total": int(aa.get("total") or 0) + int(bb.get("total") or 0),
    }


def score_g3(
    report: Dict[str, Any],
    window: str = "unknown",
) -> Dict[str, Any]:
    """Return {g3, raw, skipped, usage, who}.

    G3 yes requires operator WINDOW=yes and live G2 HIT in the hang-cap log.
    No local LLM. ROM skip-68k classification comes from NewWorldView exports.
    """
    window = (window or "unknown").lower()
    if window not in ("yes", "no", "unknown"):
        window = "unknown"
    out: Dict[str, Any] = {
        "g3": "no",
        "window": window,
        "skipped": False,
        "raw": "",
        "usage": zero_usage(),
        "who": "operator",
    }
    if window != "yes":
        out["skipped"] = True
        out["g3"] = "no"
        out["raw"] = "WINDOW not yes (set G3_WINDOW=yes when installer window is visible)"
        return out
    if report.get("g2_live"):
        out["g3"] = "yes"
        out["raw"] = "WINDOW=yes and g2_live"
    else:
        out["g3"] = "no"
        out["raw"] = "WINDOW=yes but no g2_live in log"
    return out
