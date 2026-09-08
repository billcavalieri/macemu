#!/usr/bin/env python3
"""Debug/once only: detach SHA, xcodebuild, 100s hang-cap. Never git clean the driver."""
from __future__ import annotations

import ctypes
import ctypes.util
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path
from typing import Any, Callable, Dict, Optional, Tuple, Union

from mill_apply import mill_stamp_68k
from mill_log import gzip_log, gz_log_path, mill_log_stem, read_log, resolve_log
from parse_log import hangcap_early_fail, hangcap_g0_stuck, hangcap_keep_stable

HERE = Path(__file__).resolve().parent
G2_FB = Path("/tmp/ss-g2-fb.pgm")
G2_FB_PLANT = Path("/tmp/ss-g2-fb-plant.pgm")
TCC_PROBE_PNG = Path("/tmp/ss-g3-tcc-screencapture.png")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
_SCREEN_RECORDING_TRIPPED = False


def hangcap_media_paths(log_path: Path) -> Dict[str, Path]:
    """Persist mill-N log + screenshot + FB dumps. Not deleted by ./run."""
    stem = str(mill_log_stem(log_path))
    return {
        "log": Path(stem + ".log"),
        "log_gz": Path(stem + ".log.gz"),
        "png": Path(stem + ".png"),
        "plant_png": Path(stem + "-fb-plant.png"),
        "fb": Path(stem + "-fb.pgm"),
        "plant": Path(stem + "-fb-plant.pgm"),
    }


def copy_hangcap_media(log_path: Path, dest_dir: Path) -> None:
    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    media = hangcap_media_paths(log_path)
    resolved = resolve_log(media["log"])
    if resolved is not None:
        dest_gz = dest_dir / (mill_log_stem(media["log"]).name + ".log.gz")
        if resolved.name.endswith(".gz"):
            shutil.copy2(resolved, dest_gz)
        else:
            gzip_log(resolved, dest=dest_gz, unlink_src=False)
    for key in ("png", "plant_png", "fb", "plant"):
        src = media[key]
        if src.is_file():
            shutil.copy2(src, dest_dir / src.name)


def file_sha256(path: Optional[Union[str, Path]]) -> Optional[str]:
    """SHA-256 of a hang-cap dump. None if missing. Do not hash window PNGs."""
    if path is None:
        return None
    p = Path(path)
    if not p.is_file():
        return None
    h = hashlib.sha256()
    try:
        with p.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
    except OSError:
        return None
    return h.hexdigest()


def resolve_hangcap_file(path: Path) -> Path:
    """Prefer /tmp; fall back to the research-score copy."""
    path = Path(path)
    if path.is_file():
        return path
    alt = HERE.parent / path.name
    if alt.is_file():
        return alt
    return path


def fb_diff_threshold() -> float:
    """Fraction of pixels that must differ before fb=yes. Default 10%."""
    try:
        n = float(os.environ.get("G3_FB_DIFF", "0.10"))
    except ValueError:
        n = 0.10
    return max(0.0, min(n, 1.0))


def pgm_diff_frac(
    keep_path: Path,
    mill_path: Path,
) -> Optional[float]:
    """Pixel mismatch fraction for two P5 dumps. None if not comparable."""
    keep = read_pgm(keep_path)
    mill = read_pgm(mill_path)
    if keep is None or mill is None:
        return None
    kw, kh, kp = keep
    mw, mh, mp = mill
    if kw != mw or kh != mh or not kp:
        return None
    n = len(kp)
    if n != len(mp):
        return None
    diff = 0
    for a, b in zip(kp, mp):
        if a != b:
            diff += 1
    return float(diff) / float(n)


