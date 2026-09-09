#!/usr/bin/env python3
"""Splash leftover PEF hosts. Fired voids at KEEP 16686: FrontWindow,
GetPort, ReplaceText, then DrawPicture/update/EventAvail. Not skip-68k.
Special-case before g3_pef_void_bits so the void early-out does not win."""
from __future__ import annotations

from pathlib import Path
from typing import List, Optional

PEF_SPLASH_KINDS: List[str] = [
    "pef-frontwin",
    "pef-getport",
    "pef-repltxt",
    "pef-drawpict",
    "pef-inval",
    "pef-beginupd",
    "pef-endupd",
    "pef-copybits",
    "pef-updatedlg",
    "pef-eventav",
    "pef-stublr",
    "pef-textfont",
    "pef-textsize",
    "pef-textface",
    "pef-disph",
    "pef-dispp",
    "pef-hlock",
    "pef-hunlock",
    "pef-cliprect",
    "pef-eraserect",
    "pef-hostlr",
    "pef-moveto",
    "pef-drawstr",
    "pef-strwidth",
    "pef-rgbfore",
    "pef-setcurs",
    "pef-setrect",
    "pef-setclip",
    "pef-getclip",
    "pef-disprd",
    "pef-hostlr2",
    "pef-getptrsz",
    "pef-hgetst",
    "pef-hsetst",
    "pef-delay",
    "pef-lmticks",
    "pef-fontinfo",
    "pef-pensize",
    "pef-ltog",
    "pef-newhclr",
    "pef-navrun",
    "pef-navver",
    "pef-navload",
    "pef-navunld",
    "pef-navget",
    "pef-navput",
    "pef-navdisp",
    "pef-findsym",
    "pef-getdf",
    "pef-navlr",
]
MARKER_FRONTWIN = "G3: 68k Launch A9F2 CFM Upgrader PEF FrontWindow"
MARKER_GETPORT = "G3: 68k Launch A9F2 CFM Upgrader PEF GetPort"
MARKER_REPLTXT = "G3: 68k Launch A9F2 CFM Upgrader PEF ReplaceText"
MARKER_DRAWPICT = "G3: 68k Launch A9F2 CFM Upgrader PEF DrawPicture"
MARKER_INVAL = "G3: 68k Launch A9F2 CFM Upgrader PEF InvalRect"
MARKER_BEGINUPD = "G3: 68k Launch A9F2 CFM Upgrader PEF BeginUpdate"
MARKER_ENDUPD = "G3: 68k Launch A9F2 CFM Upgrader PEF EndUpdate"
MARKER_COPYBITS = "G3: 68k Launch A9F2 CFM Upgrader PEF CopyBits"
MARKER_UPDATEDLG = "G3: 68k Launch A9F2 CFM Upgrader PEF UpdateDialog"
MARKER_EVENTAV = "G3: 68k Launch A9F2 CFM Upgrader PEF EventAvail"
MARKER_STUBLR = "G3: 68k Launch A9F2 CFM Upgrader PEF stubLR"
MARKER_TEXTFONT = "G3: 68k Launch A9F2 CFM Upgrader PEF TextFont"
MARKER_TEXTSIZE = "G3: 68k Launch A9F2 CFM Upgrader PEF TextSize"
MARKER_TEXTFACE = "G3: 68k Launch A9F2 CFM Upgrader PEF TextFace"
MARKER_DISPH = "G3: 68k Launch A9F2 CFM Upgrader PEF DisposeHandle"
MARKER_DISPP = "G3: 68k Launch A9F2 CFM Upgrader PEF DisposePtr"
MARKER_HLOCK = "G3: 68k Launch A9F2 CFM Upgrader PEF HLock"
MARKER_HUNLOCK = "G3: 68k Launch A9F2 CFM Upgrader PEF HUnlock"
MARKER_CLIPRECT = "G3: 68k Launch A9F2 CFM Upgrader PEF ClipRect"
MARKER_ERASERECT = "G3: 68k Launch A9F2 CFM Upgrader PEF EraseRect"
MARKER_HOSTLR = "G3: 68k Launch A9F2 CFM Upgrader PEF hostLR"
MARKER_MOVETO = "G3: 68k Launch A9F2 CFM Upgrader PEF MoveTo"
MARKER_DRAWSTR = "G3: 68k Launch A9F2 CFM Upgrader PEF DrawString"
MARKER_STRWIDTH = "G3: 68k Launch A9F2 CFM Upgrader PEF StringWidth"
MARKER_RGBFORE = "G3: 68k Launch A9F2 CFM Upgrader PEF RGBForeColor"
MARKER_SETCURS = "G3: 68k Launch A9F2 CFM Upgrader PEF SetCursor"
MARKER_SETRECT = "G3: 68k Launch A9F2 CFM Upgrader PEF SetRect"
MARKER_SETCLIP = "G3: 68k Launch A9F2 CFM Upgrader PEF SetClip"
MARKER_GETCLIP = "G3: 68k Launch A9F2 CFM Upgrader PEF GetClip"
MARKER_DISPRD = "G3: 68k Launch A9F2 CFM Upgrader PEF DisposeRoutineDescriptor"
MARKER_HOSTLR2 = "G3: 68k Launch A9F2 CFM Upgrader PEF hostLR2"
MARKER_GETPTRSZ = "G3: 68k Launch A9F2 CFM Upgrader PEF GetPtrSize"
MARKER_HGETST = "G3: 68k Launch A9F2 CFM Upgrader PEF HGetState"
MARKER_HSETST = "G3: 68k Launch A9F2 CFM Upgrader PEF HSetState"
MARKER_DELAY = "G3: 68k Launch A9F2 CFM Upgrader PEF Delay"
MARKER_LMTICKS = "G3: 68k Launch A9F2 CFM Upgrader PEF LMGetTicks"
MARKER_FONTINFO = "G3: 68k Launch A9F2 CFM Upgrader PEF GetFontInfo"
MARKER_PENSIZE = "G3: 68k Launch A9F2 CFM Upgrader PEF PenSize"
MARKER_LTOG = "G3: 68k Launch A9F2 CFM Upgrader PEF LocalToGlobal"
MARKER_NEWHCLR = "G3: 68k Launch A9F2 CFM Upgrader PEF NewHandleClear"
MARKER_NAVRUN = "G3: 68k Launch A9F2 CFM Upgrader PEF NavServicesCanRun"
MARKER_NAVVER = "G3: 68k Launch A9F2 CFM Upgrader PEF NavLibraryVersion"
MARKER_NAVLOAD = "G3: 68k Launch A9F2 CFM Upgrader PEF NavLoad"
MARKER_NAVUNLD = "G3: 68k Launch A9F2 CFM Upgrader PEF NavUnload"
MARKER_NAVGET = "G3: 68k Launch A9F2 CFM Upgrader PEF NavGetFile"
MARKER_NAVPUT = "G3: 68k Launch A9F2 CFM Upgrader PEF NavPutFile"
MARKER_NAVDISP = "G3: 68k Launch A9F2 CFM Upgrader PEF NavDisposeReply"
MARKER_FINDSYM = "G3: 68k Launch A9F2 CFM Upgrader PEF FindSymbol"
MARKER_GETDF = "G3: 68k Launch A9F2 CFM Upgrader PEF GetDiskFragment"
MARKER_NAVLR = "G3: 68k Launch A9F2 CFM Upgrader PEF navLR"

