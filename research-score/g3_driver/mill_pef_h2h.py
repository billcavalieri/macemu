#!/usr/bin/env python3
"""50 leftover PEF mills: skip last HandToHand bl at 1010b238, then more
call-site skips + remaining unhosted idxs 274/279/280/281/286.
Do not remill leftover:pef-stublr / skipunld / unldlr / trackgo / skipglue.
Do not remill leftover:pef-iNNN voids."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

# (kind, idx or None, marker token after "PEF ")
_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-skiph2h", None, "skipH2h"),
    ("pef-skiph2h2", None, "skipH2h2"),
    ("pef-h2hlr", None, "h2hLR"),
    ("pef-skipg1r", None, "skipG1r"),
    ("pef-skiphsz2", None, "skipHsz2"),
    ("pef-skipbm", None, "skipBm"),
    ("pef-skipdh", None, "skipDh"),
    ("pef-skipb014", None, "skipB014"),
    ("pef-skipb020", None, "skipB020"),
    ("pef-skipb074", None, "skipB074"),
    ("pef-skipb080", None, "skipB080"),
    ("pef-skipb08c", None, "skipB08c"),
    ("pef-resload", 274, "LMGetResLoad"),
    ("pef-drvq", 279, "GetDrvQHdr"),
    ("pef-prerr", 280, "PrError"),
    ("pef-insetr", 281, "InsetRect"),
    ("pef-openda", 286, "OpenDeskAcc"),
]
# Confirmed live bl sites from mill 16936 hostLR2 (ent=101013d0). Not 10101400
# (skipglue REVERT) and not splash 10101dxx.
_SKIP_EXTRA_OFFS = [
    0x9C0C,
    0x9C18,
    0x9C24,
    0x9B54,
    0x9B60,
    0x9B6C,
    0x9B8C,
    0x9A8C,
    0x9A98,
    0x9AA4,
    0x9AC4,
    0x99CC,
    0x99EC,
    0x99F8,
    0x9888,
    0x9894,
    0x98B4,
    0x9458,
    0x94E0,
    0x91DC,
    0x91EC,
    0x8A30,
    0x8A3C,
    0x74DC,
    0x74E8,
    0x8C34,
    0x8ED4,
    0x8A6C,
    0x8A88,
    0x8AA4,
    0x8AC0,
    0x8ADC,
    0x8AF8,
]
assert len(_SKIP_EXTRA_OFFS) == 33, len(_SKIP_EXTRA_OFFS)
for i, off in enumerate(_SKIP_EXTRA_OFFS):
    _HOSTS.append(("pef-skipbl%02d" % i, None, "skipBl%02d" % i))
assert len(_HOSTS) == 50, len(_HOSTS)

PEF_H2H_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

# kind -> (ent_off, insn). skiph2h2 branches +8 over the HandToHand bl.
_SKIP_OFF: Dict[str, Tuple[int, int]] = {
    "pef-skiph2h": (0x9E68, 0x48000004),
    "pef-skiph2h2": (0x9E64, 0x48000008),
    "pef-skipg1r": (0x9540, 0x48000004),
    "pef-skiphsz2": (0x98E0, 0x48000004),
    "pef-skipbm": (0x98F8, 0x48000004),
    "pef-skipdh": (0x9904, 0x48000004),
    "pef-skipb014": (0x9C44, 0x48000004),
    "pef-skipb020": (0x9C50, 0x48000004),
    "pef-skipb074": (0x9CA4, 0x48000004),
    "pef-skipb080": (0x9CB0, 0x48000004),
    "pef-skipb08c": (0x9CBC, 0x48000004),
}
for i, off in enumerate(_SKIP_EXTRA_OFFS):
    _SKIP_OFF["pef-skipbl%02d" % i] = (off, 0x48000004)

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_h2h(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_H2H_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_h2h(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_h2h(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_h2hlr(text: str) -> str:
    m = _MARKERS["pef-h2hlr"]
    if m in text:
        return text
    old = (
        "				if (idx == 79u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a954u;\n"
    )
    new = (
        "				if (idx == 251u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010b240u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nhl;\n"
        "						if (nhl < 8) {\n"
        "							nhl++;\n"
        "							nw_boot_log(\n"
        "								\"%s\");\n" % m
        + "						}\n"
        "					}\n"
        "#endif\n"
        "				} else if (idx == 79u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a954u;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-h2hlr count=%s" % n)
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


def _host_block(idx: int, marker: str) -> str:
    return (
        "	if (idx == %uu) {\n" % idx
        + "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"%s\");\n" % marker
        + "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )


def apply_pef_h2h(kind: str, cpu: Path) -> None:
    if kind == "pef-h2hlr":
        cpu.write_text(patch_cpu_pef_h2hlr(cpu.read_text()))
        return
    if kind in _SKIP_OFF:
        cpu.write_text(patch_cpu_pef_skip(cpu.read_text(), kind))
        return
    idx = _IDX.get(kind)
    marker = _MARKERS.get(kind)
    if idx is None or marker is None:
        raise ValueError("unknown pef h2h kind %s" % kind)
    text = cpu.read_text()
    if marker in text:
        return
    cpu.write_text(_insert_before_void(text, _host_block(idx, marker), "cpu-" + kind))
