#!/usr/bin/env python3
"""Load NewWorldView mill-annotations.json for skip-68k policy and Grok packs."""
from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any, Dict, List, Optional

from g3_lock import add_usage, zero_usage

FORMAT_ID = "NewWorldView-mill-annotations"
RE_ROM_OFFSET = re.compile(r"^0x([0-9a-fA-F]+)$")
RE_ADDRESS = re.compile(r"^[0-9A-Za-z]+:([0-9a-fA-F]+)$")

_ACTIVE: Optional["MillAnnotations"] = None


def parse_usage(raw: Any) -> Dict[str, int]:
    if not isinstance(raw, dict):
        return zero_usage()
    pin = int(raw.get("in") or 0)
    pout = int(raw.get("out") or 0)
    tot = int(raw.get("total") or 0)
    if tot == 0:
        tot = pin + pout
    return {"in": pin, "out": pout, "total": tot}


def off_from_entry(entry: Dict[str, Any]) -> Optional[int]:
    rom = entry.get("romOffset")
    if isinstance(rom, str):
        m = RE_ROM_OFFSET.match(rom.strip())
        if m:
            return int(m.group(1), 16)
    addr = entry.get("address")
    if isinstance(addr, str):
        m = RE_ADDRESS.match(addr.strip())
        if m:
            return int(m.group(1), 16)
    return None


class MillAnnotations:
    def __init__(self, path: Path, document: Dict[str, Any]) -> None:
        self.path = path
        self.format = str(document.get("format") or "")
        self.version = int(document.get("version") or 0)
        self.rom_key = str(document.get("romKey") or "")
        self.entries: List[Dict[str, Any]] = list(document.get("entries") or [])
        self.by_off: Dict[int, Dict[str, Any]] = {}
        for entry in self.entries:
            off = off_from_entry(entry)
            if off is not None:
                self.by_off[off] = entry
        self.document_usage = parse_usage(document.get("tokenUsage"))

    def token_usage(self) -> Dict[str, int]:
        if any(int(self.document_usage.get(k) or 0) for k in ("in", "out", "total")):
            return dict(self.document_usage)
        tot = zero_usage()
        for entry in self.entries:
            tot = add_usage(tot, parse_usage(entry.get("tokenUsage")))
        return tot

    def format_token_line(self, role: str = "mill-annotations.json") -> str:
        from g3_lock import format_tokens

        return format_tokens("apple_fm", self.token_usage(), role)

    @classmethod
    def load(cls, path: Path) -> "MillAnnotations":
        path = Path(path)
        data = json.loads(path.read_text())
        if not isinstance(data, dict):
            raise ValueError("mill-annotations root must be an object")
        fmt = data.get("format")
        if fmt and fmt != FORMAT_ID:
            raise ValueError("unexpected format %r (want %s)" % (fmt, FORMAT_ID))
        return cls(path, data)

    def entry_at(self, off: int) -> Optional[Dict[str, Any]]:
        return self.by_off.get(int(off))

    def blocks_skip_68k(self, off: int) -> bool:
        entry = self.entry_at(off)
        if not entry:
            return False
        action = str(entry.get("action") or "")
        kind = str(entry.get("kind") or "")
        if action in ("revert", "keep"):
            return True
        if kind in ("protected", "uiPath"):
            return True
        if action == "investigate":
            return True
        return False

    def skip_candidate_offs(self) -> List[int]:
        out: List[int] = []
        seen = set()
        for entry in self.entries:
            if str(entry.get("action") or "") != "skipCandidate":
                continue
            off = off_from_entry(entry)
            if off is None or off in seen:
                continue
            seen.add(off)
            out.append(off)
        return out

    def format_pack_section(self, limit: int = 24) -> str:
        if not self.entries:
            return ""
        lines = [
            "## NewWorldView annotations",
            "",
            "- source: `%s`" % self.path,
            "- romKey: `%s`" % (self.rom_key or "-"),
            "- approved entries: `%s`" % len(self.entries),
            "",
        ]
        skip = self.skip_candidate_offs()
        if skip:
            lines.append("- skipCandidate offsets: `%s`" % ", ".join("0x%x" % o for o in skip[:limit]))
        blocked = [off for off in sorted(self.by_off) if self.blocks_skip_68k(off)]
        if blocked:
            lines.append(
                "- blocked offsets: `%s`"
                % ", ".join("0x%x" % o for o in blocked[:limit])
            )
        lines.append("")
        lines.append("| off | action | kind | symbol |")
        lines.append("|-----|--------|------|--------|")
        for entry in self.entries[:limit]:
            off = off_from_entry(entry)
            off_s = "0x%x" % off if off is not None else "-"
            sym = entry.get("symbol") or "-"
            lines.append(
                "| %s | %s | %s | %s |"
                % (
                    off_s,
                    entry.get("action") or "-",
                    entry.get("kind") or "-",
                    sym,
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


def load_annotations(path: Path) -> MillAnnotations:
    global _ACTIVE
    _ACTIVE = MillAnnotations.load(path)
    return _ACTIVE


def clear_annotations() -> None:
    global _ACTIVE
    _ACTIVE = None


def active() -> Optional[MillAnnotations]:
    global _ACTIVE
    if _ACTIVE is None:
        env = os.environ.get("G3_ANNOTATIONS", "").strip()
        if env:
            p = Path(env)
            if p.is_file():
                load_annotations(p)
    return _ACTIVE


def resolve_path(explicit: Optional[str] = None) -> Optional[Path]:
    raw = (explicit or os.environ.get("G3_ANNOTATIONS", "")).strip()
    if not raw:
        return None
    p = Path(raw)
    return p if p.is_file() else None