_MARKERS = {
    "pef-frontwin": MARKER_FRONTWIN,
    "pef-getport": MARKER_GETPORT,
    "pef-repltxt": MARKER_REPLTXT,
    "pef-drawpict": MARKER_DRAWPICT,
    "pef-inval": MARKER_INVAL,
    "pef-beginupd": MARKER_BEGINUPD,
    "pef-endupd": MARKER_ENDUPD,
    "pef-copybits": MARKER_COPYBITS,
    "pef-updatedlg": MARKER_UPDATEDLG,
    "pef-eventav": MARKER_EVENTAV,
    "pef-stublr": MARKER_STUBLR,
    "pef-textfont": MARKER_TEXTFONT,
    "pef-textsize": MARKER_TEXTSIZE,
    "pef-textface": MARKER_TEXTFACE,
    "pef-disph": MARKER_DISPH,
    "pef-dispp": MARKER_DISPP,
    "pef-hlock": MARKER_HLOCK,
    "pef-hunlock": MARKER_HUNLOCK,
    "pef-cliprect": MARKER_CLIPRECT,
    "pef-eraserect": MARKER_ERASERECT,
    "pef-hostlr": MARKER_HOSTLR,
    "pef-moveto": MARKER_MOVETO,
    "pef-drawstr": MARKER_DRAWSTR,
    "pef-strwidth": MARKER_STRWIDTH,
    "pef-rgbfore": MARKER_RGBFORE,
    "pef-setcurs": MARKER_SETCURS,
    "pef-setrect": MARKER_SETRECT,
    "pef-setclip": MARKER_SETCLIP,
    "pef-getclip": MARKER_GETCLIP,
    "pef-disprd": MARKER_DISPRD,
    "pef-hostlr2": MARKER_HOSTLR2,
    "pef-getptrsz": MARKER_GETPTRSZ,
    "pef-hgetst": MARKER_HGETST,
    "pef-hsetst": MARKER_HSETST,
    "pef-delay": MARKER_DELAY,
    "pef-lmticks": MARKER_LMTICKS,
    "pef-fontinfo": MARKER_FONTINFO,
    "pef-pensize": MARKER_PENSIZE,
    "pef-ltog": MARKER_LTOG,
    "pef-newhclr": MARKER_NEWHCLR,
    "pef-navrun": MARKER_NAVRUN,
    "pef-navver": MARKER_NAVVER,
    "pef-navload": MARKER_NAVLOAD,
    "pef-navunld": MARKER_NAVUNLD,
    "pef-navget": MARKER_NAVGET,
    "pef-navput": MARKER_NAVPUT,
    "pef-navdisp": MARKER_NAVDISP,
    "pef-findsym": MARKER_FINDSYM,
    "pef-getdf": MARKER_GETDF,
    "pef-navlr": MARKER_NAVLR,
}

