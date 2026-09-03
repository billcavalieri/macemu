#!/usr/bin/env python3
"""Call Grok Build headless (`grok -p` / --prompt-file). Not an xAI mill HTTP API."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional

HERE = Path(__file__).resolve().parent


def grok_build_enabled() -> bool:
    v = os.environ.get("G3_GROK_BUILD", "1").strip().lower()
    return v not in ("0", "no", "false")


def grok_bin() -> Optional[Path]:
    env = os.environ.get("G3_GROK_BIN", "").strip()
    if env:
        p = Path(env)
        if p.is_file() and os.access(p, os.X_OK):
            return p
    w = shutil.which("grok")
    if w:
        return Path(w)
    home = Path.home() / ".grok" / "bin" / "grok"
    if home.is_file() and os.access(home, os.X_OK):
        return home
    return None


def grok_max_turns() -> int:
    try:
        n = int(os.environ.get("G3_GROK_MAX_TURNS", "24"))
    except ValueError:
        n = 24
    return max(4, min(n, 80))


def grok_build_sec() -> int:
    try:
        n = int(os.environ.get("G3_GROK_BUILD_SEC", "600"))
    except ValueError:
        n = 600
    return max(60, min(n, 1800))


def grok_max_calls() -> int:
    try:
        n = int(os.environ.get("G3_GROK_MAX_CALLS", "32"))
    except ValueError:
        n = 32
    return max(1, min(n, 256))


def repo_root() -> Path:
    return HERE.parents[1]


def mill_stash_dir() -> Path:
    return Path("/tmp/g3-mill-stash")


def mill_tree_changed() -> bool:
    """True if working-tree mill files differ from /tmp/g3-mill-stash."""
    from mill_apply import MILL_FILES

    stash = mill_stash_dir()
    root = repo_root()
    if not (stash / "ok").is_file():
        return False
    for rel in MILL_FILES:
        a = stash / rel
        b = root / rel
        if not b.is_file():
            continue
        if not a.is_file():
            return True
        try:
            if a.read_bytes() != b.read_bytes():
                return True
        except OSError:
            return True
    return False


def write_grok_prompt(slim_path: Path, dest: Optional[Path] = None) -> Path:
    dest = dest or Path("/tmp/ss-g3-grok-prompt.md")
    dest.write_text(
        "\n".join(
            [
                "You are milling SheepShaver toward Mac OS 9.2.1 G3 in this repo.",
                "Grok Build headless mill. One mill only, then stop.",
                "",
                "Read %s and mill C++ from it." % slim_path,
                "",
                "Allowlist only:",
                "- SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp",
                "- research-score/g3_driver/mill_apply.py",
                "",
                "Do not launch SheepShaver. Do not run ./research-score/g3_driver/run.",
                "Do not pkill. Do not git commit. Do not commit ROM/disk.",
                "Do not skip-68k +2 ROM walk. Do not mill skip-hang 50326.",
                "Do not remill skip-pair / skip-mfsr / unstick-stw / spin-26e88 / skip-326458.",
                "Do not skip 0x3264fc / 0x326564 / 0x326568. Do not remill e298371e.",
                "Do not r24-divert off CODE 0 JT. Do not or-in EE. Do not mill ppc-mmu for 68fff0dc.",
                "Do not skip-68k GetCCursor/DialogDispatch/SetPort/DisposeDialog/OpenResFile/GetResource/InitCursor/GetEOF/GetFPos/Read.",
                "Do not skip-68k UI path 0x5c86c-0x5c8c0 or $a190 data 0x16de8-0x16e20.",
                "Do not remill look-again KEEP OpenResFile/GetResource/InitCursor/GetFPos/GetEOF.",
                "",
                "When the mill is in the tree, print exactly:",
                "MILL_APPLIED=yes KIND=...",
                "If you cannot mill, print:",
                "MILL_APPLIED=no REASON=...",
                "Then end the turn.",
                "",
            ]
        )
    )
    return dest


def grok_cmd(slim_path: Path, prompt_path: Path) -> List[str]:
    bin_p = grok_bin()
    if bin_p is None:
        raise FileNotFoundError("grok binary not found")
    return [
        str(bin_p),
        "--permission-mode",
        "bypassPermissions",
        "--output-format",
        "json",
        "--max-turns",
        str(grok_max_turns()),
        "--cwd",
        str(repo_root()),
        "--no-plan",
        "--disable-web-search",
        "--tools",
        "read_file,search_replace,grep,list_dir",
        "--prompt-file",
        str(prompt_path),
    ]


def _usage_from_grok_json(data: Dict[str, Any]) -> Dict[str, int]:
    u = data.get("usage") or {}
    pin = int(u.get("input_tokens") or 0)
    pout = int(u.get("output_tokens") or 0)
    tot = int(u.get("total_tokens") or 0)
    if tot == 0:
        tot = pin + pout
    return {"in": pin, "out": pout, "total": tot}


def _parse_grok_stdout(raw: str) -> Dict[str, Any]:
    text = raw
    usage = {"in": 0, "out": 0, "total": 0}
    s = raw.strip()
    if s.startswith("{"):
        try:
            data = json.loads(s)
            if isinstance(data, dict):
                text = str(data.get("text") or raw)
                usage = _usage_from_grok_json(data)
        except json.JSONDecodeError:
            pass
    applied = "mill_applied=yes" in text.lower()
    return {"text": text, "usage": usage, "applied_mark": applied}


def run_grok_build(slim_path: Path) -> Dict[str, Any]:
    """Run grok headless. Returns {ok, applied, rc, usage, reason, log}."""
    out: Dict[str, Any] = {
        "ok": False,
        "applied": False,
        "rc": None,
        "usage": {"in": 0, "out": 0, "total": 0},
        "reason": None,
        "log": "/tmp/ss-g3-grok-build.log",
        "text": "",
    }
    if not grok_build_enabled():
        out["reason"] = "disabled"
        return out
    if grok_bin() is None:
        out["reason"] = "no-grok-bin"
        return out
    slim_path = Path(slim_path)
    prompt_path = write_grok_prompt(slim_path)
    cmd = grok_cmd(slim_path, prompt_path)
    logp = Path(str(out["log"]))
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(repo_root()),
            capture_output=True,
            text=True,
            timeout=grok_build_sec(),
        )
    except subprocess.TimeoutExpired as e:
        out["reason"] = "timeout"
        out["rc"] = -1
        logp.write_text((e.stdout or "") + "\n" + (e.stderr or ""))
        return out
    except OSError as e:
        out["reason"] = str(e)
        return out
    raw = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    logp.write_text(raw)
    out["rc"] = proc.returncode
    parsed = _parse_grok_stdout(proc.stdout or "")
    out["text"] = parsed["text"]
    out["usage"] = parsed["usage"]
    changed = mill_tree_changed()
    marked = parsed["applied_mark"]
    out["applied"] = bool(changed or marked)
    out["ok"] = proc.returncode == 0 and out["applied"]
    if not out["applied"]:
        out["reason"] = out["reason"] or "no-mill"
    return out
