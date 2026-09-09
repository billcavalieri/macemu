#!/usr/bin/env python3
"""100 leftover PEF mills: special hosts then void-idx no-ops. No skip-68k."""
from __future__ import annotations

import re
from pathlib import Path
from typing import List, Optional

HOSTED_PEF_IDX = {
    1, 30, 31, 34, 37, 39, 53, 64, 68, 70, 79, 92, 95, 97, 110, 121, 125,
    131, 134, 149, 153, 155, 157, 159, 160, 170, 173, 184, 185, 189, 193,
    195, 214, 223, 225, 239, 243, 250, 256, 270, 285, 287,
}
# Hosted by special mills in this batch, not void-bits.
SPECIAL_IDX = {28, 111, 162, 268, 272}
SPECIAL_KINDS = [
    "pef-nrdblr",
    "pef-getditm",
    "pef-hsz",
    "pef-memerr",
    "pef-getind",
    "pef-nts",
]
MARKER_NRDBLR = "G3: 68k Launch A9F2 CFM Upgrader PEF nrdBlr"
MARKER_GETDITM = "G3: 68k Launch A9F2 CFM Upgrader PEF GetDialogItem"
MARKER_HSZ = "G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize"
MARKER_MEMERR = "G3: 68k Launch A9F2 CFM Upgrader PEF MemError"
MARKER_GETIND = "G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString"
MARKER_NTS = "G3: 68k Launch A9F2 CFM Upgrader PEF NumToString"
MARKER_VOID = "G3: 68k Launch A9F2 CFM Upgrader PEF void"

# Prefer splash/QD/window imports, then the rest of unhosted 0..294.
_PREFERRED = [
    42, 46, 50, 25, 176, 246, 7, 265, 217, 166, 33, 199, 186, 127, 84, 117,
    276, 108, 126, 86, 100, 26, 41, 178, 51, 55, 49, 147, 156, 73, 116, 81,
    69, 0, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 27, 29, 32, 35, 36, 38, 40, 44, 45, 47, 48, 52, 54, 56,
    57, 58, 59, 60, 61, 62, 63, 65, 66, 67, 71, 72, 74, 75, 76, 77, 78, 80,
    82, 83, 85, 87, 88, 89, 90, 91, 93, 94, 96, 98, 99, 101, 102, 103, 104,
    105, 106, 107, 109, 112, 113, 114, 115, 118, 119, 120, 122, 123, 124,
]
_seen = set()
PEF_VOID_IDXS: List[int] = []
for i in _PREFERRED:
    if i in HOSTED_PEF_IDX or i in SPECIAL_IDX or i in _seen:
        continue
    if 0 <= i <= 294:
        _seen.add(i)
        PEF_VOID_IDXS.append(i)
for i in range(295):
    if i in HOSTED_PEF_IDX or i in SPECIAL_IDX or i in _seen:
        continue
    _seen.add(i)
    PEF_VOID_IDXS.append(i)
PEF_VOID_IDXS = PEF_VOID_IDXS[:94]
PEF_VOID_KINDS = ["pef-i%03d" % i for i in PEF_VOID_IDXS]
PEF_BATCH_KINDS = SPECIAL_KINDS + PEF_VOID_KINDS
assert len(PEF_BATCH_KINDS) == 100, len(PEF_BATCH_KINDS)


def pef_void_idx(kind: str) -> Optional[int]:
    if kind.startswith("pef-i") and kind[5:].isdigit() and len(kind) == 8:
        return int(kind[5:])
    return None


def pef_void_stamp(idx: int) -> str:
    return " i%03d" % idx


