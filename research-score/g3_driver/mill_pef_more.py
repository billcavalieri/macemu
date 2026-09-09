#!/usr/bin/env python3
"""50 leftover PEF mills after NavUnload stub hang. skipUnld first.
Do not remill leftover:pef-stublr."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

# (kind, idx or None, marker token after "PEF ")
_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-skipunld", None, "skipUnld"),
    ("pef-skipext", None, "skipExt"),
    ("pef-unldlr", None, "unldLR"),
    ("pef-restr2", None, "restR2"),
    ("pef-epilr", None, "epiLR"),
    ("pef-selwin", 33, "SelectWindow"),
    ("pef-zoomwin", 0, "ZoomWindow"),
    ("pef-setwtitle", 127, "SetWTitle"),
    ("pef-paramtxt", 49, "ParamText"),
    ("pef-setditxt", 147, "SetDialogItemText"),
    ("pef-getstr", 72, "GetString"),
    ("pef-loadres", 86, "LoadResource"),
    ("pef-detach", 100, "DetachResource"),
    ("pef-relres", 213, "ReleaseResource"),
    ("pef-newrgn", 235, "NewRgn"),
    ("pef-disprgn", 91, "DisposeRgn"),
    ("pef-copyrgn", 13, "CopyRgn"),
    ("pef-offrect", 73, "OffsetRect"),
    ("pef-ptinrect", 81, "PtInRect"),
    ("pef-gtolocal", 69, "GlobalToLocal"),
    ("pef-sectrect", 116, "SectRect"),
    ("pef-erasergn", 259, "EraseRgn"),
    ("pef-stdrect", 203, "StdRect"),
    ("pef-penpat", 248, "PenPat"),
    ("pef-appfont", 207, "GetAppFont"),
    ("pef-getpen", 145, "GetPenState"),
    ("pef-setpen", 240, "SetPenState"),
    ("pef-tetbox", 226, "TETextBox"),
    ("pef-trunctxt", 136, "TruncText"),
    ("pef-sysbeep", 273, "SysBeep"),
    ("pef-findwin", 282, "FindWindow"),
    ("pef-isdlg", 130, "IsDialogEvent"),
    ("pef-stdfilt", 215, "StdFilterProc"),
    ("pef-getfilt", 148, "GetStdFilterProc"),
    ("pef-sethsz", 109, "SetHandleSize"),
    ("pef-hlockhi", 23, "HLockHi"),
    ("pef-reserr", 216, "ResError"),
    ("pef-setrload", 167, "SetResLoad"),
    ("pef-cnt1res", 222, "Count1Resources"),
    ("pef-get1ind", 52, "Get1IndResource"),
    ("pef-geticon", 78, "GetIcon"),
    ("pef-ploticon", 228, "PlotIconHandle"),
    ("pef-getcurs", 26, "GetCursor"),
    ("pef-nextdev", 57, "GetNextDevice"),
    ("pef-devlist", 118, "GetDeviceList"),
    ("pef-testattr", 129, "TestDeviceAttribute"),
    ("pef-setgworld", 119, "SetGWorld"),
    ("pef-getgworld", 138, "GetGWorld"),
    ("pef-qderr", 139, "QDError"),
    ("pef-hand2hand", 251, "HandToHand"),
]
assert len(_HOSTS) == 50, len(_HOSTS)

PEF_MORE_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_more(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_MORE_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_more(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_more(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


def _insert_before_void(text: str, block: str, label: str) -> str:
    n = text.count(_ANCHOR)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(_ANCHOR, _ANCHOR + block, 1)


def _host_block(idx: int, marker: str) -> str:
    extra = ""
    if idx == 235:
        extra = (
            "		r3 = g3_pef_newptr(16u);\n"
            "		if (!r3)\n"
            "			r3 = 0;\n"
        )
    elif idx == 251:
        extra = (
            "		r3 = a3;\n"
        )
    elif idx == 148 or idx == 215:
        extra = (
            "		r3 = 0;\n"
        )
    else:
        extra = "		r3 = 0;\n"
    return (
        "	if (idx == %uu) {\n" % idx
        + extra
        + "#if NW_BOOT_LOG\n"
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


def patch_cpu_pef_skipunld(text: str) -> str:
    m = _MARKERS["pef-skipunld"]
    if m in text:
        return text
    old = _ENTER
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-skipunld count=%s" % n)
    new = (
        "	if (g3_ea_data(ent + 0x2f3u))\n"
        "		vm_write_memory_4(ent + 0x2f0u, 0x60000000u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nsu;\n"
        "		if (nsu < 8) {\n"
        "			nsu++;\n"
        "			nw_boot_log(\n"
        "				\"%s\");\n" % m
        + "		}\n"
        "	}\n"
        "#endif\n"
        + old
    )
    return text.replace(old, new, 1)


def patch_cpu_pef_skipext(text: str) -> str:
    m = _MARKERS["pef-skipext"]
    if m in text:
        return text
    old = _ENTER
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-skipext count=%s" % n)
    new = (
        "	if (g3_ea_data(ent + 0x307u))\n"
        "		vm_write_memory_4(ent + 0x304u, 0x60000000u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nse;\n"
        "		if (nse < 8) {\n"
        "			nse++;\n"
        "			nw_boot_log(\n"
        "				\"%s\");\n" % m
        + "		}\n"
        "	}\n"
        "#endif\n"
        + old
    )
    return text.replace(old, new, 1)


def patch_cpu_pef_unldlr(text: str) -> str:
    m = _MARKERS["pef-unldlr"]
    if m in text:
        return text
    old = (
        "				if (idx == 291u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x101016b0u;\n"
    )
    new = (
        "				if (idx == 289u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x101016c8u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nul;\n"
        "						if (nul < 8) {\n"
        "							nul++;\n"
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
        raise ValueError("mill patch missing: cpu-pef-unldlr count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_restr2(text: str) -> str:
    m = _MARKERS["pef-restr2"]
    if m in text:
        return text
    old = (
        "				gpr(3) = r3;\n"
        "				/* 10101400 glue: r3=0 so 1010140c takes splash. */\n"
    )
    new = (
        "				gpr(3) = r3;\n"
        "				if (g3_ea_data(gpr(1) + 23u)) {\n"
        "					uint32 toc = vm_read_memory_4(gpr(1) + 20u);\n"
        "					if (toc == 0x10115000u)\n"
        "						gpr(2) = toc;\n"
        "				}\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned nr2;\n"
        "					if (nr2 < 8) {\n"
        "						nr2++;\n"
        "						nw_boot_log(\n"
        "							\"%s\");\n" % m
        + "					}\n"
        "				}\n"
        "#endif\n"
        "				/* 10101400 glue: r3=0 so 1010140c takes splash. */\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-restr2 count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_epilr(text: str) -> str:
    m = _MARKERS["pef-epilr"]
    if m in text:
        return text
    old = (
        "				} else\n"
        "					pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    new = (
        "				} else if (lr() == pc() || lr() == 0) {\n"
        "					uint32 slr = 0;\n"
        "					if (g3_ea_data(gpr(1) + 11u))\n"
        "						slr = vm_read_memory_4(gpr(1) + 8u);\n"
        "					if (!(slr >= 0x101013d0u && slr < 0x10115000u) &&\n"
        "					    g3_ea_data(gpr(1) + 123u))\n"
        "						slr = vm_read_memory_4(gpr(1) + 120u);\n"
        "					if (slr >= 0x101013d0u && slr < 0x10115000u)\n"
        "						pc() = slr;\n"
        "					else\n"
        "						pc() = lr();\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nel;\n"
        "						if (nel < 8) {\n"
        "							char buf[96];\n"
        "							nel++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"%s to=%%08x\",\n" % m
        + "								 (unsigned)pc());\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else\n"
        "					pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-epilr count=%s" % n)
    return text.replace(old, new, 1)


_SPECIAL = {
    "pef-skipunld": patch_cpu_pef_skipunld,
    "pef-skipext": patch_cpu_pef_skipext,
    "pef-unldlr": patch_cpu_pef_unldlr,
    "pef-restr2": patch_cpu_pef_restr2,
    "pef-epilr": patch_cpu_pef_epilr,
}


def apply_pef_more(kind: str, cpu: Path) -> None:
    if kind in _SPECIAL:
        cpu.write_text(_SPECIAL[kind](cpu.read_text()))
        return
    idx = _IDX.get(kind)
    marker = _MARKERS.get(kind)
    if idx is None or marker is None:
        raise ValueError("unknown pef more kind %s" % kind)
    text = cpu.read_text()
    if marker in text:
        cpu.write_text(text)
        return
    cpu.write_text(_insert_before_void(text, _host_block(idx, marker), "cpu-" + kind))
