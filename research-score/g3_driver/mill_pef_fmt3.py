#!/usr/bin/env python3
"""25 leftover PEF mills after KEEP 17118: raise format-path log caps, always
log pfmt/sfmt/cfmt Count1, more TOC ids. Do not mill skip-68k.
Do not remill leftover:pef-crf4/hsz4/urf4/hvol0/res0."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_TOC2: List[Tuple[str, str, int, int]] = [
    ("pef-idd00", "idd00", 0x0D00, 3500),
    ("pef-idd02", "idd02", 0x0D02, 1),
    ("pef-idd04", "idd04", 0x0D04, 3502),
    ("pef-idd06", "idd06", 0x0D06, 4020),
    ("pef-idd08", "idd08", 0x0D08, 0x0101),
    ("pef-idd0a", "idd0a", 0x0D0A, 4),
    ("pef-idd0c", "idd0c", 0x0D0C, 128),
    ("pef-idd0e", "idd0e", 0x0D0E, 147),
    ("pef-idd10", "idd10", 0x0D10, 510),
    ("pef-idd12", "idd12", 0x0D12, 519),
    ("pef-idd14", "idd14", 0x0D14, 701),
    ("pef-idd16", "idd16", 0x0D16, 1050),
    ("pef-idd18", "idd18", 0x0D18, 1234),
    ("pef-idd1a", "idd1a", 0x0D1A, 1000),
    ("pef-idd1c", "idd1c", 0x0D1C, 3),
    ("pef-idd1e", "idd1e", 0x0D1E, 4500),
    ("pef-idd20", "idd20", 0x0D20, 6001),
    ("pef-idd22", "idd22", 0x0D22, 5000),
    ("pef-idd24", "idd24", 0x0D24, 0x0004),
    ("pef-idd26", "idd26", 0x0D26, 0x0101),
]
_SPECIAL: List[Tuple[str, str]] = [
    ("pef-cntfmt", "cntfmt"),
    ("pef-cnt256", "cnt256"),
    ("pef-g1256", "g1256"),
    ("pef-n2s256", "n2s256"),
    ("pef-gis256", "gis256"),
]
_HOSTS: List[Tuple[str, str]] = _SPECIAL + [(k, tok) for k, tok, _, _ in _TOC2]
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_FMT3_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_TOC_WR: Dict[str, Tuple[int, int]] = {k: (off, val) for k, _, off, val in _TOC2}
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_fmt3(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_FMT3_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_fmt3(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_fmt3(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_cntfmt(text: str) -> str:
    m = _MARKERS["pef-cntfmt"]
    if m in text:
        return text
    old = (
        "	if (idx == 222u) {\n"
        "		r3 = g3_res_count(a3);\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 16) {\n"
    )
    new = (
        "	if (idx == 222u) {\n"
        "		r3 = g3_res_count(a3);\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 16 || a3 == 0x70666d74u || a3 == 0x73666d74u ||\n"
        "			    a3 == 0x63666d74u) {\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-cntfmt count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ncntfmt"), "cpu-pef-cntfmt")


def patch_cpu_pef_cnt256(text: str) -> str:
    m = _MARKERS["pef-cnt256"]
    if m in text:
        return text
    old = "			if (n < 16 || a3 == 0x70666d74u || a3 == 0x73666d74u ||\n"
    new = "			if (n < 256 || a3 == 0x70666d74u || a3 == 0x73666d74u ||\n"
    if old not in text:
        old = "	if (idx == 222u) {\n		r3 = g3_res_count(a3);\n#if NW_BOOT_LOG\n		{\n			static unsigned n;\n			if (n < 16) {\n"
        new = "	if (idx == 222u) {\n		r3 = g3_res_count(a3);\n#if NW_BOOT_LOG\n		{\n			static unsigned n;\n			if (n < 256) {\n"
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-cnt256 count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ncnt256"), "cpu-pef-cnt256")


def patch_cpu_pef_g1256(text: str) -> str:
    m = _MARKERS["pef-g1256"]
    if m in text:
        return text
    old = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    # unique: bump the cap next to Get1Ind log
    oldc = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    # change if (n < 16) immediately before this snprintf in Get1Ind
    old = (
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 16) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    new = (
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 256) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-g1256 count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ng1256"), "cpu-pef-g1256")


def patch_cpu_pef_n2s256(text: str) -> str:
    m = _MARKERS["pef-n2s256"]
    if m in text:
        return text
    old = (
        "			static unsigned nns;\n"
        "			if (nns < 16) {\n"
        "				char lbuf[96];\n"
        "				nns++;\n"
        "				snprintf(lbuf, sizeof(lbuf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString n=%d\",\n"
    )
    new = (
        "			static unsigned nns;\n"
        "			if (nns < 256) {\n"
        "				char lbuf[96];\n"
        "				nns++;\n"
        "				snprintf(lbuf, sizeof(lbuf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString n=%d\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-n2s256 count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "nn2s256"), "cpu-pef-n2s256")


def patch_cpu_pef_gis256(text: str) -> str:
    m = _MARKERS["pef-gis256"]
    if m in text:
        return text
    old = (
        "			static unsigned ngi;\n"
        "			if (ngi < 16) {\n"
        "				char buf[96];\n"
        "				ngi++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString id=%d ix=%d\",\n"
    )
    new = (
        "			static unsigned ngi;\n"
        "			if (ngi < 256) {\n"
        "				char buf[96];\n"
        "				ngi++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString id=%d ix=%d\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-gis256 count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ngis256"), "cpu-pef-gis256")


def apply_pef_fmt3q(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind in _TOC_WR:
        m = _MARKERS[kind]
        if m in text:
            return
        cpu.write_text(_insert_after_enter(text, _toc2_block(kind), "cpu-" + kind))
        return
    fn = {
        "pef-cntfmt": patch_cpu_pef_cntfmt,
        "pef-cnt256": patch_cpu_pef_cnt256,
        "pef-g1256": patch_cpu_pef_g1256,
        "pef-n2s256": patch_cpu_pef_n2s256,
        "pef-gis256": patch_cpu_pef_gis256,
    }.get(kind)
    if fn is None:
        raise ValueError("unknown pef fmt3 kind %s" % kind)
    cpu.write_text(fn(text))
