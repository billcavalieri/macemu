#!/usr/bin/env python3
"""25 leftover PEF mills after KEEP 16966: skip remaining NewRoutineDescriptor
bls (idx 159) plus splash-after TickCount/Gestalt/Get1Resource.
Do not remill leftover:pef-skipbl13..32 / skipglue / skipunld / stublr.
Do not skip GetNewDialog/ShowWindow/DrawDialog (splash 510 already live)."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Unused live bls from mill 16966 hostLR2 (ent=101013d0). Not 10101400
# (skipglue REVERT) and not already-milled skipbl/skiph2h offs.
_NRD_OFFS = [
    0x8B14,
    0x8B30,
    0x8B4C,
    0x8B68,
    0x8B84,
    0x8BA0,
    0x8BBC,
    0x8BDC,
    0x8BFC,
    0x8C18,
    0x8C50,
    0x8C70,
    0x8C8C,
    0x8CA8,
    0x8CC8,
    0x8CE4,
    0x8D00,
    0x8D1C,
    0x8D38,
    0x8D58,
]
assert len(_NRD_OFFS) == 20, len(_NRD_OFFS)

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-skipnrd", None, "skipNrd"),
    ("pef-nrdlr", None, "nrdLR"),
    ("pef-skipgest", None, "skipGest"),
    ("pef-skiptck2", None, "skipTck2"),
    ("pef-skipg1s", None, "skipG1s"),
]
for i, _off in enumerate(_NRD_OFFS):
    _HOSTS.append(("pef-skipn%02d" % i, None, "skipN%02d" % i))
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_NRD_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

# kind -> (ent_off, insn). skipnrd branches over remaining NRD cluster
# 10109ee4 -> 1010a28c (0x8b14 + 0x3a8).
_SKIP_OFF: Dict[str, Tuple[int, int]] = {
    "pef-skipnrd": (0x8B14, 0x480003A8),
    "pef-skipgest": (0x0BA4, 0x48000004),
    "pef-skiptck2": (0x0B10, 0x48000004),
    "pef-skipg1s": (0x09E0, 0x48000004),
}
for i, off in enumerate(_NRD_OFFS):
    _SKIP_OFF["pef-skipn%02d" % i] = (off, 0x48000004)

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_nrd(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_NRD_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_nrd(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_nrd(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


def _insert_before_void(text: str, block: str, label: str) -> str:
    n = text.count(_ANCHOR)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(_ANCHOR, _ANCHOR + block, 1)


def _insert_before_enter(text: str, block: str, label: str) -> str:
    n = text.count(_ENTER)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(_ENTER, block + _ENTER, 1)


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


def patch_cpu_pef_nrdlr(text: str) -> str:
    m = _MARKERS["pef-nrdlr"]
    if m in text:
        return text
    old = (
        "				if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010b240u;\n"
    )
    new = (
        "				if (idx == 159u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a28cu;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nnl;\n"
        "						if (nnl < 8) {\n"
        "							nnl++;\n"
        "							nw_boot_log(\n"
        "								\"%s\");\n" % m
        + "						}\n"
        "					}\n"
        "#endif\n"
        "				} else if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010b240u;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-nrdlr count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_skip(text: str, kind: str) -> str:
    m = _MARKERS[kind]
    if m in text:
        return text
    off, insn = _SKIP_OFF[kind]
    var = "n%s" % kind.replace("pef-", "").replace("-", "")
    block = (
        "	if (g3_ea_data(ent + 0x%xu))\n" % (off + 3)
        + "		vm_write_memory_4(ent + 0x%xu, 0x%08xu);\n" % (off, insn)
        + _log_enter(m, var)
    )
    return _insert_before_enter(text, block, "cpu-" + kind)


def apply_pef_nrdq(kind: str, cpu: Path) -> None:
    if kind == "pef-nrdlr":
        cpu.write_text(patch_cpu_pef_nrdlr(cpu.read_text()))
        return
    if kind in _SKIP_OFF:
        cpu.write_text(patch_cpu_pef_skip(cpu.read_text(), kind))
        return
    raise ValueError("unknown pef nrd kind %s" % kind)
