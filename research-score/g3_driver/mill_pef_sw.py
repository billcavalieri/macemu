#!/usr/bin/env python3
"""5 leftover PEF mills after KEEP 17017: undo skipcrf (it dropped ShowWindow),
jump glue-return to ShowWindow/DrawDialog, skip MoveWindow/SizeWindow.
Patches run AFTER g3_did_pef_enter so skipcrf cannot overwrite uncrf.
Do not remill leftover:pef-nrdlr / skipgest / skipglue / skip-68k 0x1372.
Do not skip GetNewDialog. Do not mill skip-68k."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-uncrf", None, "unCrf"),
    ("pef-gluesw", None, "glueSw"),
    ("pef-gluedlg", None, "glueDlg"),
    ("pef-skipmv", None, "skipMv"),
    ("pef-skipsz2", None, "skipSz2"),
]
assert len(_HOSTS) == 5, len(_HOSTS)

PEF_SW_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}

# ent=101013d0. uncrf nops skipcrf's b+4 at CurResFile. glue return 10101404
# -> ShowWindow 10101ed8 (+0xad4) or DrawDialog 10101ef8 (+0xaf4).
_SKIP_OFF: Dict[str, Tuple[int, int]] = {
    "pef-uncrf": (0x094C, 0x60000000),
    "pef-gluesw": (0x0034, 0x48000AD4),
    "pef-gluedlg": (0x0034, 0x48000AF4),
    "pef-skipmv": (0x0AFC, 0x48000004),
    "pef-skipsz2": (0x0AA4, 0x48000004),
}

_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_sw(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_SW_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_sw(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_sw(kind: str, has_stamp) -> Optional[bool]:
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
    return _insert_after_enter(text, block, "cpu-" + kind)


def apply_pef_swq(kind: str, cpu: Path) -> None:
    if kind not in _SKIP_OFF:
        raise ValueError("unknown pef sw kind %s" % kind)
    cpu.write_text(patch_cpu_pef_skip(cpu.read_text(), kind))
