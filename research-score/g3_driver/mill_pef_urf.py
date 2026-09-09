#!/usr/bin/env python3
"""50 leftover PEF mills: skip last UseResFile bl at 1010a94c, then more hosts.
Do not remill leftover:pef-stublr / skipunld / unldlr / trackgo."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, Optional[int], str]] = [
    ("pef-skipurf", None, "skipUrf"),
    ("pef-skipurf2", None, "skipUrf2"),
    ("pef-urflr", None, "urfLR"),
    ("pef-skipurf3", None, "skipUrf3"),
    ("pef-skipurf4", None, "skipUrf4"),
    ("pef-shutdwn", 6, "ShutDwnStart"),
    ("pef-addres", 142, "AddResource"),
    ("pef-aegetpd", 143, "AEGetParamDesc"),
    ("pef-setgz", 144, "SetGrowZone"),
    ("pef-getfcb", 151, "PBGetFCBInfoSync"),
    ("pef-getvinf2", 154, "PBGetVInfoSync"),
    ("pef-setcura5", 158, "SetCurrentA5"),
    ("pef-aelist", 163, "AECreateList"),
    ("pef-fspinfo", 164, "FSpGetFInfo"),
    ("pef-drawgrow", 165, "DrawGrowIcon"),
    ("pef-propenpg", 168, "PrOpenPage"),
    ("pef-drvstat", 169, "DriveStatus"),
    ("pef-prvalid", 171, "PrValidate"),
    ("pef-findctl", 174, "FindControl"),
    ("pef-fspcrres", 177, "FSpCreateResFile"),
    ("pef-getwref", 180, "GetWRefCon"),
    ("pef-setwcol", 190, "SetWinColor"),
    ("pef-prclpage", 191, "PrClosePage"),
    ("pef-fspcr", 192, "FSpCreate"),
    ("pef-growwin", 194, "GrowWindow"),
    ("pef-munger", 202, "Munger"),
    ("pef-unmount", 205, "UnmountVol"),
    ("pef-setctlref", 208, "SetControlReference"),
    ("pef-pbstat", 209, "PBStatusSync"),
    ("pef-getauxwin", 212, "GetAuxWin"),
    ("pef-setwref", 219, "SetWRefCon"),
    ("pef-custput", 220, "CustomPutFile"),
    ("pef-ngettrap", 227, "NGetTrapAddress"),
    ("pef-prstl", 231, "PrStlDialog"),
    ("pef-newctl2", 233, "GetNewControl"),
    ("pef-sysdir", 237, "GetSysDirection"),
    ("pef-aecoerce", 241, "AECoerceDesc"),
    ("pef-aeputptr", 242, "AEPutPtr"),
    ("pef-vremove", 244, "VRemove"),
    ("pef-getctlref", 245, "GetControlReference"),
    ("pef-getfore", 247, "GetForeColor"),
    ("pef-setresld", 249, "LMSetResLoad"),
    ("pef-prjob", 252, "PrJobDialog"),
    ("pef-vblq", 253, "LMGetVBLQueue"),
    ("pef-closeconn", 254, "CloseConnection"),
    ("pef-setdlgdef", 257, "SetDialogDefaultItem"),
    ("pef-aecdesc", 260, "AECreateDesc"),
    ("pef-updctl", 264, "UpdateControls"),
    ("pef-maxmem", 267, "MaxMem"),
    ("pef-catinfo", 269, "PBGetCatInfoSync"),
]
assert len(_HOSTS) == 50, len(_HOSTS)

PEF_URF_KINDS: List[str] = [k for k, _, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, _, tok in _HOSTS
}
_IDX: Dict[str, int] = {k: idx for k, idx, _ in _HOSTS if idx is not None}

_ANCHOR = "	(void)g3_pef_void_st;\n"
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_urf(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_URF_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_urf(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_urf(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_skipurf(text: str) -> str:
    m = _MARKERS["pef-skipurf"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x957fu))\n"
        "		vm_write_memory_4(ent + 0x957cu, 0x48000004u);\n"
        + _log_enter(m, "nsu")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipurf")


def patch_cpu_pef_skipurf2(text: str) -> str:
    m = _MARKERS["pef-skipurf2"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x957bu))\n"
        "		vm_write_memory_4(ent + 0x9578u, 0x4800000cu);\n"
        + _log_enter(m, "nsu2")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipurf2")


def patch_cpu_pef_urflr(text: str) -> str:
    m = _MARKERS["pef-urflr"]
    if m in text:
        return text
    old = (
        "				if (idx == 117u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a328u;\n"
    )
    new = (
        "				if (idx == 79u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a954u;\n"
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
        "				} else if (idx == 117u && (lr() == pc() || lr() == 0)) {\n"
        "					pc() = 0x1010a328u;\n"
    )
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: cpu-pef-urflr count=%s" % n)
    return text.replace(old, new, 1)


def patch_cpu_pef_skipurf3(text: str) -> str:
    m = _MARKERS["pef-skipurf3"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0x947bu))\n"
        "		vm_write_memory_4(ent + 0x9478u, 0x48000004u);\n"
        + _log_enter(m, "nsu3")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipurf3")


def patch_cpu_pef_skipurf4(text: str) -> str:
    m = _MARKERS["pef-skipurf4"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(ent + 0xb37u))\n"
        "		vm_write_memory_4(ent + 0xb34u, 0x48000004u);\n"
        + _log_enter(m, "nsu4")
    )
    return _insert_before_enter(text, block, "cpu-pef-skipurf4")


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
    "pef-skipurf": patch_cpu_pef_skipurf,
    "pef-skipurf2": patch_cpu_pef_skipurf2,
    "pef-urflr": patch_cpu_pef_urflr,
    "pef-skipurf3": patch_cpu_pef_skipurf3,
    "pef-skipurf4": patch_cpu_pef_skipurf4,
}


def apply_pef_urf(kind: str, cpu: Path) -> None:
    if kind in _SPECIAL:
        cpu.write_text(_SPECIAL[kind](cpu.read_text()))
        return
    idx = _IDX.get(kind)
    marker = _MARKERS.get(kind)
    if idx is None or marker is None:
        raise ValueError("unknown pef urf kind %s" % kind)
    text = cpu.read_text()
    if marker in text:
        return
    cpu.write_text(_insert_before_void(text, _host_block(idx, marker), "cpu-" + kind))
