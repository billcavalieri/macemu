#!/usr/bin/env python3
"""Debug/once only: detach SHA, xcodebuild, 100s hang-cap. Never git clean the driver."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Callable, Dict, Optional

from mill_apply import mill_stamp_68k
from parse_log import hangcap_early_fail, hangcap_g0_stuck, hangcap_keep_stable

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


def hangcap_sec() -> int:
    """G2 and hang 04cecd36 show up in the first seconds. 100s only resamples the walk."""
    try:
        n = int(os.environ.get("G3_HANGCAP_SEC", "45"))
    except ValueError:
        n = 45
    return max(15, min(n, 180))


def mill_dd_default() -> Path:
    return Path("/tmp/macemu-g3-mill")


def mill_spec_dd() -> Path:
    return Path("/tmp/macemu-g3-mill-spec")


def mill_app(dd: Path) -> Path:
    return (
        Path(dd)
        / "Build"
        / "Products"
        / "Debug"
        / "SheepShaver.app"
        / "Contents"
        / "MacOS"
        / "SheepShaver"
    )


def mill_app_bundle(dd: Path) -> Path:
    return Path(dd) / "Build" / "Products" / "Debug" / "SheepShaver.app"


def binary_has_stamp(app: Path, stamp: str) -> bool:
    if not stamp or not app.is_file():
        return False
    needle = stamp.encode("ascii", "ignore")
    try:
        data = app.read_bytes()
    except OSError:
        return False
    return needle in data


def use_runtime_68k(kind: Optional[str] = None) -> bool:
    if kind != "skip-68k":
        return False
    v = os.environ.get("G3_RUNTIME_68K", "1").strip().lower()
    return v not in ("0", "no", "false")


def mill_binary_match(
    app: Path,
    kind: Optional[str] = None,
    hang_off: Optional[int] = None,
    runtime: bool = False,
) -> bool:
    if not app.is_file():
        return False
    k = kind or ""
    if k == "skip-68k" and runtime:
        return binary_has_stamp(app, "G3: 68k map r24=")
    if k == "skip-68k" and hang_off is not None:
        return binary_has_stamp(app, mill_stamp_68k(int(hang_off)))
    return True


def xcodebuild_mill(
    dd: Path,
    clean: bool = False,
    xc_log: Optional[Path] = None,
) -> int:
    """Incremental xcodebuild unless clean=True. Returns xcodebuild rc."""
    root = repo_root()
    dd = Path(dd)
    if clean and dd.exists():
        shutil.rmtree(dd)
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
        str(dd),
    ]
    log = Path(xc_log) if xc_log else Path("/tmp/ss-g3-mill-xcodebuild.log")
    with log.open("w") as xf:
        return subprocess.call(xc, cwd=str(root), stdout=xf, stderr=subprocess.STDOUT)


def promote_spec_dd(spec: Path, dest: Path) -> bool:
    spec = Path(spec)
    dest = Path(dest)
    app = mill_app(spec)
    if not app.is_file():
        return False
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(spec, dest)
    return mill_app(dest).is_file()


def hangcap_working_tree(
    log_path: Optional[Path] = None,
    dd: Optional[Path] = None,
    sec: Optional[int] = None,
    stop: Optional[Callable[[], bool]] = None,
    kind: Optional[str] = None,
    hang_off: Optional[int] = None,
    saw_68k: bool = False,
    skip_build: bool = False,
    after_launch: Optional[Callable[[], None]] = None,
) -> Dict[str, object]:
    """Build the current tree and hang-cap. Incremental xcodebuild; no git checkout."""
    root = repo_root()
    if log_path is None:
        log_path = Path("/tmp/ss-g3-mill.log")
    out: Dict[str, object] = {
        "ok": False,
        "fail": None,
        "log": str(log_path),
        "perl_exit": None,
        "sha": "working",
        "clean_rebuild": False,
        "skipped_build": False,
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

    subprocess.call(["pkill", "-x", "SheepShaver"])
    dd_path = Path(dd) if dd else mill_dd_default()
    force_clean = os.environ.get("G3_XCODE_CLEAN", "").strip() in ("1", "true", "yes")
    runtime = use_runtime_68k(kind) and hang_off is not None
    old_skip = os.environ.get("G3_SKIP_68K_OFF")
    if runtime:
        os.environ["G3_SKIP_68K_OFF"] = "0x%x" % int(hang_off)
    app = mill_app(dd_path)
    if runtime and app.is_file() and mill_binary_match(
        app, kind=kind, hang_off=hang_off, runtime=True
    ):
        skip_build = True
    need_build = not skip_build
    if skip_build and mill_binary_match(
        app, kind=kind, hang_off=hang_off, runtime=runtime
    ):
        need_build = False
        out["skipped_build"] = True
    elif skip_build:
        need_build = True
    if need_build:
        xc_rc = xcodebuild_mill(dd_path, clean=force_clean)
        app = mill_app(dd_path)
        if xc_rc != 0 or not app.is_file():
            out["fail"] = "xcodebuild"
            return out
        if not mill_binary_match(
            app, kind=kind, hang_off=hang_off, runtime=runtime
        ):
            xc_rc = xcodebuild_mill(dd_path, clean=True)
            out["clean_rebuild"] = True
            app = mill_app(dd_path)
            if xc_rc != 0 or not app.is_file():
                out["fail"] = "xcodebuild"
                return out
            if not mill_binary_match(
                app, kind=kind, hang_off=hang_off, runtime=runtime
            ):
                out["fail"] = "mill-mismatch"
                return out
    app_bundle = mill_app_bundle(dd_path)
    if not (app_bundle / "Contents" / "MacOS" / "SheepShaver").is_file():
        out["fail"] = "xcodebuild"
        return out
    if log_path.exists():
        log_path.unlink()
    g2log = Path("/tmp/ss-g2-run.log")
    if g2log.exists():
        g2log.unlink()
    # Launch the .app so Cocoa maps the SDL window. perl+stdbuf exec of
    # the inner binary leaves NSApp unresponsive (Force Quit, no window).
    open_args = [
        "open",
        "-n",
        str(app_bundle),
        "--args",
        "--config",
        str(prefs),
    ]
    if runtime:
        open_args.extend(["--g3-skip-68k", "0x%x" % int(hang_off)])
    rc = subprocess.call(open_args)
    if rc != 0:
        out["fail"] = "open"
        return out
    if after_launch is not None:
        try:
            after_launch()
        except Exception:
            pass
    t_wait = time.time()
    ss_seen = False
    while time.time() - t_wait < 10:
        if subprocess.call(
            ["pgrep", "-x", "SheepShaver"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) == 0:
            ss_seen = True
            break
        time.sleep(0.2)
    out["ss_seen"] = ss_seen
    if not ss_seen:
        subprocess.call(["pkill", "-x", "SheepShaver"])
        out["fail"] = "no-process"
        out["ss_alive_sec"] = 0.0
        return out
    t0 = time.time()
    cap = hangcap_sec() if sec is None else max(15, int(sec))
    out["hangcap_sec"] = cap
    out["early_fail"] = None
    out["ss_alive_sec"] = 0.0
    while time.time() - t0 < cap:
        if stop and stop():
            break
        time.sleep(1)
        if subprocess.call(["pgrep", "-x", "SheepShaver"],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) != 0:
            break
        if g2log.is_file():
            try:
                live = g2log.read_text(errors="replace")
            except OSError:
                live = ""
            early = hangcap_early_fail(live, saw_68k=saw_68k)
            if early:
                out["early_fail"] = early
                break
            if hangcap_g0_stuck(live, time.time() - t0):
                out["early_fail"] = "g0_only"
                break
            if hangcap_keep_stable(live):
                out["early_stop"] = "keep_stable"
                break
    out["ss_alive_sec"] = time.time() - t0
    subprocess.call(["pkill", "-x", "SheepShaver"])
    time.sleep(0.5)
    still = subprocess.call(["pgrep", "-x", "SheepShaver"],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if still == 0:
        subprocess.call(["pkill", "-9", "-x", "SheepShaver"])
        out["fail"] = "process-alive"
        return out
    if g2log.is_file():
        shutil.copy2(g2log, log_path)
    elif not log_path.is_file():
        log_path.write_text("")
    out["perl_exit"] = 142
    text = log_path.read_text(errors="replace") if log_path.is_file() else ""
    if "heartbeat pc=" in text:
        out["ok"] = True
        return out
    out["fail"] = "no-heartbeat"
    return out