_ANCHOR = "	(void)g3_pef_void_st;\n"


def next_pef_splash(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_SPLASH_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_splash(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_splash(kind: str, has_stamp) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return has_stamp(m)


def _insert_before_void(text: str, block: str, label: str) -> str:
    n = text.count(_ANCHOR)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(_ANCHOR, _ANCHOR + block, 1)


def patch_cpu_pef_frontwin(text: str) -> str:
    if MARKER_FRONTWIN in text:
        return text
    block = (
        "	if (idx == 166u) {\n"
        "		r3 = g3_splash_dlg;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nfw;\n"
        "			if (nfw < 8) {\n"
        "				char buf[96];\n"
        "				nfw++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF FrontWindow w=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-frontwin")


def patch_cpu_pef_getport(text: str) -> str:
    if MARKER_GETPORT in text:
        return text
    block = (
        "	if (idx == 176u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (!port)\n"
        "			port = RAMBase + 0xa100u;\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			vm_write_memory_4(a3, port);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngp;\n"
        "			if (ngp < 8) {\n"
        "				char buf[96];\n"
        "				ngp++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetPort p=%08x\",\n"
        "					 (unsigned)port);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-getport")


def patch_cpu_pef_repltxt(text: str) -> str:
    if MARKER_REPLTXT in text:
        return text
    block = (
        "	if (idx == 12u) {\n"
        "		/* ReplaceText: 1 substitution so splash does not retry. */\n"
        "		r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nrt;\n"
        "			if (nrt < 8) {\n"
        "				nrt++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ReplaceText\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-repltxt")


def patch_cpu_pef_drawpict(text: str) -> str:
    if MARKER_DRAWPICT in text:
        return text
    block = (
        "	if (idx == 246u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ndp;\n"
        "			if (ndp < 8) {\n"
        "				ndp++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF DrawPicture\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-drawpict")


def patch_cpu_pef_inval(text: str) -> str:
    if MARKER_INVAL in text:
        return text
    block = (
        "	if (idx == 217u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned niv;\n"
        "			if (niv < 8) {\n"
        "				niv++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF InvalRect\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-inval")


def patch_cpu_pef_beginupd(text: str) -> str:
    if MARKER_BEGINUPD in text:
        return text
    block = (
        "	if (idx == 7u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nbu;\n"
        "			if (nbu < 8) {\n"
        "				nbu++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF BeginUpdate\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-beginupd")


def patch_cpu_pef_endupd(text: str) -> str:
    if MARKER_ENDUPD in text:
        return text
    block = (
        "	if (idx == 265u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned neu;\n"
        "			if (neu < 8) {\n"
        "				neu++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF EndUpdate\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-endupd")


def patch_cpu_pef_copybits(text: str) -> str:
    if MARKER_COPYBITS in text:
        return text
    block = (
        "	if (idx == 236u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ncb;\n"
        "			if (ncb < 8) {\n"
        "				ncb++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF CopyBits\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-copybits")


def patch_cpu_pef_updatedlg(text: str) -> str:
    if MARKER_UPDATEDLG in text:
        return text
    block = (
        "	if (idx == 221u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nud;\n"
        "			if (nud < 8) {\n"
        "				nud++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF UpdateDialog\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-updatedlg")


def patch_cpu_pef_eventav(text: str) -> str:
    if MARKER_EVENTAV in text:
        return text
    block = (
        "	if (idx == 230u) {\n"
        "		unsigned i;\n"
        "		if (a4 && g3_ea_data(a4 + 15u))\n"
        "			for (i = 0; i < 16u; i += 4u)\n"
        "				vm_write_memory_4(a4 + i, 0);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nea;\n"
        "			if (nea < 8) {\n"
        "				nea++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF EventAvail\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-eventav")


def patch_cpu_pef_stublr(text: str) -> str:
    if MARKER_STUBLR in text:
        return text
    old = (
        "				}\n"
        "				pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    new = (
        "				}\n"
        "				{\n"
        "					uint32 ret = lr();\n"
        "					if (ret == pc() || ret == 0) {\n"
        "						uint32 slr = 0, r0v = gpr(0);\n"
        "						if (g3_ea_data(gpr(1) + 11u))\n"
        "							slr = vm_read_memory_4(gpr(1) + 8u);\n"
        "						if (r0v >= 0x101013d0u && r0v < 0x10115000u)\n"
        "							ret = r0v;\n"
        "						else if (slr >= 0x101013d0u && slr < 0x10115000u)\n"
        "							ret = slr;\n"
        "						else\n"
        "							ret = 0x101014b4u;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nslr;\n"
        "							if (nslr < 8) {\n"
        "								char buf[96];\n"
        "								nslr++;\n"
        "								snprintf(buf, sizeof(buf),\n"
        "									 \"G3: 68k Launch A9F2 CFM Upgrader PEF stubLR lr=%08x to=%08x\",\n"
        "									 (unsigned)lr(), (unsigned)ret);\n"
        "								nw_boot_log(buf);\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					}\n"
        "					pc() = ret;\n"
        "				}\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-stublr count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_textfont(text: str) -> str:
    if MARKER_TEXTFONT in text:
        return text
    block = (
        "	if (idx == 17u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (port && g3_ea_data(port + 69u))\n"
        "			vm_write_memory_2(port + 68u, (uint16)(a3 & 0xffffu));\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ntf;\n"
        "			if (ntf < 8) {\n"
        "				ntf++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF TextFont\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-textfont")


def patch_cpu_pef_textsize(text: str) -> str:
    if MARKER_TEXTSIZE in text:
        return text
    block = (
        "	if (idx == 178u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (port && g3_ea_data(port + 75u))\n"
        "			vm_write_memory_2(port + 74u, (uint16)(a3 & 0xffffu));\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nts;\n"
        "			if (nts < 8) {\n"
        "				nts++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF TextSize\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-textsize")


def patch_cpu_pef_textface(text: str) -> str:
    if MARKER_TEXTFACE in text:
        return text
    block = (
        "	if (idx == 41u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (port && g3_ea_data(port + 70u))\n"
        "			vm_write_memory_1(port + 70u, (uint8)(a3 & 0xffu));\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nfa;\n"
        "			if (nfa < 8) {\n"
        "				nfa++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF TextFace\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-textface")


def patch_cpu_pef_disph(text: str) -> str:
    if MARKER_DISPH in text:
        return text
    block = (
        "	if (idx == 276u) {\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			vm_write_memory_4(a3, 0);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ndh;\n"
        "			if (ndh < 8) {\n"
        "				ndh++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF DisposeHandle\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-disph")


def patch_cpu_pef_dispp(text: str) -> str:
    if MARKER_DISPP in text:
        return text
    block = (
        "	if (idx == 117u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ndp;\n"
        "			if (ndp < 8) {\n"
        "				ndp++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF DisposePtr\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-dispp")


def patch_cpu_pef_hlock(text: str) -> str:
    if MARKER_HLOCK in text:
        return text
    block = (
        "	if (idx == 277u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhl;\n"
        "			if (nhl < 8) {\n"
        "				nhl++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HLock\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-hlock")


def patch_cpu_pef_hunlock(text: str) -> str:
    if MARKER_HUNLOCK in text:
        return text
    block = (
        "	if (idx == 38u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhu;\n"
        "			if (nhu < 8) {\n"
        "				nhu++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HUnlock\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-hunlock")


def patch_cpu_pef_cliprect(text: str) -> str:
    if MARKER_CLIPRECT in text:
        return text
    block = (
        "	if (idx == 25u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ncr;\n"
        "			if (ncr < 8) {\n"
        "				ncr++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ClipRect\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-cliprect")


def patch_cpu_pef_eraserect(text: str) -> str:
    if MARKER_ERASERECT in text:
        return text
    block = (
        "	if (idx == 206u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ner;\n"
        "			if (ner < 8) {\n"
        "				ner++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF EraseRect\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-eraserect")


def patch_cpu_pef_hostlr(text: str) -> str:
    if MARKER_HOSTLR in text:
        return text
    old = (
        "				}\n"
        "				pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    new = (
        "				}\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned nhl;\n"
        "					if (nhl < 24) {\n"
        "						char buf[128];\n"
        "						uint32 slr = 0;\n"
        "						nhl++;\n"
        "						if (g3_ea_data(gpr(1) + 11u))\n"
        "							slr = vm_read_memory_4(gpr(1) + 8u);\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF hostLR idx=%u lr=%08x r0=%08x s8=%08x\",\n"
        "							 (unsigned)idx, (unsigned)lr(),\n"
        "							 (unsigned)gpr(0), (unsigned)slr);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-hostlr count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_moveto(text: str) -> str:
    if MARKER_MOVETO in text:
        return text
    block = (
        "	if (idx == 218u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (port && g3_ea_data(port + 51u)) {\n"
        "			vm_write_memory_2(port + 48u, (uint16)(a3 & 0xffffu));\n"
        "			vm_write_memory_2(port + 50u, (uint16)(a4 & 0xffffu));\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nmt;\n"
        "			if (nmt < 8) {\n"
        "				nmt++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF MoveTo\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-moveto")


def patch_cpu_pef_drawstr(text: str) -> str:
    if MARKER_DRAWSTR in text:
        return text
    block = (
        "	if (idx == 51u) {\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nds;\n"
        "			if (nds < 8) {\n"
        "				nds++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF DrawString\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-drawstr")


def patch_cpu_pef_strwidth(text: str) -> str:
    if MARKER_STRWIDTH in text:
        return text
    block = (
        "	if (idx == 55u) {\n"
        "		uint32 n = 0;\n"
        "		if (a3 && g3_ea_data(a3))\n"
        "			n = vm_read_memory_1(a3);\n"
        "		r3 = n * 6u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nsw;\n"
        "			if (nsw < 8) {\n"
        "				nsw++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF StringWidth\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-strwidth")


def patch_cpu_pef_rgbfore(text: str) -> str:
    if MARKER_RGBFORE in text:
        return text
    block = (
        "	if (idx == 188u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nrf;\n"
        "			if (nrf < 8) {\n"
        "				nrf++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF RGBForeColor\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-rgbfore")


def patch_cpu_pef_setcurs(text: str) -> str:
    if MARKER_SETCURS in text:
        return text
    block = (
        "	if (idx == 42u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nsc;\n"
        "			if (nsc < 8) {\n"
        "				nsc++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetCursor\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-setcurs")


def patch_cpu_pef_setrect(text: str) -> str:
    if MARKER_SETRECT in text:
        return text
    block = (
        "	if (idx == 156u) {\n"
        "		if (a3 && g3_ea_data(a3 + 7u)) {\n"
        "			vm_write_memory_2(a3, (uint16)(a4 & 0xffffu));\n"
        "			vm_write_memory_2(a3 + 2u, (uint16)(a5 & 0xffffu));\n"
        "			vm_write_memory_2(a3 + 4u, (uint16)(a6 & 0xffffu));\n"
        "			vm_write_memory_2(a3 + 6u, (uint16)(a7 & 0xffffu));\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nsr;\n"
        "			if (nsr < 8) {\n"
        "				nsr++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetRect\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-setrect")


def patch_cpu_pef_setclip(text: str) -> str:
    if MARKER_SETCLIP in text:
        return text
    block = (
        "	if (idx == 50u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ncl;\n"
        "			if (ncl < 8) {\n"
        "				ncl++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetClip\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-setclip")


def patch_cpu_pef_getclip(text: str) -> str:
    if MARKER_GETCLIP in text:
        return text
    block = (
        "	if (idx == 46u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngc;\n"
        "			if (ngc < 8) {\n"
        "				ngc++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetClip\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-getclip")


def patch_cpu_pef_disprd(text: str) -> str:
    if MARKER_DISPRD in text:
        return text
    block = (
        "	if (idx == 126u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ndr;\n"
        "			if (ndr < 8) {\n"
        "				ndr++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF DisposeRoutineDescriptor\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-disprd")


def patch_cpu_pef_hostlr2(text: str) -> str:
    if MARKER_HOSTLR2 in text:
        return text
    old = (
        "					if (nhl < 24) {\n"
        "						char buf[128];\n"
        "						uint32 slr = 0;\n"
        "						nhl++;\n"
        "						if (g3_ea_data(gpr(1) + 11u))\n"
        "							slr = vm_read_memory_4(gpr(1) + 8u);\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF hostLR idx=%u lr=%08x r0=%08x s8=%08x\",\n"
    )
    new = (
        "					if (nhl < 256) {\n"
        "						char buf[128];\n"
        "						uint32 slr = 0;\n"
        "						nhl++;\n"
        "						if (g3_ea_data(gpr(1) + 11u))\n"
        "							slr = vm_read_memory_4(gpr(1) + 8u);\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF hostLR2 idx=%u lr=%08x r0=%08x s8=%08x\",\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-hostlr2 count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_getptrsz(text: str) -> str:
    if MARKER_GETPTRSZ in text:
        return text
    block = (
        "	if (idx == 224u) {\n"
        "		r3 = a3 ? 64u : 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nps;\n"
        "			if (nps < 8) {\n"
        "				nps++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetPtrSize\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-getptrsz")


def patch_cpu_pef_hgetst(text: str) -> str:
    if MARKER_HGETST in text:
        return text
    block = (
        "	if (idx == 196u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhg;\n"
        "			if (nhg < 8) {\n"
        "				nhg++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HGetState\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-hgetst")


def patch_cpu_pef_hsetst(text: str) -> str:
    if MARKER_HSETST in text:
        return text
    block = (
        "	if (idx == 201u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhs;\n"
        "			if (nhs < 8) {\n"
        "				nhs++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HSetState\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-hsetst")


def patch_cpu_pef_delay(text: str) -> str:
    if MARKER_DELAY in text:
        return text
    block = (
        "	if (idx == 107u) {\n"
        "		if (a4 && g3_ea_data(a4 + 3u))\n"
        "			vm_write_memory_4(a4, a3);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ndy;\n"
        "			if (ndy < 8) {\n"
        "				ndy++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF Delay\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-delay")


def patch_cpu_pef_lmticks(text: str) -> str:
    if MARKER_LMTICKS in text:
        return text
    block = (
        "	if (idx == 150u) {\n"
        "		static uint32 lmt = 120;\n"
        "		r3 = lmt++;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nlt;\n"
        "			if (nlt < 8) {\n"
        "				nlt++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF LMGetTicks\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-lmticks")


def patch_cpu_pef_fontinfo(text: str) -> str:
    if MARKER_FONTINFO in text:
        return text
    block = (
        "	if (idx == 146u) {\n"
        "		if (a3 && g3_ea_data(a3 + 7u)) {\n"
        "			vm_write_memory_2(a3, 9);\n"
        "			vm_write_memory_2(a3 + 2u, 2);\n"
        "			vm_write_memory_2(a3 + 4u, 6);\n"
        "			vm_write_memory_2(a3 + 6u, 0);\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nfi;\n"
        "			if (nfi < 8) {\n"
        "				nfi++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetFontInfo\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-fontinfo")


def patch_cpu_pef_pensize(text: str) -> str:
    if MARKER_PENSIZE in text:
        return text
    block = (
        "	if (idx == 271u) {\n"
        "		uint32 port = 0;\n"
        "		if (g3_ea_data(0x2aau))\n"
        "			port = vm_read_memory_4(0x2aau);\n"
        "		if (!port)\n"
        "			port = g3_splash_dlg;\n"
        "		if (port && g3_ea_data(port + 55u)) {\n"
        "			vm_write_memory_2(port + 52u, (uint16)(a3 & 0xffffu));\n"
        "			vm_write_memory_2(port + 54u, (uint16)(a4 & 0xffffu));\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned npn;\n"
        "			if (npn < 8) {\n"
        "				npn++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PenSize\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-pensize")


def patch_cpu_pef_ltog(text: str) -> str:
    if MARKER_LTOG in text:
        return text
    block = (
        "	if (idx == 2u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nlg;\n"
        "			if (nlg < 8) {\n"
        "				nlg++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF LocalToGlobal\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-ltog")


def patch_cpu_pef_newhclr(text: str) -> str:
    if MARKER_NEWHCLR in text:
        return text
    block = (
        "	if (idx == 84u) {\n"
        "		uint32 p = g3_pef_newptr(a3);\n"
        "		uint32 h = g3_pef_newptr(8u);\n"
        "		if (h && g3_ea_data(h + 3u))\n"
        "			vm_write_memory_4(h, p);\n"
        "		r3 = h;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnh;\n"
        "			if (nnh < 8) {\n"
        "				char buf[96];\n"
        "				nnh++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF NewHandleClear h=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-newhclr")


def patch_cpu_pef_navrun(text: str) -> str:
    if MARKER_NAVRUN in text:
        return text
    block = (
        "	if (idx == 291u) {\n"
        "		r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnr;\n"
        "			if (nnr < 8) {\n"
        "				nnr++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavServicesCanRun\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navrun")


def patch_cpu_pef_navver(text: str) -> str:
    if MARKER_NAVVER in text:
        return text
    block = (
        "	if (idx == 292u) {\n"
        "		r3 = 0x02008000u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnv;\n"
        "			if (nnv < 8) {\n"
        "				nnv++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavLibraryVersion\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navver")


def patch_cpu_pef_navload(text: str) -> str:
    if MARKER_NAVLOAD in text:
        return text
    block = (
        "	if (idx == 288u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnl;\n"
        "			if (nnl < 8) {\n"
        "				nnl++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavLoad\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navload")


def patch_cpu_pef_navunld(text: str) -> str:
    if MARKER_NAVUNLD in text:
        return text
    block = (
        "	if (idx == 289u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnu;\n"
        "			if (nnu < 8) {\n"
        "				nnu++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavUnload\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navunld")


def patch_cpu_pef_navget(text: str) -> str:
    if MARKER_NAVGET in text:
        return text
    block = (
        "	if (idx == 290u) {\n"
        "		r3 = 0xffffff80u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nng;\n"
        "			if (nng < 8) {\n"
        "				nng++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavGetFile\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navget")


def patch_cpu_pef_navput(text: str) -> str:
    if MARKER_NAVPUT in text:
        return text
    block = (
        "	if (idx == 294u) {\n"
        "		r3 = 0xffffff80u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnp;\n"
        "			if (nnp < 8) {\n"
        "				nnp++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavPutFile\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navput")


def patch_cpu_pef_navdisp(text: str) -> str:
    if MARKER_NAVDISP in text:
        return text
    block = (
        "	if (idx == 293u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnd;\n"
        "			if (nnd < 8) {\n"
        "				nnd++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NavDisposeReply\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-navdisp")


def patch_cpu_pef_findsym(text: str) -> str:
    if MARKER_FINDSYM in text:
        return text
    block = (
        "	if (idx == 108u) {\n"
        "		if (a6 && g3_ea_data(a6 + 3u))\n"
        "			vm_write_memory_4(a6, 0);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nfs;\n"
        "			if (nfs < 8) {\n"
        "				nfs++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF FindSymbol\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-findsym")


def patch_cpu_pef_getdf(text: str) -> str:
    if MARKER_GETDF in text:
        return text
    block = (
        "	if (idx == 114u) {\n"
        "		if (a7 && g3_ea_data(a7 + 3u))\n"
        "			vm_write_memory_4(a7, 1);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngf;\n"
        "			if (ngf < 8) {\n"
        "				ngf++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetDiskFragment\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    return _insert_before_void(text, block, "cpu-pef-getdf")


def patch_cpu_pef_navlr(text: str) -> str:
    if MARKER_NAVLR in text:
        return text
    old = (
        "#endif\n"
        "				pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_post && g3_post < 24) {\n"
    )
    new = (
        "#endif\n"
        "				if (idx == 291u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x101016b0u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nnl;\n"
        "						if (nnl < 8) {\n"
        "							nnl++;\n"
        "							nw_boot_log(\n"
        "								\"G3: 68k Launch A9F2 CFM Upgrader PEF navLR\");\n"
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
        raise ValueError("mill patch missing: cpu-pef-navlr count=%s" % n)
    return text.replace(old, new, 1)


_PATCH = {
    "pef-frontwin": patch_cpu_pef_frontwin,
    "pef-getport": patch_cpu_pef_getport,
    "pef-repltxt": patch_cpu_pef_repltxt,
    "pef-drawpict": patch_cpu_pef_drawpict,
    "pef-inval": patch_cpu_pef_inval,
    "pef-beginupd": patch_cpu_pef_beginupd,
    "pef-endupd": patch_cpu_pef_endupd,
    "pef-copybits": patch_cpu_pef_copybits,
    "pef-updatedlg": patch_cpu_pef_updatedlg,
    "pef-eventav": patch_cpu_pef_eventav,
    "pef-stublr": patch_cpu_pef_stublr,
    "pef-textfont": patch_cpu_pef_textfont,
    "pef-textsize": patch_cpu_pef_textsize,
    "pef-textface": patch_cpu_pef_textface,
    "pef-disph": patch_cpu_pef_disph,
    "pef-dispp": patch_cpu_pef_dispp,
    "pef-hlock": patch_cpu_pef_hlock,
    "pef-hunlock": patch_cpu_pef_hunlock,
    "pef-cliprect": patch_cpu_pef_cliprect,
    "pef-eraserect": patch_cpu_pef_eraserect,
    "pef-hostlr": patch_cpu_pef_hostlr,
    "pef-moveto": patch_cpu_pef_moveto,
    "pef-drawstr": patch_cpu_pef_drawstr,
    "pef-strwidth": patch_cpu_pef_strwidth,
    "pef-rgbfore": patch_cpu_pef_rgbfore,
    "pef-setcurs": patch_cpu_pef_setcurs,
    "pef-setrect": patch_cpu_pef_setrect,
    "pef-setclip": patch_cpu_pef_setclip,
    "pef-getclip": patch_cpu_pef_getclip,
    "pef-disprd": patch_cpu_pef_disprd,
    "pef-hostlr2": patch_cpu_pef_hostlr2,
    "pef-getptrsz": patch_cpu_pef_getptrsz,
    "pef-hgetst": patch_cpu_pef_hgetst,
    "pef-hsetst": patch_cpu_pef_hsetst,
    "pef-delay": patch_cpu_pef_delay,
    "pef-lmticks": patch_cpu_pef_lmticks,
    "pef-fontinfo": patch_cpu_pef_fontinfo,
    "pef-pensize": patch_cpu_pef_pensize,
    "pef-ltog": patch_cpu_pef_ltog,
    "pef-newhclr": patch_cpu_pef_newhclr,
    "pef-navrun": patch_cpu_pef_navrun,
    "pef-navver": patch_cpu_pef_navver,
    "pef-navload": patch_cpu_pef_navload,
    "pef-navunld": patch_cpu_pef_navunld,
    "pef-navget": patch_cpu_pef_navget,
    "pef-navput": patch_cpu_pef_navput,
    "pef-navdisp": patch_cpu_pef_navdisp,
    "pef-findsym": patch_cpu_pef_findsym,
    "pef-getdf": patch_cpu_pef_getdf,
    "pef-navlr": patch_cpu_pef_navlr,
}


def apply_pef_splash(kind: str, cpu: Path) -> None:
    fn = _PATCH.get(kind)
    if fn is None:
        raise ValueError("unknown pef splash kind %s" % kind)
    cpu.write_text(fn(cpu.read_text()))
