#!/usr/bin/env python3
"""25 leftover PEF mills after KEEP 17068: sfmt/cfmt/pfmt TOC, GetHandleSize,
CurResFile 4, document STR#/TEXT ids. Do not mill skip-68k."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

# After-enter TOC plants (toc=0x10115000). 0x908+off in PEF r29.
# 0x2b0 -> toc+0xbb8 sfmt+2. 0x9c -> toc+0x9a4 sfmt file.
# 0x9e -> toc+0x9a6 pfmt file. 0x2ac -> toc+0xbb4 cfmt file.
_TOC2: List[Tuple[str, str, int, int]] = [
    ("pef-toc2b0", "toc2b0", 0x0BB8, 0x0101),
    ("pef-toc9a6", "toc9a6", 0x09A6, 0x0004),
    ("pef-toc9a4", "toc9a4", 0x09A4, 0x0003),
    ("pef-tocbb4", "tocbb4", 0x0BB4, 0x0004),
    ("pef-tocbb2", "tocbb2", 0x0BB2, 0x0001),
    ("pef-tocc48", "tocc48", 0x0C48, 1234),
    ("pef-tocc4a", "tocc4a", 0x0C4A, 1050),
    ("pef-tocc4c", "tocc4c", 0x0C4C, 3502),
    ("pef-toc3500", "toc3500", 0x0C4E, 3500),
    ("pef-toc4020", "toc4020", 0x0C46, 4020),
    ("pef-tocbb6", "tocbb6", 0x0BB6, 0x0004),
    ("pef-toc9a8", "toc9a8", 0x09A8, 0x0004),
    ("pef-toc9aa", "toc9aa", 0x09AA, 0x0004),
    ("pef-toc2aa", "toc2aa", 0x0BB2, 0x0002),
    ("pef-toc128", "toc128", 0x0C44, 128),
    ("pef-toc510", "toc510", 0x0C42, 510),
    ("pef-toc519", "toc519", 0x0C40, 519),
    ("pef-toc147", "toc147", 0x0C3E, 147),
    ("pef-toc1000", "toc1000", 0x0C3C, 1000),
    ("pef-toc701", "toc701", 0x0C3A, 701),
]
_SPECIAL: List[Tuple[str, str]] = [
    ("pef-hsz4", "hsz4"),
    ("pef-crf4", "crf4"),
    ("pef-urf4", "urf4"),
    ("pef-hvol0", "hvol0"),
    ("pef-res0", "res0"),
]
_HOSTS: List[Tuple[str, str]] = [(k, tok) for k, tok, _, _ in _TOC2] + _SPECIAL
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_FMT_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_TOC_WR: Dict[str, Tuple[int, int]] = {k: (off, val) for k, _, off, val in _TOC2}
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_fmt(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_FMT_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_fmt(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_fmt(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_hsz(text: str) -> str:
    m = _MARKERS["pef-hsz4"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 28u) {\n"
        "		uint32 p = 0;\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			p = vm_read_memory_4(a3);\n"
        "		r3 = p ? 64u : 0;\n"
    )
    new = (
        "	} else if (idx == 28u) {\n"
        "		uint32 p = 0;\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			p = vm_read_memory_4(a3);\n"
        "		r3 = 0;\n"
        "		if (p && g3_ea_data(p - 1u))\n"
        "			r3 = vm_read_memory_4(p - 4u);\n"
        "		if (!r3 && p)\n"
        "			r3 = 64u;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-hsz count=%s" % n)
    text = text.replace(old, new, 1)
    log = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize n=%u\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    log_new = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize n=%u\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        + _log_enter(m, "nhsz4")
    )
    if log not in text:
        raise ValueError("mill patch missing: cpu-pef-hsz-log")
    return text.replace(log, log_new, 1)


def patch_cpu_pef_crf4(text: str) -> str:
    m = _MARKERS["pef-crf4"]
    if m in text:
        return text
    old = "		if (idx == 270u || idx == 195u)\n			r3 = 3;\n"
    if old not in text:
        old = (
            "		if (idx == 270u || idx == 195u) {\n"
            "			r3 = 4;\n"
        )
        if old in text:
            return text
        raise ValueError("mill patch missing: cpu-pef-crf4")
    new = (
        "		if (idx == 270u || idx == 195u) {\n"
        "			r3 = 4;\n"
        + _log_enter(m, "ncrf4")
        + "		} else if (idx == 250u) {\n"
    )
    # old chain: if 270 r3=3; else if 250
    old = (
        "		if (idx == 270u || idx == 195u)\n"
        "			r3 = 3;\n"
        "		else if (idx == 250u)\n"
    )
    new = (
        "		if (idx == 270u || idx == 195u) {\n"
        "			r3 = 4;\n"
        + _log_enter(m, "ncrf4")
        + "		} else if (idx == 250u)\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-crf4 count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_urf4(text: str) -> str:
    m = _MARKERS["pef-urf4"]
    if m in text:
        return text
    return _insert_after_enter(text, _log_enter(m, "nurf4"), "cpu-pef-urf4")


def patch_cpu_pef_hvol0(text: str) -> str:
    m = _MARKERS["pef-hvol0"]
    if m in text:
        return text
    return _insert_after_enter(text, _log_enter(m, "nhvol0"), "cpu-pef-hvol0")


def patch_cpu_pef_res0(text: str) -> str:
    m = _MARKERS["pef-res0"]
    if m in text:
        return text
    return _insert_after_enter(text, _log_enter(m, "nres0"), "cpu-pef-res0")


def apply_pef_fmtq(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind in _TOC_WR:
        m = _MARKERS[kind]
        if m in text:
            return
        cpu.write_text(_insert_after_enter(text, _toc2_block(kind), "cpu-" + kind))
        return
    if kind == "pef-hsz4":
        cpu.write_text(patch_cpu_pef_hsz(text))
        return
    if kind == "pef-crf4":
        cpu.write_text(patch_cpu_pef_crf4(text))
        return
    if kind == "pef-urf4":
        cpu.write_text(patch_cpu_pef_urf4(text))
        return
    if kind == "pef-hvol0":
        cpu.write_text(patch_cpu_pef_hvol0(text))
        return
    if kind == "pef-res0":
        cpu.write_text(patch_cpu_pef_res0(text))
        return
    raise ValueError("unknown pef fmt kind %s" % kind)
