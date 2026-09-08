#!/usr/bin/env python3
"""Load NewWorldView mill-histogram.json for skip-68k priority hints."""
from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any, Dict, List, Optional

from mill_annotations import parse_usage

FORMAT_ID = "NewWorldView-mill-histogram"
RE_ROM_OFFSET = re.compile(r"^0x([0-9a-fA-F]+)$")

_ACTIVE: Optional["MillHistogram"] = None


def off_from_entry(entry: Dict[str, Any]) -> Optional[int]:
    rom = entry.get("romOffset")
    if isinstance(rom, str):
        m = RE_ROM_OFFSET.match(rom.strip())
        if m:
            return int(m.group(1), 16)
    return None


class MillHistogram:
    def __init__(self, path: Path, document: Dict[str, Any]) -> None:
        self.path = path
        self.format = str(document.get("format") or "")
        self.version = int(document.get("version") or 0)
        self.rom_key = str(document.get("romKey") or "")
        self.scanned_logs = int(document.get("scannedLogs") or 0)
        self.entries: List[Dict[str, Any]] = list(document.get("entries") or [])
        self.by_off: Dict[int, Dict[str, Any]] = {}
        for entry in self.entries:
            off = off_from_entry(entry)
            if off is not None:
                self.by_off[off] = entry
        self.document_usage = parse_usage(document.get("tokenUsage"))

    @classmethod
    def load(cls, path: Path) -> "MillHistogram":
        path = Path(path)
        data = json.loads(path.read_text())
        if not isinstance(data, dict):
            raise ValueError("mill-histogram root must be an object")
        fmt = data.get("format")
        if fmt and fmt != FORMAT_ID:
            raise ValueError("unexpected format %r (want %s)" % (fmt, FORMAT_ID))
        return cls(path, data)

    def entry_at(self, off: int) -> Optional[Dict[str, Any]]:
        return self.by_off.get(int(off))

    def ranked_offs(self) -> List[int]:
        out: List[int] = []
        seen = set()
        for entry in self.entries:
            off = off_from_entry(entry)
            if off is None or off in seen:
                continue
            seen.add(off)
            out.append(off)
        return out

    def token_usage(self) -> Dict[str, int]:
        if any(int(self.document_usage.get(k) or 0) for k in ("in", "out", "total")):
            return dict(self.document_usage)
        return {"in": 0, "out": 0, "total": 0}

    def format_token_line(self, role: str = "mill-histogram.json") -> str:
        from g3_lock import format_tokens

        return format_tokens("apple_fm", self.token_usage(), role)

    def format_pack_section(self, limit: int = 24) -> str:
        if not self.entries:
            return ""
        lines = [
            "## NewWorldView histogram",
            "",
            "- source: `%s`" % self.path,
            "- romKey: `%s`" % (self.rom_key or "-"),
            "- scannedLogs: `%s`" % self.scanned_logs,
            "- entries: `%s`" % len(self.entries),
            "",
            "| off | count | action | status |",
            "|-----|-------|--------|--------|",
        ]
        for entry in self.entries[:limit]:
            off = off_from_entry(entry)
            off_s = "0x%x" % off if off is not None else "-"
            lines.append(
                "| %s | %s | %s | %s |"
                % (
                    off_s,
                    entry.get("count") or "-",
                    entry.get("annotationAction") or "-",
                    entry.get("annotationStatus") or "-",
                )
            )
        if len(self.entries) > limit:
            lines.append("| … | … | … | … |")
        usage = self.token_usage()
        if any(int(usage.get(k) or 0) for k in ("in", "out", "total")):
            lines.append("")
            lines.append(self.format_token_line())
        lines.append("")
        return "\n".join(lines)


def load_histogram(path: Path) -> MillHistogram:
    global _ACTIVE
    _ACTIVE = MillHistogram.load(path)
    return _ACTIVE


def clear_histogram() -> None:
    global _ACTIVE
    _ACTIVE = None


def active() -> Optional[MillHistogram]:
    global _ACTIVE
    if _ACTIVE is None:
        env = os.environ.get("G3_HISTOGRAM", "").strip()
        if env:
            p = Path(env)
            if p.is_file():
                load_histogram(p)
    return _ACTIVE


def resolve_path(explicit: Optional[str] = None) -> Optional[Path]:
    raw = (explicit or os.environ.get("G3_HISTOGRAM", "")).strip()
    if not raw:
        return None
    p = Path(raw)
    return p if p.is_file() else None