def next_pef_batch(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_BATCH_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_batch(kind: str, text: str) -> Optional[bool]:
    if kind == "pef-nrdblr":
        return MARKER_NRDBLR in text
    if kind == "pef-getditm":
        return MARKER_GETDITM in text
    if kind == "pef-hsz":
        return MARKER_HSZ in text
    if kind == "pef-memerr":
        return MARKER_MEMERR in text
    if kind == "pef-getind":
        return MARKER_GETIND in text
    if kind == "pef-nts":
        return MARKER_NTS in text
    idx = pef_void_idx(kind)
    if idx is None:
        return None
    return pef_void_stamp(idx) in text


def mill_binary_match_pef_batch(kind: str, has_stamp) -> Optional[bool]:
    if kind == "pef-nrdblr":
        return has_stamp(MARKER_NRDBLR)
    if kind == "pef-getditm":
        return has_stamp(MARKER_GETDITM)
    if kind == "pef-hsz":
        return has_stamp(MARKER_HSZ)
    if kind == "pef-memerr":
        return has_stamp(MARKER_MEMERR)
    if kind == "pef-getind":
        return has_stamp(MARKER_GETIND)
    if kind == "pef-nts":
        return has_stamp(MARKER_NTS)
    idx = pef_void_idx(kind)
    if idx is None:
        return None
    return has_stamp(pef_void_stamp(idx))


def _replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(old, new, 1)


def patch_cpu_pef_nrdblr(text: str) -> str:
    if MARKER_NRDBLR in text:
        return text
    old = (
        "	} else if (idx == 159u) {\n"
        "		/* NewRoutineDescriptor: PPC UPP is the ProcPtr. */\n"
        "		r3 = a3;\n"
    )
    new = (
        "	} else if (idx == 159u) {\n"
        "		/* NewRoutineDescriptor: UPP is a blr stub, not TOC. */\n"
        "		r3 = g3_pef_newptr(16u);\n"
        "		if (r3 && g3_ea_data(r3 + 3u))\n"
        "			vm_write_memory_4(r3, 0x4e800020u);\n"
        "		if (!r3)\n"
        "			r3 = a3;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nbl;\n"
        "			if (nbl < 8) {\n"
        "				char buf[96];\n"
        "				nbl++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF nrdBlr p=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    return _replace_once(text, old, new, "cpu-pef-nrdblr")


def patch_cpu_pef_getditm(text: str) -> str:
    if MARKER_GETDITM in text:
        return text
    old_sig = (
        "static uint32 g3_pef_host(uint32 idx, uint32 a3, uint32 a4, uint32 a5)\n"
    )
    new_sig = (
        "static uint32 g3_pef_host(uint32 idx, uint32 a3, uint32 a4, uint32 a5,\n"
        "			 uint32 a6, uint32 a7)\n"
    )
    text = _replace_once(text, old_sig, new_sig, "cpu-pef-getditm-sig")
    old_call = (
        "				r3 = g3_pef_host(idx, gpr(3), gpr(4), gpr(5));\n"
    )
    new_call = (
        "				r3 = g3_pef_host(idx, gpr(3), gpr(4), gpr(5),\n"
        "						 gpr(6), gpr(7));\n"
    )
    text = _replace_once(text, old_call, new_call, "cpu-pef-getditm-call")
    old = (
        "	} else if (idx == 30u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	} else if (idx == 111u) {\n"
        "		if (a5 && g3_ea_data(a5 + 1u))\n"
        "			vm_write_memory_2(a5, 0);\n"
        "		if (a6 && g3_ea_data(a6 + 3u))\n"
        "			vm_write_memory_4(a6, 0);\n"
        "		if (a7 && g3_ea_data(a7 + 7u)) {\n"
        "			vm_write_memory_2(a7, 0);\n"
        "			vm_write_memory_2(a7 + 2u, 0);\n"
        "			vm_write_memory_2(a7 + 4u, 44);\n"
        "			vm_write_memory_2(a7 + 6u, 506);\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngdi;\n"
        "			if (ngdi < 8) {\n"
        "				ngdi++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetDialogItem\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 30u) {\n"
        "		r3 = 0;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-getditm")


def patch_cpu_pef_hsz(text: str) -> str:
    if MARKER_HSZ in text:
        return text
    old = (
        "	} else if (idx == 111u) {\n"
    )
    new = (
        "	} else if (idx == 28u) {\n"
        "		uint32 p = 0;\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			p = vm_read_memory_4(a3);\n"
        "		r3 = p ? 64u : 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhz;\n"
        "			if (nhz < 8) {\n"
        "				char buf[96];\n"
        "				nhz++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize n=%u\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 111u) {\n"
    )
    if old not in text:
        old = (
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
        new = (
            "	} else if (idx == 28u) {\n"
            "		uint32 p = 0;\n"
            "		if (a3 && g3_ea_data(a3 + 3u))\n"
            "			p = vm_read_memory_4(a3);\n"
            "		r3 = p ? 64u : 0;\n"
            "#if NW_BOOT_LOG\n"
            "		{\n"
            "			static unsigned nhz;\n"
            "			if (nhz < 8) {\n"
            "				char buf[96];\n"
            "				nhz++;\n"
            "				snprintf(buf, sizeof(buf),\n"
            "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize n=%u\",\n"
            "					 (unsigned)r3);\n"
            "				nw_boot_log(buf);\n"
            "			}\n"
            "		}\n"
            "#endif\n"
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
    return _replace_once(text, old, new, "cpu-pef-hsz")


def patch_cpu_pef_memerr(text: str) -> str:
    if MARKER_MEMERR in text:
        return text
    old = (
        "	} else if (idx == 28u) {\n"
    )
    new = (
        "	} else if (idx == 272u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nme;\n"
        "			if (nme < 8) {\n"
        "				nme++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF MemError\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 28u) {\n"
    )
    if old not in text:
        old = (
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
        new = (
            "	} else if (idx == 272u) {\n"
            "		r3 = 0;\n"
            "#if NW_BOOT_LOG\n"
            "		{\n"
            "			static unsigned nme;\n"
            "			if (nme < 8) {\n"
            "				nme++;\n"
            "				nw_boot_log(\n"
            "					\"G3: 68k Launch A9F2 CFM Upgrader PEF MemError\");\n"
            "			}\n"
            "		}\n"
            "#endif\n"
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
    return _replace_once(text, old, new, "cpu-pef-memerr")


def patch_cpu_pef_getind(text: str) -> str:
    if MARKER_GETIND in text:
        return text
    old = (
        "	} else if (idx == 272u) {\n"
    )
    new = (
        "	} else if (idx == 162u) {\n"
        "		if (a3 && g3_ea_data(a3))\n"
        "			vm_write_memory_1(a3, 0);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngi;\n"
        "			if (ngi < 8) {\n"
        "				ngi++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 272u) {\n"
    )
    if old not in text:
        old = (
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
        new = (
            "	} else if (idx == 162u) {\n"
            "		if (a3 && g3_ea_data(a3))\n"
            "			vm_write_memory_1(a3, 0);\n"
            "		r3 = 0;\n"
            "#if NW_BOOT_LOG\n"
            "		{\n"
            "			static unsigned ngi;\n"
            "			if (ngi < 8) {\n"
            "				ngi++;\n"
            "				nw_boot_log(\n"
            "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetIndString\");\n"
            "			}\n"
            "		}\n"
            "#endif\n"
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
    return _replace_once(text, old, new, "cpu-pef-getind")


def patch_cpu_pef_nts(text: str) -> str:
    if MARKER_NTS in text:
        return text
    old = (
        "	} else if (idx == 162u) {\n"
    )
    new = (
        "	} else if (idx == 268u) {\n"
        "		uint32 s = a4 ? a4 : a3;\n"
        "		if (s && g3_ea_data(s + 1u)) {\n"
        "			vm_write_memory_1(s, 1);\n"
        "			vm_write_memory_1(s + 1u, '0');\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nns;\n"
        "			if (nns < 8) {\n"
        "				nns++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 162u) {\n"
    )
    if old not in text:
        old = (
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
        new = (
            "	} else if (idx == 268u) {\n"
            "		uint32 s = a4 ? a4 : a3;\n"
            "		if (s && g3_ea_data(s + 1u)) {\n"
            "			vm_write_memory_1(s, 1);\n"
            "			vm_write_memory_1(s + 1u, '0');\n"
            "		}\n"
            "		r3 = 0;\n"
            "#if NW_BOOT_LOG\n"
            "		{\n"
            "			static unsigned nns;\n"
            "			if (nns < 8) {\n"
            "				nns++;\n"
            "				nw_boot_log(\n"
            "					\"G3: 68k Launch A9F2 CFM Upgrader PEF NumToString\");\n"
            "			}\n"
            "		}\n"
            "#endif\n"
            "	} else if (idx == 30u) {\n"
            "		r3 = 0;\n"
        )
    return _replace_once(text, old, new, "cpu-pef-nts")


def _void_infra(text: str) -> str:
    if MARKER_VOID in text and "g3_pef_void_bits" in text:
        return text
    old = (
        "static uint32 g3_pef_host(uint32 idx, uint32 a3, uint32 a4, uint32 a5"
    )
    # match both 5-arg and 7-arg signatures
    m = re.search(
        r"static uint32 g3_pef_host\(uint32 idx, uint32 a3, uint32 a4, uint32 a5(?:,\n\t\t\t uint32 a6, uint32 a7)?\)\n\{\n\tuint32 r3 = 0;\n",
        text,
    )
    if not m:
        raise ValueError("mill patch missing: cpu-pef-void-infra")
    old = m.group(0)
    new = (
        "static uint32 g3_pef_void_bits[10];\n"
        "static const char g3_pef_void_st[] =\n"
        "	\"G3: 68k Launch A9F2 CFM Upgrader PEF void\"\n"
        "	\"\"\n"
        "	;\n"
        + old
    )
    text = text.replace(old, new, 1)
    old2 = (
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	if (idx == 1u) {\n"
    )
    new2 = (
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	(void)g3_pef_void_st;\n"
        "	if (idx < 320u &&\n"
        "	    (g3_pef_void_bits[idx >> 5] &\n"
        "	     (1u << (idx & 31u)))) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nv;\n"
        "			if (nv < 16) {\n"
        "				char buf[96];\n"
        "				nv++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF void idx=%u\",\n"
        "					 (unsigned)idx);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
        "	if (idx == 1u) {\n"
    )
    return _replace_once(text, old2, new2, "cpu-pef-void-check")


def patch_cpu_pef_void(text: str, idx: int) -> str:
    text = _void_infra(text)
    tag = "i%03d" % idx
    # Live snprintf format so the unique stamp is not stripped.
    m = re.search(
        r'"G3: 68k Launch A9F2 CFM Upgrader PEF void idx=%u[^"]*"',
        text,
    )
    if not m:
        raise ValueError("mill patch missing: cpu-pef-void-fmt")
    fmt = m.group(0)
    if tag not in fmt:
        # "... void idx=%u x" -> "... void idx=%u i042 x"
        if fmt.endswith(' x"'):
            fmt2 = fmt[:-3] + " " + tag + ' x"'
        else:
            fmt2 = fmt[:-1] + " " + tag + '"'
        text = text[: m.start()] + fmt2 + text[m.end() :]
    word = idx >> 5
    bit = idx & 31
    # set bit in static array if initialized; else bits are BSS zero —
    # write a one-shot assign in the void check
    assign = "g3_pef_void_bits[%uu] |= 1u << %uu;" % (word, bit)
    if assign in text:
        return text
    oldc = (
        "	if (idx < 320u &&\n"
        "	    (g3_pef_void_bits[idx >> 5] &\n"
        "	     (1u << (idx & 31u)))) {\n"
    )
    # collect all assigns already present
    assigns = re.findall(
        r"g3_pef_void_bits\[(\d+)u\] \|= 1u << (\d+)u;", text
    )
    newc = oldc
    if not assigns:
        newc = "	" + assign + "\n" + oldc
    else:
        newc = "	" + assign + "\n" + oldc
    # if oldc appears once, insert assign immediately before it once
    if text.count(oldc) != 1:
        raise ValueError("mill patch missing: cpu-pef-void-bit")
    # avoid duplicating the if
    if assign not in text:
        text = text.replace(oldc, "	" + assign + "\n" + oldc, 1)
    return text


def apply_pef_batch(kind: str, cpu: Path) -> None:
    text = cpu.read_text()
    if kind == "pef-nrdblr":
        text = patch_cpu_pef_nrdblr(text)
    elif kind == "pef-getditm":
        text = patch_cpu_pef_getditm(text)
    elif kind == "pef-hsz":
        text = patch_cpu_pef_hsz(text)
    elif kind == "pef-memerr":
        text = patch_cpu_pef_memerr(text)
    elif kind == "pef-getind":
        text = patch_cpu_pef_getind(text)
    elif kind == "pef-nts":
        text = patch_cpu_pef_nts(text)
    else:
        idx = pef_void_idx(kind)
        if idx is None:
            raise ValueError("unknown pef batch kind %s" % kind)
        text = patch_cpu_pef_void(text, idx)
    cpu.write_text(text)
