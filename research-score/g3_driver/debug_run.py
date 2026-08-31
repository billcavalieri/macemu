#!/usr/bin/env python3
"""Debug/once only: detach SHA, xcodebuild, 100s hang-cap. Never git clean the driver."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, Optional

HERE = Path(__file__).resolve().parent


def repo_root() -> Path:
    return Path(os.environ.get("MACEMU_ROOT", str(HERE.parents[1]))).resolve()


def prefs_path() -> Path:
    env = os.environ.get("G3_PREFS")
    if env:
        return Path(env)
    return (
        Path.home()
        / "Library"
        / "Application Support"
        / "SheepShaver"
        / "os921"
        / "prefs"
    )


def _copy_driver(dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(
        HERE,
        dst,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )


def _restore_driver(stash: Path) -> None:
    dest = repo_root() / "research-score" / "g3_driver"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not (dest / "g3_driver.py").exists():
        _copy_driver(dest)


def debug_sha(sha: str, dd: Optional[Path] = None, force: bool = False) -> Dict[str, object]:
    """Build and hang-cap. Fail closed (not NEW)."""
    sha = sha.strip().lower()
    root = repo_root()
    log_path = Path("/tmp/ss-pr10-%s.log" % sha[:8])
    out: Dict[str, object] = {
        "ok": False,
        "fail": None,
        "log": str(log_path),
        "perl_exit": None,
        "sha": sha,
    }
    prefs = prefs_path()
    if not prefs.is_file():
        out["fail"] = "missing-prefs"
        return out
    rom_ok = False
    try:
        for line in prefs.read_text(errors="replace").splitlines():
            if line.startswith("rom "):
                rom = Path(line.split(None, 1)[1].strip())
                if rom.is_file():
                    rom_ok = True
    except OSError:
        rom_ok = False
    if not rom_ok:
        out["fail"] = "missing-rom"
        return out

    stash = Path(tempfile.mkdtemp(prefix="g3_driver_stash_")) / "g3_driver"
    _copy_driver(stash)
    try:
        subprocess.call(["pkill", "-x", "SheepShaver"])
        subprocess.call(
            ["git", "fetch", "origin", "g3", "cursor/g3-dec-yield-432c"],
            cwd=str(root),
        )
        # Detach C++ at SHA. Do not git clean research-score/.
        r = subprocess.call(
            ["git", "checkout", "--detach", sha],
            cwd=str(root),
        )
        _restore_driver(stash)
        if r != 0:
            out["fail"] = "checkout"
            return out

        dd_path = Path(dd) if dd else Path("/tmp/macemu-g3-%s" % sha[:8])
        if dd_path.exists():
            shutil.rmtree(dd_path)
        proj = root / "SheepShaver" / "src" / "MacOSX" / "SheepShaver_Xcode8.xcodeproj"
        xc = [
            "xcodebuild",
            "-project",
            str(proj),
            "-scheme",
            "SheepShaver",
            "-configuration",
            "Debug",
            "ARCHS=arm64",
            "ONLY_ACTIVE_ARCH=YES",
            "-derivedDataPath",
            str(dd_path),
        ]
        xc_rc = subprocess.call(xc, cwd=str(root))
        if xc_rc != 0:
            out["fail"] = "xcodebuild"
            return out
        app = (
            dd_path
            / "Build"
            / "Products"
            / "Debug"
            / "SheepShaver.app"
            / "Contents"
            / "MacOS"
            / "SheepShaver"
        )
        if not app.is_file():
            out["fail"] = "xcodebuild"
            return out
        if log_path.exists():
            log_path.unlink()
        perl = [
            "perl",
            "-e",
            "alarm 100; exec @ARGV",
            "--",
            "stdbuf",
            "-o0",
            str(app),
            "--config",
            str(prefs),
        ]
        with log_path.open("w") as lf:
            pr = subprocess.call(perl, cwd=str(root), stdout=lf, stderr=subprocess.STDOUT)
        out["perl_exit"] = pr
        subprocess.call(["pkill", "-x", "SheepShaver"])
        # leftover SheepShaver gone
        still = subprocess.call(["pgrep", "-x", "SheepShaver"])
        if still == 0:
            out["fail"] = "process-alive"
            return out
        if pr != 142:
            out["fail"] = "perl_exit"
            return out
        text = log_path.read_text(errors="replace") if log_path.is_file() else ""
        if "heartbeat pc=" not in text:
            out["fail"] = "no-heartbeat"
            return out
        out["ok"] = True
        return out
    finally:
        _restore_driver(stash)
        shutil.rmtree(stash.parent, ignore_errors=True)
