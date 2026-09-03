#!/usr/bin/env python3
"""Qwen is a G3 lock only. It does not pick the next file, mill PC, or class."""
from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional

from parse_log import pin_g2_packet

HERE = Path(__file__).resolve().parent
QWEN_URL = os.environ.get("G3_QWEN_URL", "http://127.0.0.1:8080/v1/chat/completions")
QWEN_MODEL = os.environ.get(
    "G3_QWEN_MODEL", "mlx-community/Qwen3.5-9B-MLX-4bit"
)
QWEN_KEY = os.environ.get("G3_QWEN_KEY", "local")

SYSTEM = (
    "G3 scorer only. Ignore NEXT_FILE. Do not choose a file or PC. "
    "Answer G3 yes, G3 no, or UNKNOWN. "
    "G3 accept needs installer WINDOW and live G2 HIT. "
    "Host SDL2 present n=1 is not WINDOW."
)


def _plan_text() -> str:
    env = os.environ.get("G3_PLAN_EXTRACT")
    cands = []
    if env:
        cands.append(Path(env))
    cands.append(HERE.parent / "PLAN-extract-qwen.txt")
    for cand in cands:
        if cand.is_file():
            return cand.read_text(errors="replace")[:4000]
    return (
        "G3 is the 9.2.1 installer window AND Debug NW-BOOT still shows G2 HIT. "
        "A window without that G2 line is not G3."
    )


def qwen_available() -> bool:
    models = QWEN_URL.replace("/chat/completions", "/models")
    try:
        req = urllib.request.Request(models, headers={"Authorization": "Bearer " + QWEN_KEY})
        urllib.request.urlopen(req, timeout=2).read(64)
        return True
    except Exception:
        return False


def zero_usage() -> Dict[str, int]:
    return {"in": 0, "out": 0, "total": 0}


def usage_from_response(data: Optional[Dict[str, Any]]) -> Dict[str, int]:
    u = (data or {}).get("usage") or {}
    pin = int(u.get("prompt_tokens") or 0)
    pout = int(u.get("completion_tokens") or 0)
    tot = int(u.get("total_tokens") or 0)
    if tot == 0:
        tot = pin + pout
    return {"in": pin, "out": pout, "total": tot}


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
    """Return {g3, raw, skipped, usage}. skipped if Qwen down or WINDOW is not yes."""
    window = (window or "unknown").lower()
    if window not in ("yes", "no", "unknown"):
        window = "unknown"
    out = {
        "g3": "no",
        "window": window,
        "skipped": False,
        "raw": "",
        "usage": zero_usage(),
        "who": "qwen",
    }
    if window != "yes":
        out["skipped"] = True
        out["g3"] = "no"
        return out
    log_lines = pin_g2_packet(report.get("parsed") or report)
    log_txt = "\n".join(log_lines[-80:])
    if not qwen_available():
        out["skipped"] = True
        if window == "yes" and report.get("g2_live"):
            out["g3"] = "unknown"
        else:
            out["g3"] = "no"
        return out
    user = (
        "PLAN:\n%s\n\nLOG:\n%s\n\nWINDOW:\n%s\n"
        % (_plan_text(), log_txt, window)
    )
    body = {
        "model": QWEN_MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": user},
        ],
        "max_tokens": 250,
        "temperature": 0,
    }
    req = urllib.request.Request(
        QWEN_URL,
        data=json.dumps(body).encode(),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + QWEN_KEY,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = json.loads(resp.read().decode())
        raw = data["choices"][0]["message"]["content"]
        out["usage"] = usage_from_response(data)
    except (urllib.error.URLError, KeyError, IndexError, json.JSONDecodeError, OSError) as e:
        out["skipped"] = True
        out["raw"] = str(e)
        out["g3"] = "no"
        return out
    out["raw"] = raw
    low = raw.lower()
    qwen_yes = "g3 yes" in low or (low.strip().startswith("yes") and "no" not in low[:20])
    qwen_unknown = "unknown" in low
    # Qwen YES or UNKNOWN with WINDOW unknown => operator G3 NO.
    if window == "unknown" and (qwen_yes or qwen_unknown):
        out["g3"] = "no"
    elif window == "yes" and report.get("g2_live") and qwen_yes:
        out["g3"] = "yes"
    else:
        out["g3"] = "no"
    return out