def compare_keep_fb(
    keep_log: Optional[Union[str, Path]],
    mill_log: Union[str, Path],
) -> Dict[str, Any]:
    """KEEP vs this mill's guest PGM dumps. Not the window PNG.

    Glass (fb): valid same-size P5, yes if pixel mismatch >= G3_FB_DIFF
    (default 10%). Truncated dumps are unknown, not yes.
    Plant: SHA-256. Notify only uses fb=yes.
    """
    mill_media = hangcap_media_paths(Path(mill_log))
    keep_media = (
        hangcap_media_paths(Path(keep_log)) if keep_log else None
    )
    out: Dict[str, Any] = {
        "fb_changed": "unknown",
        "fb": None,
        "plant": None,
        "keep_fb": None,
        "mill_fb": None,
        "keep_plant": None,
        "mill_plant": None,
        "fb_diff": None,
        "reason": None,
    }
    mill_fb = file_sha256(resolve_hangcap_file(mill_media["fb"]))
    mill_plant = file_sha256(resolve_hangcap_file(mill_media["plant"]))
    out["mill_fb"] = mill_fb
    out["mill_plant"] = mill_plant
    if keep_media is None:
        out["reason"] = "no-keep-log"
        return out
    keep_fb_path = resolve_hangcap_file(keep_media["fb"])
    mill_fb_path = resolve_hangcap_file(mill_media["fb"])
    keep_fb = file_sha256(keep_fb_path)
    keep_plant = file_sha256(resolve_hangcap_file(keep_media["plant"]))
    out["keep_fb"] = keep_fb
    out["keep_plant"] = keep_plant

    frac = pgm_diff_frac(keep_fb_path, mill_fb_path)
    out["fb_diff"] = frac
    if frac is not None:
        out["fb"] = "yes" if frac >= fb_diff_threshold() else "no"
    else:
        out["fb"] = "unknown"
        if mill_fb or keep_fb:
            out["reason"] = out["reason"] or "fb-incomparable"

    if keep_plant and mill_plant:
        out["plant"] = "yes" if keep_plant != mill_plant else "no"
    else:
        out["plant"] = "unknown"

    if out["fb"] == "yes" or out["plant"] == "yes":
        out["fb_changed"] = "yes"
    elif out["fb"] == "no" or out["plant"] == "no":
        out["fb_changed"] = "no"
    else:
        out["fb_changed"] = "unknown"
        out["reason"] = out["reason"] or "missing-pgm"
    return out


def format_fb_changed(cmp: Dict[str, Any]) -> str:
    extra = ""
    if cmp.get("fb_diff") is not None:
        extra += " fb_diff=%.3f" % float(cmp["fb_diff"])
    if cmp.get("reason"):
        extra += " reason=%s" % cmp["reason"]
    return "fb_changed=%s fb=%s plant=%s%s" % (
        cmp.get("fb_changed"),
        cmp.get("fb"),
        cmp.get("plant"),
        extra,
    )


FB_GLASS_LOG = Path("/tmp/ss-g3-fb-glass-changed.log")
SKIP68K_EMPTY_LOG = Path("/tmp/ss-g3-skip68k-empty.log")


def fb_notify_enabled() -> bool:
    v = os.environ.get("G3_FB_NOTIFY", "1").strip().lower()
    return v not in ("0", "no", "false")


