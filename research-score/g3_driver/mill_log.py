#!/usr/bin/env python3
"""Hang-cap mill logs as gzip streams (zcat/zgrep style).

Live /tmp/ss-g2-run.log stays uncompressed while SheepShaver writes it.
After hang-cap, mill-N.log is gzipped to mill-N.log.gz and the plain file
is removed. read_log() opens .log or .log.gz. Python 3.9+ stdlib gzip.
"""
from __future__ import annotations

import gzip
import os
import re
import shutil
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Union

HERE = Path(__file__).resolve().parent
GZIP_MAGIC = b"\x1f\x8b"
RE_MILL_LOG = re.compile(r"^ss-g3-mill-\d+\.log(?:\.gz)?$")

LogPath = Union[str, Path]


def gzip_level() -> int:
    try:
        n = int(os.environ.get("G3_LOG_GZIP_LEVEL", "6"))
    except ValueError:
        n = 6
    return max(1, min(n, 9))


def mill_log_stem(path: LogPath) -> Path:
    """ss-g3-mill-N.log / .log.gz -> ss-g3-mill-N (parent included)."""
    p = Path(path)
    name = p.name
    if name.endswith(".log.gz"):
        return p.with_name(name[:-7])
    if name.endswith(".log"):
        return p.with_name(name[:-4])
    if name.endswith(".gz"):
        return p.with_name(name[:-3])
    return p.with_suffix("") if p.suffix else p


def plain_log_path(path: LogPath) -> Path:
    return Path(str(mill_log_stem(path)) + ".log")


def gz_log_path(path: LogPath) -> Path:
    return Path(str(mill_log_stem(path)) + ".log.gz")


def log_is_gzip(path: LogPath) -> bool:
    p = Path(path)
    if p.name.endswith(".gz"):
        return True
    if not p.is_file():
        return False
    try:
        with p.open("rb") as f:
            return f.read(2) == GZIP_MAGIC
    except OSError:
        return False


def log_candidates(path: Optional[LogPath]) -> List[Path]:
    """Prefer a live uncompressed log, then .gz, then research-score copies."""
    if path is None:
        return []
    p = Path(str(path))
    stem = mill_log_stem(p)
    ordered = [
        Path(str(stem) + ".log"),
        Path(str(stem) + ".log.gz"),
        HERE.parent / (stem.name + ".log"),
        HERE.parent / (stem.name + ".log.gz"),
    ]
    out: List[Path] = []
    seen = set()
    for c in ordered:
        key = str(c)
        if key in seen:
            continue
        seen.add(key)
        out.append(c)
    return out


def resolve_log(path: Optional[LogPath]) -> Optional[Path]:
    for c in log_candidates(path):
        if c.is_file():
            return c
    return None


def read_log(path: Optional[LogPath]) -> str:
    """zcat-style: gzip.open text stream, or plain read_text."""
    p = resolve_log(path)
    if p is None:
        return ""
    try:
        if log_is_gzip(p):
            with gzip.open(p, "rt", encoding="utf-8", errors="replace") as f:
                return f.read()
        return p.read_text(errors="replace")
    except OSError:
        return ""


def read_log_tail(path: Optional[LogPath], n: int = 12000) -> str:
    text = read_log(path)
    if n <= 0:
        return text
    return text[-n:]


def gzip_log(
    src: LogPath,
    dest: Optional[LogPath] = None,
    unlink_src: bool = True,
) -> Optional[Path]:
    """Write a gzip stream. Default dest is src + '.gz' for a .log path."""
    src_p = Path(src)
    if not src_p.is_file():
        return None
    if log_is_gzip(src_p):
        return src_p
    dest_p = Path(dest) if dest is not None else gz_log_path(src_p)
    dest_p.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest_p.with_name(dest_p.name + ".tmp")
    try:
        with src_p.open("rb") as inf, gzip.open(
            tmp, "wb", compresslevel=gzip_level()
        ) as out:
            shutil.copyfileobj(inf, out, length=1 << 20)
        tmp.replace(dest_p)
    except OSError:
        try:
            tmp.unlink()
        except OSError:
            pass
        return None
    if unlink_src and src_p.resolve() != dest_p.resolve():
        try:
            src_p.unlink()
        except OSError:
            pass
    return dest_p


def is_mill_n_log(path: LogPath) -> bool:
    return bool(RE_MILL_LOG.match(Path(path).name))


def gzip_mill_logs(directories: Iterable[LogPath]) -> Dict[str, Any]:
    """Compress existing ss-g3-mill-N.log files in place. Skip live g2 log."""
    out: Dict[str, Any] = {"ok": 0, "skip": 0, "fail": 0, "saved": 0}
    for directory in directories:
        d = Path(directory)
        if not d.is_dir():
            continue
        for p in sorted(d.glob("ss-g3-mill-*.log")):
            if p.name.endswith(".gz") or not is_mill_n_log(p):
                continue
            try:
                before = p.stat().st_size
            except OSError:
                out["fail"] += 1
                continue
            dest = gzip_log(p, unlink_src=True)
            if dest is None:
                out["fail"] += 1
                continue
            try:
                after = dest.stat().st_size
            except OSError:
                after = 0
            out["ok"] += 1
            out["saved"] += max(0, before - after)
    return out
