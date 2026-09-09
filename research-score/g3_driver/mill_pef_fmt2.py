#!/usr/bin/env python3
"""25 leftover PEF mills after KEEP 17088: sync pfmt version word, sfmt store,
more TOC ids. Do not remill leftover:pef-crf4/hsz4/urf4/hvol0/res0. Do not mill skip-68k."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_TOC2: List[Tuple[str, str, int, int]] = [
    ("pef-idbe0", "idbe0", 0x0BE0, 3500),
    ("pef-idbe2", "idbe2", 0x0BE2, 1),
    ("pef-idbe4", "idbe4", 0x0BE4, 3502),
    ("pef-idbe6", "idbe6", 0x0BE6, 4020),
    ("pef-idbe8", "idbe8", 0x0BE8, 147),
    ("pef-idbea", "idbea", 0x0BEA, 128),
    ("pef-idbec", "idbec", 0x0BEC, 510),
    ("pef-idbee", "idbee", 0x0BEE, 519),
    ("pef-idbf0", "idbf0", 0x0BF0, 701),
    ("pef-idbf2", "idbf2", 0x0BF2, 1050),
    ("pef-idbf4", "idbf4", 0x0BF4, 1234),
    ("pef-idbf6", "idbf6", 0x0BF6, 1000),
    ("pef-idbf8", "idbf8", 0x0BF8, 0x0101),
    ("pef-idbfa", "idbfa", 0x0BFA, 4),
    ("pef-idbfc", "idbfc", 0x0BFC, 3),
    ("pef-idc00", "idc00", 0x0C00, 3500),
    ("pef-idc02", "idc02", 0x0C02, 1),
    ("pef-idc04", "idc04", 0x0C04, 3502),
    ("pef-idc06", "idc06", 0x0C06, 4020),
    ("pef-idc08", "idc08", 0x0C08, 0x0101),
]
_SPECIAL: List[Tuple[str, str]] = [
    ("pef-g1sync", "g1sync"),
    ("pef-sfmtld", "sfmtld"),
    ("pef-g1wlog", "g1wlog"),
    ("pef-pfmt2", "pfmt2"),
    ("pef-bb8w", "bb8w"),
]
_HOSTS: List[Tuple[str, str]] = _SPECIAL + [(k, tok) for k, tok, _, _ in _TOC2]
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_FMT2_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_TOC_WR: Dict[str, Tuple[int, int]] = {k: (off, val) for k, _, off, val in _TOC2}
_ENTER = "	g3_did_pef_enter = 1;\n"
_PFMT_PLANT = (
    "		if (a3 == 0x70666d74u && ix == 1) {\n"
    "			static const uint8 pfmt[4] = { 1, 1, 0, 4 };\n"
    "			r3 = g3_plant_raw(pfmt, 4u);\n"
    "		} else if (g3_res_nth(a3, ix, &doff, &ln) && ln)\n"
)


def next_pef_fmt2(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_FMT2_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_fmt2(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_fmt2(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_g1sync(text: str) -> str:
    m = _MARKERS["pef-g1sync"]
    if m in text:
        return text
    old = _PFMT_PLANT
    new = (
        "		if (a3 == 0x70666d74u && ix == 1) {\n"
        "			static const uint8 pfmt[4] = { 1, 1, 0, 4 };\n"
        "			r3 = g3_plant_raw(pfmt, 4u);\n"
        "			if (g3_ea_data(0x10115bb9u))\n"
        "				vm_write_memory_2(0x10115bb8u, 0x0101u);\n"
        "		} else if (g3_res_nth(a3, ix, &doff, &ln) && ln)\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-g1sync count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ng1sync"), "cpu-pef-g1sync")


def patch_cpu_pef_sfmtld(text: str) -> str:
    m = _MARKERS["pef-sfmtld"]
    if m in text:
        return text
    old = (
        "		} else if (g3_res_nth(a3, ix, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    new = (
        "		} else if (g3_res_nth(a3, ix, &doff, &ln) && ln) {\n"
        "			r3 = g3_res_plant(doff, ln);\n"
        "			if (a3 == 0x73666d74u && r3 && g3_ea_data(r3 + 3u)) {\n"
        "				uint32 p = vm_read_memory_4(r3);\n"
        "				if (p && g3_ea_data(p + 3u) && g3_ea_data(0x10115bb9u))\n"
        "					vm_write_memory_2(0x10115bb8u,\n"
        "							  vm_read_memory_2(p + 2u));\n"
        "			}\n"
        "		}\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-sfmtld count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "nsfmtld"), "cpu-pef-sfmtld")


def patch_cpu_pef_g1wlog(text: str) -> str:
    m = _MARKERS["pef-g1wlog"]
    if m in text:
        return text
    old = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x\",\n"
        "					 (unsigned)a3, (int)ix, (unsigned)r3);\n"
    )
    new = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF Get1IndResource ty=%08x ix=%d r3=%08x w=%04x\",\n"
        "					 (unsigned)a3, (int)ix, (unsigned)r3,\n"
        "					 (unsigned)(r3 && g3_ea_data(r3 + 3u) &&\n"
        "					  g3_ea_data(vm_read_memory_4(r3) + 1u) ?\n"
        "					  vm_read_memory_2(vm_read_memory_4(r3)) : 0));\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-g1wlog count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "ng1wlog"), "cpu-pef-g1wlog")


def patch_cpu_pef_pfmt2(text: str) -> str:
    m = _MARKERS["pef-pfmt2"]
    if m in text:
        return text
    old = "			static const uint8 pfmt[4] = { 1, 1, 0, 4 };\n			r3 = g3_plant_raw(pfmt, 4u);\n"
    new = "			static const uint8 pfmt[2] = { 1, 1 };\n			r3 = g3_plant_raw(pfmt, 2u);\n"
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-pfmt2 count=%s" % n)
    text = text.replace(old, new, 1)
    return _insert_after_enter(text, _log_enter(m, "npfmt2"), "cpu-pef-pfmt2")


def patch_cpu_pef_bb8w(text: str) -> str:
    m = _MARKERS["pef-bb8w"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(0x10115bb9u))\n"
        "		vm_write_memory_2(0x10115bb8u, 0x0101u);\n"
        + _log_enter(m, "nbb8w")
    )
    return _insert_after_enter(text, block, "cpu-pef-bb8w")


def apply_pef_fmt2q(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind in _TOC_WR:
        m = _MARKERS[kind]
        if m in text:
            return
        cpu.write_text(_insert_after_enter(text, _toc2_block(kind), "cpu-" + kind))
        return
    fn = {
        "pef-g1sync": patch_cpu_pef_g1sync,
        "pef-sfmtld": patch_cpu_pef_sfmtld,
        "pef-g1wlog": patch_cpu_pef_g1wlog,
        "pef-pfmt2": patch_cpu_pef_pfmt2,
        "pef-bb8w": patch_cpu_pef_bb8w,
    }.get(kind)
    if fn is None:
        raise ValueError("unknown pef fmt2 kind %s" % kind)
    cpu.write_text(fn(text))