def _macos_notify_glass(title: str, body: str) -> None:
    script = (
        'display notification %s with title %s sound name "Glass"'
        % (_osa_quote(body), _osa_quote(title))
    )
    try:
        subprocess.call(
            ["osascript", "-e", script],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=8,
        )
    except (OSError, subprocess.TimeoutExpired):
        pass
    sound = Path("/System/Library/Sounds/Glass.aiff")
    if sound.is_file():
        try:
            subprocess.call(
                ["afplay", str(sound)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass


def notify_skip68k_empty(mill: Optional[Dict[str, Any]] = None) -> bool:
    """macOS notification + Glass when skip-68k leftover map is empty."""
    if not fb_notify_enabled():
        return False
    n = int((mill or {}).get("n") or 0)
    line = (
        "SKIP68K_EMPTY mill=%s leftover=grok-escalate LOOK (mill C++ then ./run)"
        % n
    )
    print(line)
    try:
        with SKIP68K_EMPTY_LOG.open("a") as f:
            f.write(line + "\n")
    except OSError:
        pass
    _macos_notify_glass(
        "G3 skip-68k empty",
        "mill-%s map empty. Mill C++ then ./run" % n,
    )
    return True


def notify_fb_glass(log_path: Path, cmp: Dict[str, Any]) -> bool:
    """macOS notification + sound + /tmp log when guest the_buffer changes.

    Plant-only changes do not notify. Does not open Preview. Fail soft.
    """
    if not fb_notify_enabled() or cmp.get("fb") != "yes":
        return False
    media = hangcap_media_paths(Path(log_path))
    png = media["png"]
    mill_n = mill_log_stem(log_path).name.replace("ss-g3-mill-", "")
    line = "FB_GLASS_CHANGED mill=%s png=%s LOOK (installer? then G3_WINDOW=yes)" % (
        mill_n,
        png,
    )
    print(line)
    try:
        with FB_GLASS_LOG.open("a") as f:
            f.write(line + "\n")
    except OSError:
        pass
    title = "G3 mill glass changed"
    body = "mill-%s guest FB. Open %s" % (mill_n, png.name)
    _macos_notify_glass(title, body)
    return True


def _osa_quote(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def read_pgm(path: Path) -> Optional[Tuple[int, int, bytes]]:
    """P5 grayscale. Returns (w, h, pixels) or None."""
    path = Path(path)
    if not path.is_file():
        return None
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if not (data.startswith(b"P5") or data.startswith(b"P6")):
        return None
    channels = 3 if data.startswith(b"P6") else 1
    i = 2

    def skip_ws_comments() -> None:
        nonlocal i
        while i < len(data):
            while i < len(data) and data[i] in b" \t\r\n":
                i += 1
            if i < len(data) and data[i] == ord("#"):
                while i < len(data) and data[i] not in b"\r\n":
                    i += 1
                continue
            return

    def token() -> Optional[bytes]:
        nonlocal i
        skip_ws_comments()
        start = i
        while i < len(data) and data[i] not in b" \t\r\n#":
            i += 1
        if start == i:
            return None
        return data[start:i]

    try:
        tw = token()
        th = token()
        tmax = token()
        if tw is None or th is None or tmax is None:
            return None
        w = int(tw)
        h = int(th)
        maxval = int(tmax)
    except ValueError:
        return None
    if w <= 0 or h <= 0 or maxval <= 0 or maxval > 255:
        return None
    if i < len(data) and data[i] in b" \t\r\n":
        i += 1
    need = w * h * channels
    pixels = data[i : i + need]
    if len(pixels) != need:
        return None
    return w, h, pixels


def write_png_gray(path: Path, w: int, h: int, pixels: bytes) -> bool:
    return write_png(path, w, h, pixels, 1)


def write_png(path: Path, w: int, h: int, pixels: bytes, channels: int = 1) -> bool:
    """8-bit gray (1) or RGB (3) PNG. stdlib zlib."""
    if channels not in (1, 3) or w <= 0 or h <= 0:
        return False
    stride = w * channels
    if len(pixels) < stride * h:
        return False
    color = 2 if channels == 3 else 0
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(pixels[y * stride : (y + 1) * stride])
    compressed = zlib.compress(bytes(raw), 6)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, color, 0, 0, 0)

    def chunk(tag: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(tag)
        crc = zlib.crc32(body, crc) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", crc)

    try:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(
            PNG_MAGIC + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
        )
    except OSError:
        return False
    return png_is_valid(path)


def pgm_to_png(src: Path, dest: Path) -> bool:
    parsed = read_pgm(src)
    if parsed is None:
        return False
    w, h, pixels = parsed
    if len(pixels) == w * h * 3:
        return write_png(dest, w, h, pixels, 3)
    return write_png(dest, w, h, pixels, 1)


def png_is_valid(path: Path) -> bool:
    """True only for a real PNG signature. screencapture writes b'PNG' when TCC denies."""
    path = Path(path)
    if not path.is_file():
        return False
    try:
        with path.open("rb") as f:
            return f.read(8) == PNG_MAGIC
    except OSError:
        return False


def unlink_invalid_png(path: Path) -> None:
    path = Path(path)
    if path.is_file() and not png_is_valid(path):
        try:
            path.unlink()
        except OSError:
            pass


def trip_screen_recording() -> Dict[str, Any]:
    """Force the macOS Screen Recording prompt for this python3 and screencapture.

    CGRequestScreenCaptureAccess plus a 1x1 CGWindowListCreateImage trip python3.
    screencapture -t png -R 0,0,128,128 trips the binary hang-cap actually uses.
    A 3-byte 'PNG' stub means TCC denied; that file is deleted. Once per process.
    Does not launch SheepShaver.
    """
    global _SCREEN_RECORDING_TRIPPED
    out: Dict[str, Any] = {
        "preflight": None,
        "request": None,
        "cg_image": False,
        "screencapture": False,
        "ok": False,
        "probe": str(TCC_PROBE_PNG),
    }
    cg_path = ctypes.util.find_library("CoreGraphics")
    cf_path = ctypes.util.find_library("CoreFoundation")
    if cg_path:
        cg = ctypes.CDLL(cg_path)
        if hasattr(cg, "CGPreflightScreenCaptureAccess"):
            cg.CGPreflightScreenCaptureAccess.restype = ctypes.c_bool
            try:
                out["preflight"] = bool(cg.CGPreflightScreenCaptureAccess())
            except OSError:
                out["preflight"] = None
        if hasattr(cg, "CGRequestScreenCaptureAccess"):
            cg.CGRequestScreenCaptureAccess.restype = ctypes.c_bool
            try:
                out["request"] = bool(cg.CGRequestScreenCaptureAccess())
            except OSError:
                out["request"] = None

        class CGPoint(ctypes.Structure):
            _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]

        class CGSize(ctypes.Structure):
            _fields_ = [("width", ctypes.c_double), ("height", ctypes.c_double)]

        class CGRect(ctypes.Structure):
            _fields_ = [("origin", CGPoint), ("size", CGSize)]

        cg.CGWindowListCreateImage.restype = ctypes.c_void_p
        cg.CGWindowListCreateImage.argtypes = [
            CGRect,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_uint32,
        ]
        rect = CGRect(CGPoint(0.0, 0.0), CGSize(1.0, 1.0))
        img = None
        try:
            img = cg.CGWindowListCreateImage(rect, 1, 0, 0)
        except OSError:
            img = None
        out["cg_image"] = bool(img)
        if img and cf_path:
            cf = ctypes.CDLL(cf_path)
            cf.CFRelease.argtypes = [ctypes.c_void_p]
            try:
                cf.CFRelease(img)
            except OSError:
                pass
    try:
        rc = subprocess.call(
            [
                "screencapture",
                "-x",
                "-t",
                "png",
                "-R",
                "0,0,128,128",
                str(TCC_PROBE_PNG),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        out["screencapture"] = rc == 0 and png_is_valid(TCC_PROBE_PNG)
        if not out["screencapture"]:
            unlink_invalid_png(TCC_PROBE_PNG)
    except OSError:
        out["screencapture"] = False
        unlink_invalid_png(TCC_PROBE_PNG)
    out["ok"] = bool(
        out.get("request") or out["cg_image"] or out["screencapture"]
    )
    _SCREEN_RECORDING_TRIPPED = True
    return out


def screenshot_sheepshaver(dest: Path) -> bool:
    """Capture the SheepShaver window. Fail soft (Screen Recording)."""
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not _SCREEN_RECORDING_TRIPPED:
        trip_screen_recording()
    script = (
        'tell application "System Events" to '
        'get id of window 1 of (first process whose name is "SheepShaver")'
    )
    try:
        r = subprocess.run(
            ["osascript", "-e", script],
            capture_output=True,
            text=True,
            timeout=8,
        )
    except (OSError, subprocess.TimeoutExpired):
        r = None
    wid = (r.stdout or "").strip() if r is not None else ""
    cmd = ["screencapture", "-x", "-t", "png"]
    if r is not None and r.returncode == 0 and wid.isdigit():
        cmd.extend(["-l", wid])
    cmd.append(str(dest))
    try:
        rc = subprocess.call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        unlink_invalid_png(dest)
        return False
    ok = rc == 0 and png_is_valid(dest)
    if not ok:
        unlink_invalid_png(dest)
    return ok


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
        text = read_log(log_path)
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
        return (
            binary_has_stamp(app, "G3: 68k map r24=")
            and binary_has_stamp(app, "G3: FB dump packed xRGB")
            and binary_has_stamp(app, "G3: 68k GetNewDialog A97C")
        )
    if k == "skip-68k" and hang_off is not None:
        return binary_has_stamp(app, mill_stamp_68k(int(hang_off)))
    if k == "grok-escalate":
        return binary_has_stamp(app, "G3: 68k LoadSeg A9F0 enter")
    if k == "stay-code66":
        return binary_has_stamp(app, "G3: 68k stay CODE 66")
    if k == "getresource-a9a0":
        return binary_has_stamp(app, "G3: 68k GetResource A9A0 toast")
    if k == "getnewdialog-dlog":
        return binary_has_stamp(app, "G3: 68k GetNewDialog A97C toast")
    if k == "code66-syserr99":
        return binary_has_stamp(app, "G3: 68k CODE 66 SysError 99 continue")
    if k == "code66-resume":
        return binary_has_stamp(app, "G3: 68k LoadSeg A9F0 CODE 66 resume")
    if k == "code66-allow-9440":
        return binary_has_stamp(app, "G3: 68k CODE 66 allow 0x9440")
    if k == "launch-upgrader":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader")
    if k == "splash-510":
        return binary_has_stamp(app, "G3: 68k GetNewDialog A97C Splash 510")
    if k == "splash-510-even":
        return binary_has_stamp(app, "G3: 68k GetNewDialog A97C Splash 510 even")
    if k == "pict-1000":
        return binary_has_stamp(app, "G3: 68k DrawPicture A8F6 PICT 1000")
    if k == "pef-upgrader":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF")
    if k == "pef-enter":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF enter")
    if k == "pef-imports":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF import")
    if k == "pef-sysenv":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF SysEnvirons")
    if k == "pef-vol":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF vol")
    if k == "pef-dce":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF dce")
    if k == "pef-wait":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF WaitNextEvent")
    if k == "pef-te":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF TENew")
    if k == "pef-terec":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF TERec")
    if k == "pef-skipte":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF skipTE")
    if k == "pef-skipdi":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF skipDI")
    if k == "pef-idx":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF idx")
    if k == "pef-gnd":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog 510")
    if k == "pef-gndid":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id=")
    if k == "pef-d519":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF DLOG 519")
    if k == "pef-modal":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF ModalDialog")
    if k == "pef-no519":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF no519")
    if k == "pef-show":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF ShowWindow")
    if k == "pef-forcesplash":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF forceSplash")
    if k == "pef-skipwait":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF skipWait")
    if k == "pef-callsplash":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF callSplash")
    if k == "pef-skipalert":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert")
    if k == "pef-nimp":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF nimp")
    if k == "pef-jumpsplash":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF jumpSplash")
    if k == "pef-plantsplash":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF plantSplash")
    if k == "pef-forceblit":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF forceBlit")
    if k == "pef-blitoff":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF blitOff")
    if k == "pef-callgnd":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF callGnd")
    if k == "pef-skipae":
        return binary_has_stamp(app, "G3: 68k Launch A9F2 CFM Upgrader PEF skipAE")
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
    gz_existing = gz_log_path(log_path)
    if gz_existing.is_file():
        gz_existing.unlink()
    g2log = Path("/tmp/ss-g2-run.log")
    if g2log.exists():
        g2log.unlink()
    for live in (G2_FB, G2_FB_PLANT):
        if live.is_file():
            try:
                live.unlink()
            except OSError:
                pass
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
            if hangcap_keep_stable(live) and G2_FB.is_file() and G2_FB.stat().st_size > 64:
                out["early_stop"] = "keep_stable"
                break
    out["ss_alive_sec"] = time.time() - t0
    media = hangcap_media_paths(log_path)
    out["screenshot"] = None
    want_glass = os.environ.get("G3_SCREENCAPTURE", "").strip().lower() in (
        "1",
        "true",
        "yes",
    )
    glass = Path(str(mill_log_stem(log_path)) + "-glass.png") if want_glass else None
    if glass is not None:
        screenshot_sheepshaver(glass)
    subprocess.call(["pkill", "-x", "SheepShaver"])
    time.sleep(0.5)
    still = subprocess.call(["pgrep", "-x", "SheepShaver"],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if still == 0:
        subprocess.call(["pkill", "-9", "-x", "SheepShaver"])
        out["fail"] = "process-alive"
    if g2log.is_file():
        shutil.copy2(g2log, log_path)
    elif not log_path.is_file():
        log_path.write_text("")
    if G2_FB.is_file():
        shutil.copy2(G2_FB, media["fb"])
    if G2_FB_PLANT.is_file():
        shutil.copy2(G2_FB_PLANT, media["plant"])
    out["fb"] = str(media["fb"]) if media["fb"].is_file() else None
    out["fb_plant"] = str(media["plant"]) if media["plant"].is_file() else None
    if media["fb"].is_file() and pgm_to_png(media["fb"], media["png"]):
        out["screenshot"] = str(media["png"])
    if media["plant"].is_file():
        pgm_to_png(media["plant"], media["plant_png"])
    out["fb_png"] = str(media["png"]) if media["png"].is_file() else None
    out["fb_plant_png"] = (
        str(media["plant_png"]) if media["plant_png"].is_file() else None
    )
    text = log_path.read_text(errors="replace") if log_path.is_file() else ""
    gz = gzip_log(log_path, unlink_src=True)
    if gz is not None:
        out["log"] = str(gz)
    if out.get("fail") == "process-alive":
        return out
    out["perl_exit"] = 142
    if "heartbeat pc=" in text:
        out["ok"] = True
        return out
    out["fail"] = "no-heartbeat"
    return out
