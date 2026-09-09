#!/usr/bin/env python3
"""50 leftover PEF mills: skip whole Nav CanRun/Unload block, then more hosts.
Do not remill leftover:pef-stublr / pef-skipunld / pef-unldlr."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-navflag0", None, "navFlag0"),
    ("pef-skipnav", None, "skipNav"),
    ("pef-navoff", None, "NavOff"),
    ("pef-unlderr", None, "UnldErr"),
    ("pef-skipcan", None, "skipCan"),
    ("pef-exitshell", 14, "ExitToShell"),
    ("pef-launchapp", 65, "LaunchApplication"),
    ("pef-getnproc", 4, "GetNextProcess"),
    ("pef-getpinfo", 200, "GetProcessInformation"),
    ("pef-setfront", 175, "SetFrontProcess"),
    ("pef-getcurpr", 278, "GetCurrentProcess"),
    ("pef-dragwin", 197, "DragWindow"),
    ("pef-trackgo", 234, "TrackGoAway"),
    ("pef-sysclick", 94, "SystemClick"),
    ("pef-hnopurge", 181, "HNoPurge"),
    ("pef-hpurge", 229, "HPurge"),
    ("pef-ptr2hand", 275, "PtrToHand"),
    ("pef-newgworld", 210, "NewGWorld"),
    ("pef-dispgw", 3, "DisposeGWorld"),
    ("pef-unlkpix", 204, "UnlockPixels"),
    ("pef-lkpix", 284, "LockPixels"),
    ("pef-dispdlog", 211, "DisposeDialog"),
    ("pef-dispwin", 186, "DisposeWindow"),
    ("pef-drawmbar", 24, "DrawMenuBar"),
    ("pef-menukey", 36, "MenuKey"),
    ("pef-insmenu", 66, "InsertMenu"),
    ("pef-getmh", 59, "GetMenuHandle"),
    ("pef-getmitem", 71, "GetMenuItemText"),
    ("pef-getmenu", 115, "GetMenu"),
    ("pef-appendrm", 187, "AppendResMenu"),
    ("pef-menusel", 266, "MenuSelect"),
    ("pef-hilmenu", 16, "HiliteMenu"),
    ("pef-aeinst", 15, "AEInstallEventHandler"),
    ("pef-aecount", 83, "AECountItems"),
    ("pef-aecreate", 90, "AECreateAppleEvent"),
    ("pef-aesend", 232, "AESend"),
    ("pef-fsread", 93, "FSRead"),
    ("pef-fswrite", 47, "FSWrite"),
    ("pef-fsclose", 172, "FSClose"),
    ("pef-seteof", 85, "SetEOF"),
    ("pef-geteof", 238, "GetEOF"),
    ("pef-setfpos", 255, "SetFPos"),
    ("pef-fspopendf", 60, "FSpOpenDF"),
    ("pef-fspopenrf", 262, "FSpOpenResFile"),
    ("pef-closeres", 179, "CloseResFile"),
    ("pef-setvol", 128, "SetVol"),
    ("pef-hgetvol", 258, "HGetVol"),
    ("pef-equalstr", 152, "EqualString"),
    ("pef-blockmd", 182, "BlockMoveData"),
    ("pef-offrgn", 183, "OffsetRgn"),
]
assert len(_HOSTS) == 50, len(_HOSTS)

PEF_NEXT_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_next(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_NEXT_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_next(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_next(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_navflag0(text: str) -> str:
    m = _MARKERS["pef-navflag0"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(toc + 0x49fu))\n"
        "		vm_write_memory_4(toc + 0x49cu, 0);\n"
        + _log_enter(m, "nnf")
    )
    return _insert_before_enter(text, block, "cpu-pef-navflag0")


def patch_cpu_pef_skipnav(text: str) -> str:
    m = _MARKERS["pef-skipnav"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x2cfu))\n"
        "		vm_write_memory_4(ent + 0x2ccu, 0x4800002cu);\n"
        + _log_enter(m, "nsn")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipnav")


def patch_cpu_pef_navoff(text: str) -> str:
    m = _MARKERS["pef-navoff"]
    if m in text:
        return text
    old = (
        "	if (idx == 291u) {\n"
        "		r3 = 1;\n"
    )
    new = (
        "	if (idx == 291u) {\n"
        "		r3 = 0;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-navoff-r3 count=%s" % n)
    text = text.replace(old, new, 1)
    old2 = (
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavServicesCanRun\");\n"
    )
    new2 = (
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavOff\");\n"
    )
    n2 = text.count(old2)
    if n2 != 1:
        raise ValueError("mill patch missing: cpu-pef-navoff-st count=%s" % n2)
    return text.replace(old2, new2, 1)


def patch_cpu_pef_unlderr(text: str) -> str:
    m = _MARKERS["pef-unlderr"]
    if m in text:
        return text
    old = (
        "	if (idx == 289u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 289u) {\n"
        "		r3 = 0xffffffffu;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-unlderr-r3 count=%s" % n)
    text = text.replace(old, new, 1)
    old2 = (
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavUnload\");\n"
    )
    new2 = (
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF UnldErr\");\n"
    )
    n2 = text.count(old2)
    if n2 != 1:
        raise ValueError("mill patch missing: cpu-pef-unlderr-st count=%s" % n2)
    return text.replace(old2, new2, 1)


def patch_cpu_pef_skipcan(text: str) -> str:
    m = _MARKERS["pef-skipcan"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x2dfu))\n"
        "		vm_write_memory_4(ent + 0x2dcu, 0x60000000u);\n"
        + _log_enter(m, "nsc")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipcan")


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


_SPECIAL = {
    "pef-navflag0": patch_cpu_pef_navflag0,
    "pef-skipnav": patch_cpu_pef_skipnav,
    "pef-navoff": patch_cpu_pef_navoff,
    "pef-unlderr": patch_cpu_pef_unlderr,
    "pef-skipcan": patch_cpu_pef_skipcan,
}


def apply_pef_next(kind: str, cpu: Path) -> None:
    if kind in _SPECIAL:
        cpu.write_text(_SPECIAL[kind](cpu.read_text()))
        return
    idx = _IDX.get(kind)
    marker = _MARKERS.get(kind)
    if idx is None or marker is None:
        raise ValueError("unknown pef next kind %s" % kind)
    text = cpu.read_text()
    if marker in text:
        return
    cpu.write_text(_insert_before_void(text, _host_block(idx, marker), "cpu-" + kind))
