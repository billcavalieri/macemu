#!/usr/bin/env python3
"""25 leftover PEF mills after KEEP 17023: reach splash from glue, restore
ShowWindow jump, remaining NRD bls, splash-path skips/intercepts.
Do not remill leftover:pef-gluesw / gluedlg / nrdlr / skipgest / skipglue.
Do not mill skip-68k. Do not skip GetNewDialog. Do not write 0x30 (skipglue)."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_NRD_OFFS = [
    0x8D78,
    0x8D94,
    0x8DB0,
    0x8DD0,
    0x8DF0,
    0x8E0C,
    0x8E28,
    0x8E44,
    0x8E60,
    0x8E80,
    0x8E9C,
    0x8EB8,
]
assert len(_NRD_OFFS) == 12, len(_NRD_OFFS)

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-gluespl", None, "glueSpl"),
    ("pef-resgndsw", None, "resGndSw"),
    ("pef-noeq", None, "noEq"),
    ("pef-swlr", None, "swLR"),
    ("pef-drawlr2", None, "drawLR2"),
    ("pef-skipsetd", None, "skipSetd"),
    ("pef-skipurf5", None, "skipUrf5"),
    ("pef-skipsw2", None, "skipSw2"),
    ("pef-skipdd2", None, "skipDd2"),
    ("pef-skipg2", None, "skipG2"),
    ("pef-glue2sp", None, "glue2Sp"),
    ("pef-aftersw", None, "afterSw"),
    ("pef-forceeq", None, "forceEq"),
]
for i, _off in enumerate(_NRD_OFFS):
    _HOSTS.append(("pef-skipn%02d" % (20 + i), None, "skipN%02d" % (20 + i)))
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_REST_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}

# After-enter so skipcrf/gnddlg cannot overwrite.
_AFTER: Dict[str, Tuple[int, int]] = {
    "pef-gluespl": (0x0038, 0x480000A8),
    "pef-resgndsw": (0x0974, 0x48000194),
    "pef-noeq": (0x003C, 0x60000000),
    "pef-glue2sp": (0x0040, 0x480000A0),
    "pef-forceeq": (0x003C, 0x4800001C),
}
_BEFORE: Dict[str, Tuple[int, int]] = {
    "pef-skipsetd": (0x0A64, 0x48000004),
    "pef-skipurf5": (0x095C, 0x48000004),
    "pef-skipsw2": (0x0B08, 0x48000004),
    "pef-skipdd2": (0x0B28, 0x48000004),
    "pef-skipg2": (0x0040, 0x48000004),
    "pef-aftersw": (0x0B0C, 0x4800001C),
}
for i, off in enumerate(_NRD_OFFS):
    _BEFORE["pef-skipn%02d" % (20 + i)] = (off, 0x48000004)

_ENTER = "	g3_did_pef_enter = 1;\n"
_IDX251 = (
    "				if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
    "					pc() = 0x1010b240u;\n"
)
_IDX173 = (
    "				if (idx == 173u && (lr() == pc() || lr() == 0)) {\n"
    "					pc() = 0x10101ee0u;\n"
)


def next_pef_rest(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_REST_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_rest(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_rest(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


def _insert(text: str, needle: str, block: str, label: str, after: bool) -> str:
    n = text.count(needle)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    if after:
        return text.replace(needle, needle + block, 1)
    return text.replace(needle, block + needle, 1)


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


def _skip_block(kind: str, off: int, insn: int) -> str:
    m = _MARKERS[kind]
    var = "n%s" % kind.replace("pef-", "").replace("-", "")
    return (
        "	if (g3_ea_data(ent + 0x%xu))\n" % (off + 3)
        + "		vm_write_memory_4(ent + 0x%xu, 0x%08xu);\n" % (off, insn)
        + _log_enter(m, var)
    )


def patch_cpu_pef_swlr(text: str) -> str:
    m = _MARKERS["pef-swlr"]
    if m in text:
        return text
    new = (
        "				if (idx == 173u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x10101ee0u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nswl;\n"
        "						if (nswl < 8) {\n"
        "							nswl++;\n"
        "							nw_boot_log(\n"
        "								\"%s\");\n" % m
        + "						}\n"
        "					}\n"
        "#endif\n"
        "				} else if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010b240u;\n"
    )
    n = text.count(_IDX251)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-swlr count=%s" % n)
    return text.replace(_IDX251, new, 1)


def patch_cpu_pef_drawlr2(text: str) -> str:
    m = _MARKERS["pef-drawlr2"]
    if m in text:
        return text
    log = (
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned ndl2;\n"
        "						if (ndl2 < 8) {\n"
        "							ndl2++;\n"
        "							nw_boot_log(\n"
        "								\"%s\");\n" % m
        + "						}\n"
        "					}\n"
        "#endif\n"
    )
    if _IDX173 in text:
        old = _IDX173
        new = (
            "				if (idx == 155u && (lr() == pc() || lr() == 0)) {\n"
            "					pc() = 0x10101f00u;\n"
            + log
            + "				} else if (idx == 173u && (lr() == pc() || lr() == 0)) {\n"
            "					pc() = 0x10101ee0u;\n"
        )
    else:
        old = _IDX251
        new = (
            "				if (idx == 155u && (lr() == pc() || lr() == 0)) {\n"
            "					pc() = 0x10101f00u;\n"
            + log
            + "				} else if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
            "					pc() = 0x1010b240u;\n"
        )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-drawlr2 count=%s" % n)
    return text.replace(old, new, 1)


def apply_pef_restq(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind == "pef-swlr":
        cpu.write_text(patch_cpu_pef_swlr(text))
        return
    if kind == "pef-drawlr2":
        cpu.write_text(patch_cpu_pef_drawlr2(text))
        return
    m = _MARKERS.get(kind)
    if m is None:
        raise ValueError("unknown pef rest kind %s" % kind)
    if m in text:
        return
    if kind in _AFTER:
        off, insn = _AFTER[kind]
        cpu.write_text(
            _insert(text, _ENTER, _skip_block(kind, off, insn), "cpu-" + kind, True)
        )
        return
    if kind in _BEFORE:
        off, insn = _BEFORE[kind]
        cpu.write_text(
            _insert(text, _ENTER, _skip_block(kind, off, insn), "cpu-" + kind, False)
        )
        return
    raise ValueError("unknown pef rest kind %s" % kind)
