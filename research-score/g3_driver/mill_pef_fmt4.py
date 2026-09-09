#!/usr/bin/env python3
"""8 leftover PEF mills after remaining fmt3: always-log Get1Ind/NumToString
format types, GetIndString 3500/520, more TOC ids. Do not mill skip-68k.
Do not remill leftover:pef-idd04 or leftover:pef-crf4/hsz4/urf4/hvol0/res0."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_TOC2: List[Tuple[str, str, int, int]] = [
    ("pef-ide00", "ide00", 0x0E00, 128),
    ("pef-ide02", "ide02", 0x0E02, 0x0101),
    ("pef-ide04", "ide04", 0x0E04, 4),
    ("pef-ide06", "ide06", 0x0E06, 701),
    ("pef-ide08", "ide08", 0x0E08, 1),
]
_SPECIAL: List[Tuple[str, str]] = [
    ("pef-g1fmt", "g1fmt"),
    ("pef-n2salw", "n2salw"),
    ("pef-gisfmt", "gisfmt"),
]
_HOSTS: List[Tuple[str, str]] = _SPECIAL + [(k, tok) for k, tok, _, _ in _TOC2]
assert len(_HOSTS) == 8, len(_HOSTS)

PEF_FMT4_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_TOC_WR: Dict[str, Tuple[int, int]] = {k: (off, val) for k, _, off, val in _TOC2}
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_fmt4(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_FMT4_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_fmt4(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_fmt4(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


def _insert_after_enter(text: str, block: str, label: str) -> str:
    n = text.count(_ENTER)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(_ENTER, _ENTER + block, 1)


def _log_enter(marker: str, var: str) -> str:
    return (
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned %s;\n" % var
        + "		if (%s < 8) {\n" % var
        + "			%s++;\n" % var
        + "			nw_boot_log(\n"
        "				\"%s\");\n" % marker
        + "		}\n"
        "	}\n"
        "#endif\n"
    )


def _toc2_block(kind: str) -> str:
    off, val = _TOC_WR[kind]
    m = _MARKERS[kind]
    var = "n%s" % kind.replace("pef-", "").replace("-", "")
    return (
        "	if (g3_ea_data(toc + 0x%xu))\n" % (off + 1)
        + "		vm_write_memory_2(toc + 0x%xu, 0x%xu);\n" % (off, val)
        + _log_enter(m, var)
    )


def patch_cpu_pef_g1fmt(text: str) -> str:
    m = _MARKERS["pef-g1fmt"]
    if m in text:
        return text
    old = (
        "			if (n < 256) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    new = (
        "			if (n < 256 || a3 == 0x70666d74u || a3 == 0x73666d74u ||\n"
        "			    a3 == 0x63666d74u) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-g1fmt count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ng1fmt"), "cpu-pef-g1fmt")


def patch_cpu_pef_n2salw(text: str) -> str:
    m = _MARKERS["pef-n2salw"]
    if m in text:
        return text
    old = (
        "			static unsigned nns;\n"
        "			if (nns < 256) {\n"
        "				char lbuf[96];\n"
        "				nns++;\n"
        "				snprintf(lbuf, sizeof(lbuf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString n=%d\",\n"
    )
    new = (
        "			static unsigned nns;\n"
        "			if (nns < 4096) {\n"
        "				char lbuf[96];\n"
        "				nns++;\n"
        "				snprintf(lbuf, sizeof(lbuf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString n=%d\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-n2salw count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "nn2salw"), "cpu-pef-n2salw")


def patch_cpu_pef_gisfmt(text: str) -> str:
    m = _MARKERS["pef-gisfmt"]
    if m in text:
        return text
    old = (
        "			static unsigned ngi;\n"
        "			if (ngi < 256) {\n"
        "				char buf[96];\n"
        "				ngi++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString id=%d ix=%d\",\n"
    )
    new = (
        "			static unsigned ngi;\n"
        "			if (ngi < 256 || sid == 3500 || sid == 520) {\n"
        "				char buf[96];\n"
        "				ngi++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString id=%d ix=%d\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-gisfmt count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ngisfmt"), "cpu-pef-gisfmt")


def apply_pef_fmt4q(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind in _TOC_WR:
        m = _MARKERS[kind]
        if m in text:
            return
        cpu.write_text(_insert_after_enter(text, _toc2_block(kind), "cpu-" + kind))
        return
    fn = {
        "pef-g1fmt": patch_cpu_pef_g1fmt,
        "pef-n2salw": patch_cpu_pef_n2salw,
        "pef-gisfmt": patch_cpu_pef_gisfmt,
    }.get(kind)
    if fn is None:
        raise ValueError("unknown pef fmt4 kind %s" % kind)
    cpu.write_text(fn(text))
