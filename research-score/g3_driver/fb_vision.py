#!/usr/bin/env python3
"""Grok Build CLI WINDOW yes/no from hang-cap PNGs.

Dedicated operator script. Not nested from ./run. Does not mill C++,
does not launch SheepShaver, does not hash PGM dumps (that is hang-cap).

  python3 research-score/g3_driver/fb_vision.py --png /tmp/ss-g3-mill-N.png
  python3 research-score/g3_driver/g3_driver.py vision --n N
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Union

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from debug_run import hangcap_media_paths
from grok_build import _parse_grok_stdout, grok_bin, repo_root

VISION_SOCK = "/tmp/ss-g3-grok-vision.sock"
VISION_LOG = Path("/tmp/ss-g3-grok-vision.log")
VISION_PROMPT_JSON = Path("/tmp/ss-g3-grok-vision-prompt.json")
WINDOW_PROMPT = """This is the SheepShaver guest framebuffer (SDL the_buffer), converted from the hang-cap PGM dump. It is not a host screenshot. Covering windows and display-sleep do not affect it.

Is the Mac OS 9.2.1 installer WINDOW visible?
Overlay, Balloon Help, a lone rectangle, or SDL2 present n=1 is NOT the installer.
Do not claim Finder.

Print exactly one line:
WINDOW=yes
or
WINDOW=no
Then end the turn. Do not mill. Do not edit files. Do not launch SheepShaver.
Do not run ./research-score/g3_driver/run.
"""


def grok_vision_sec() -> int:
    try:
        n = int(os.environ.get("G3_GROK_VISION_SEC", "120"))
    except ValueError:
        n = 120
    return max(30, min(n, 600))


def mime_for_path(path: Path) -> str:
    ext = path.suffix.lower()
    if ext in (".jpg", ".jpeg"):
        return "image/jpeg"
    if ext == ".gif":
        return "image/gif"
    if ext == ".webp":
        return "image/webp"
    return "image/png"


def image_block(path: Path) -> Dict[str, str]:
    path = Path(path)
    data = base64.b64encode(path.read_bytes()).decode("ascii")
    return {
        "type": "image",
        "mimeType": mime_for_path(path),
        "data": data,
    }


def text_block(text: str) -> Dict[str, str]:
    return {"type": "text", "text": text}


def acp_content(pngs: Sequence[Path], keep_png: Optional[Path] = None) -> List[Dict[str, str]]:
    blocks: List[Dict[str, str]] = [text_block(WINDOW_PROMPT)]
    labeled: List[str] = []
    for i, p in enumerate(pngs, start=1):
        blocks.append(image_block(Path(p)))
        labeled.append("image %d is hang-cap window %s" % (i, p))
    if keep_png is not None:
        blocks.append(image_block(Path(keep_png)))
        labeled.append(
            "image %d is the KEEP window %s" % (len(pngs) + 1, keep_png)
        )
    if labeled:
        blocks.insert(1, text_block("Labels: " + "; ".join(labeled) + "."))
    return blocks


def acp_prompt_json(
    pngs: Sequence[Path],
    keep_png: Optional[Path] = None,
) -> str:
    """ACP prompt for grok --prompt-json: {type:acp, content:[text, image...]}."""
    return json.dumps(
        {"type": "acp", "content": acp_content(pngs, keep_png=keep_png)},
        separators=(",", ":"),
    )


def grok_vision_leader_socket() -> str:
    return os.environ.get("G3_GROK_VISION_SOCK", VISION_SOCK).strip() or VISION_SOCK


def grok_vision_cmd(prompt_json: str) -> List[str]:
    bin_p = grok_bin()
    if bin_p is None:
        raise FileNotFoundError("grok binary not found")
    # Unique leader socket: must not attach to this chat or mill escalate.
    return [
        str(bin_p),
        "--permission-mode",
        "bypassPermissions",
        "--output-format",
        "json",
        "--max-turns",
        "1",
        "--cwd",
        str(repo_root()),
        "--no-plan",
        "--no-subagents",
        "--disable-web-search",
        "--no-alt-screen",
        "--leader-socket",
        grok_vision_leader_socket(),
        "--session-id",
        str(uuid.uuid4()),
        "--prompt-json",
        prompt_json,
    ]


def parse_window(text: str) -> str:
    s = (text or "").strip()
    if s.startswith("{"):
        try:
            data = json.loads(s)
        except json.JSONDecodeError:
            data = None
        if isinstance(data, dict):
            w = str(data.get("window") or data.get("WINDOW") or "").lower()
            if w in ("yes", "no"):
                return w
            inner = str(data.get("text") or "")
            if inner and inner != s:
                return parse_window(inner)
    low = (text or "").lower()
    yes = low.rfind("window=yes")
    no = low.rfind("window=no")
    if yes < 0 and no < 0:
        return "unknown"
    if yes > no:
        return "yes"
    return "no"


def resolve_pngs(
    pngs: Optional[Sequence[Union[str, Path]]] = None,
    keep_png: Optional[Union[str, Path]] = None,
    n: Optional[int] = None,
    log: Optional[Union[str, Path]] = None,
    mill_n: Optional[int] = None,
) -> Dict[str, Any]:
    """Pick hang-cap PNGs. keep_png is optional side-by-side, not hashed."""
    out: List[Path] = []
    for p in pngs or []:
        out.append(Path(p))
    if n is not None:
        out.append(Path("/tmp/ss-g3-mill-%d.png" % int(n)))
    elif log:
        out.append(hangcap_media_paths(Path(str(log)))["png"])
    elif not out and mill_n is not None:
        out.append(Path("/tmp/ss-g3-mill-%d.png" % int(mill_n)))
    keep = Path(keep_png) if keep_png else None
    return {"pngs": out, "keep_png": keep}


def run_vision(
    pngs: Union[Sequence[Path], Dict[str, Any], None] = None,
    keep_png: Optional[Path] = None,
) -> Dict[str, Any]:
    out: Dict[str, Any] = {
        "ok": False,
        "window": "unknown",
        "rc": None,
        "usage": {"in": 0, "out": 0, "total": 0},
        "reason": None,
        "log": str(VISION_LOG),
        "text": "",
    }
    if isinstance(pngs, dict):
        keep_png = pngs.get("keep_png") or keep_png
        pngs = pngs.get("pngs") or []
    paths = [Path(p) for p in (pngs or [])]
    keep_path = Path(keep_png) if keep_png else None
    missing = [str(p) for p in paths if not p.is_file()]
    if keep_path is not None and not keep_path.is_file():
        missing.append(str(keep_path))
    if not paths:
        out["reason"] = "no-png"
        return out
    if missing:
        out["reason"] = "missing-png"
        out["text"] = "missing: %s" % " ".join(missing)
        return out
    if grok_bin() is None:
        out["reason"] = "no-grok-bin"
        return out
    prompt = acp_prompt_json(paths, keep_png=keep_path)
    VISION_PROMPT_JSON.write_text(prompt)
    cmd = grok_vision_cmd(prompt)
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(repo_root()),
            capture_output=True,
            text=True,
            timeout=grok_vision_sec(),
        )
    except subprocess.TimeoutExpired as e:
        out["reason"] = "timeout"
        out["rc"] = -1
        VISION_LOG.write_text(
            "timeout after %ss (nested grok TUI / leader.sock?)\ncmd=%s\nstdout:\n%s\nstderr:\n%s\n"
            % (
                grok_vision_sec(),
                " ".join(cmd[:16] + ["--prompt-json", "<acp>"]),
                e.stdout or "",
                e.stderr or "",
            )
        )
        return out
    except OSError as e:
        out["reason"] = str(e)
        return out
    raw = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    VISION_LOG.write_text(raw)
    out["rc"] = proc.returncode
    parsed = _parse_grok_stdout(proc.stdout or "")
    out["text"] = parsed["text"]
    out["usage"] = parsed["usage"]
    out["window"] = parse_window(parsed["text"])
    out["ok"] = proc.returncode == 0 and out["window"] in ("yes", "no")
    if not out["ok"]:
        out["reason"] = out["reason"] or (
            "window-unknown" if proc.returncode == 0 else "grok-rc"
        )
    return out


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--png", action="append", default=[], help="Hang-cap window PNG")
    ap.add_argument("--keep-png", default=None, help="KEEP window PNG")
    ap.add_argument("--n", type=int, default=None, help="Use /tmp/ss-g3-mill-N.png")
    ap.add_argument("--log", default=None, help="Hang-cap log; uses sibling .png")
    args = ap.parse_args(argv)
    mill_n = None
    state = HERE / "state.json"
    if state.is_file():
        try:
            mill_n = (json.loads(state.read_text()).get("mill") or {}).get("n")
        except (OSError, json.JSONDecodeError):
            mill_n = None
    pngs = resolve_pngs(
        pngs=args.png,
        keep_png=args.keep_png,
        n=args.n,
        log=args.log,
        mill_n=mill_n,
    )
    r = run_vision(pngs)
    print("WINDOW=%s" % (r.get("window") or "unknown"))
    print(
        "Grok Build vision ok=%s rc=%s reason=%s"
        % (r.get("ok"), r.get("rc"), r.get("reason"))
    )
    if r.get("log"):
        print("log %s" % r["log"])
    return 0 if r.get("window") in ("yes", "no") else 1


if __name__ == "__main__":
    raise SystemExit(main())
