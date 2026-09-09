#!/usr/bin/env python3
"""50 leftover PEF mills: skip DisposePtr site at 1010a308, then more hosts.
Do not remill leftover:pef-stublr / skipunld / unldlr / trackgo."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-dispptr0", None, "dispPtr0"),
    ("pef-skipdisp", None, "skipDisp"),
    ("pef-dispbeq", None, "dispBeq"),
    ("pef-displr", None, "dispLR"),
    ("pef-flushvol", 5, "FlushVol"),
    ("pef-appzone", 8, "ApplicationZone"),
    ("pef-vinst", 10, "VInstall"),
    ("pef-frround", 11, "FrameRoundRect"),
    ("pef-eject", 20, "Eject"),
    ("pef-calcmask", 21, "CalcMask"),
    ("pef-crsrbusy", 22, "LMGetCrsrBusy"),
    ("pef-hidectl", 27, "HideControl"),
    ("pef-newcwin", 29, "GetNewCWindow"),
    ("pef-movectl", 35, "MoveControl"),
    ("pef-font2scr", 40, "FontToScript"),
    ("pef-propen", 44, "PrOpen"),
    ("pef-prclose", 45, "PrClose"),
    ("pef-prclosed", 48, "PrCloseDoc"),
    ("pef-killctl", 54, "KillControls"),
    ("pef-propend", 56, "PrOpenDoc"),
    ("pef-getctlmin", 61, "GetControlMinimum"),
    ("pef-getvinfo", 62, "GetVInfo"),
    ("pef-newctl", 63, "NewControl"),
    ("pef-setctlval", 67, "SetControlValue"),
    ("pef-setctlmin", 74, "SetControlMinimum"),
    ("pef-showctl", 75, "ShowControl"),
    ("pef-prpic", 76, "PrPicFile"),
    ("pef-mbarh", 77, "GetMBarHeight"),
    ("pef-getctlval", 80, "GetControlValue"),
    ("pef-getback", 82, "GetBackColor"),
    ("pef-sizectl", 87, "SizeControl"),
    ("pef-trackbox", 88, "TrackBox"),
    ("pef-sfget", 96, "SFGetFile"),
    ("pef-setstdc", 98, "SetStdCProcs"),
    ("pef-getctlmax", 101, "GetControlMaximum"),
    ("pef-prdef", 102, "PrintDefault"),
    ("pef-aegetn", 103, "AEGetNthDesc"),
    ("pef-dispctl", 104, "DisposeControl"),
    ("pef-aedisp", 105, "AEDisposeDesc"),
    ("pef-setdlgcanc", 106, "SetDialogCancelItem"),
    ("pef-getscript", 112, "GetScriptVariable"),
    ("pef-hilitectl", 113, "HiliteControl"),
    ("pef-setctlmax", 120, "SetControlMaximum"),
    ("pef-setmitem", 122, "SetMenuItemText"),
    ("pef-aegetnp", 123, "AEGetNthPtr"),
    ("pef-getgray", 124, "GetGray"),
    ("pef-rmvres", 132, "RemoveResource"),
    ("pef-aeputp", 133, "AEPutParamDesc"),
    ("pef-seta5", 135, "SetA5"),
    ("pef-trackctl", 141, "TrackControl"),
]
assert len(_HOSTS) == 50, len(_HOSTS)

PEF_DISP_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_disp(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_DISP_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_disp(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_disp(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_dispptr0(text: str) -> str:
    m = _MARKERS["pef-dispptr0"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(toc + 0xc6fu))\n"
        "		vm_write_memory_4(toc + 0xc6cu, 0);\n"
        + _log_enter(m, "nd0")
    )
    return _insert_before_enter(text, block, "cpu-pef-dispptr0")


def patch_cpu_pef_skipdisp(text: str) -> str:
    m = _MARKERS["pef-skipdisp"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x8f3bu))\n"
        "		vm_write_memory_4(ent + 0x8f38u, 0x48000020u);\n"
        + _log_enter(m, "nsd")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipdisp")


def patch_cpu_pef_dispbeq(text: str) -> str:
    m = _MARKERS["pef-dispbeq"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x8f47u))\n"
        "		vm_write_memory_4(ent + 0x8f44u, 0x48000014u);\n"
        + _log_enter(m, "ndb")
    )
    return _insert_before_enter(text, block, "cpu-pef-dispbeq")


def patch_cpu_pef_displr(text: str) -> str:
    m = _MARKERS["pef-displr"]
    if m in text:
        return text
    old = (
        "				if (idx == 291u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x101016b0u;\n"
    )
    new = (
        "				if (idx == 117u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a328u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned ndl;\n"
        "						if (ndl < 8) {\n"
        "							ndl++;\n"
        "							nw_boot_log(\n"
        "								\"%s\");\n" % m
        + "						}\n"
        "					}\n"
        "#endif\n"
        "				} else if (idx == 291u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x101016b0u;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-displr count=%s" % n)
    return text.replace(old, new, 1)


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
    "pef-dispptr0": patch_cpu_pef_dispptr0,
    "pef-skipdisp": patch_cpu_pef_skipdisp,
    "pef-dispbeq": patch_cpu_pef_dispbeq,
    "pef-displr": patch_cpu_pef_displr,
}


def apply_pef_disp(kind: str, cpu: Path) -> None:
    if kind in _SPECIAL:
        cpu.write_text(_SPECIAL[kind](cpu.read_text()))
        return
    idx = _IDX.get(kind)
    marker = _MARKERS.get(kind)
    if idx is None or marker is None:
        raise ValueError("unknown pef disp kind %s" % kind)
    text = cpu.read_text()
    if marker in text:
        return
    cpu.write_text(_insert_before_void(text, _host_block(idx, marker), "cpu-" + kind))
