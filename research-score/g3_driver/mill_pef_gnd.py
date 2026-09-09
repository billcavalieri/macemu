#!/usr/bin/env python3
"""5 leftover PEF mills after KEEP 17011: jump GetNewDialog 510 to
ShowWindow/DrawDialog (skipg1s dropped those stamps), plus skip CurResFile
and GetDialogItem. Do not remill leftover:pef-nrdlr / skipgest / skipglue.
Do not skip GetNewDialog itself. Do not mill skip-68k."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

# (kind, idx or None, marker token after "PEF ")
_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-gndsw", None, "gndSw"),
    ("pef-g1ssw", None, "g1sSw"),
    ("pef-gnddlg", None, "gndDlg"),
    ("pef-skipcrf", None, "skipCrf"),
    ("pef-skipgdi3", None, "skipGdi3"),
]
assert len(_HOSTS) == 5, len(_HOSTS)

PEF_GND_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}

# ent=101013d0. After GetNewDialog bl (10101d40) jump to ShowWindow 10101ed8
# or DrawDialog 10101ef8. g1ssw overwrites skipg1s 10101db0 with a longer
# branch to ShowWindow.
_SKIP_OFF: Dict[str, Tuple[int, int]] = {
    "pef-gndsw": (0x0974, 0x48000194),
    "pef-g1ssw": (0x09E0, 0x48000128),
    "pef-gnddlg": (0x0974, 0x480001B4),
    "pef-skipcrf": (0x094C, 0x48000004),
    "pef-skipgdi3": (0x0A48, 0x48000004),
}

_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_gnd(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_GND_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_gnd(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_gnd(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


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


def apply_pef_gndq(kind: str, cpu: Path) -> None:
    if kind not in _SKIP_OFF:
        raise ValueError("unknown pef gnd kind %s" % kind)
    cpu.write_text(patch_cpu_pef_skip(cpu.read_text(), kind))
