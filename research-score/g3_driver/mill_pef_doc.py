#!/usr/bin/env python3
"""25 leftover PEF mills: stop fake highLevelEvent System Error 1, open the
Install Mac OS 9.2.1 document fork, host FS/AE the Finder launch would make.
Do not mill skip-68k. Do not remill leftover:pef-crf4/urf4/hsz4/idd04."""
from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, str]] = [
    ("pef-wne0", "wne0"),
    ("pef-ea0", "ea0"),
    ("pef-split79", "split79"),
    ("pef-srcdoc", "srcdoc"),
    ("pef-fspset", "fspset"),
    ("pef-crfsrc", "crfsrc"),
    ("pef-aegot", "aegot"),
    ("pef-catnm", "catnm"),
    ("pef-fsspec", "fsspec"),
    ("pef-fspnm", "fspnm"),
    ("pef-ners0", "ners0"),
    ("pef-str3500", "str3500"),
    ("pef-txt3502", "txt3502"),
    ("pef-gis3500", "gis3500"),
    ("pef-wneodoc", "wneodoc"),
    ("pef-use4", "use4"),
    ("pef-paramt", "paramt"),
    ("pef-setdt", "setdt"),
    ("pef-hgetv", "hgetv"),
    ("pef-fspdf", "fspdf"),
    ("pef-setvol", "setvol"),
    ("pef-pbdir", "pbdir"),
    ("pef-getvi", "getvi"),
    ("pef-noexit", "noexit"),
    ("pef-aecls", "aecls"),
]
assert len(_HOSTS) == 25, len(_HOSTS)

PEF_DOC_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_doc(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_DOC_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_doc(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_doc(kind: str, has_stamp) -> Optional[bool]:
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


def _replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise ValueError("mill patch missing: %s count=%s" % (label, n))
    return text.replace(old, new, 1)


def patch_cpu_pef_wne0(text: str) -> str:
    m = _MARKERS["pef-wne0"]
    if m in text:
        return text
    old = (
        "				if (!did_wne)\n"
        "					vm_write_memory_2(a4, 23);\n"
        "			}\n"
        "			r3 = did_wne ? 0 : 1;\n"
        "			did_wne = 1;\n"
    )
    new = (
        "			}\n"
        "			r3 = 0;\n"
        "			did_wne = 1;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-wne0")
    return _insert_after_enter(text, _log_enter(m, "nwne0"), "cpu-pef-wne0")


def patch_cpu_pef_ea0(text: str) -> str:
    m = _MARKERS["pef-ea0"]
    if m in text:
        return text
    old = "		} else if (idx == 121u)\n			r3 = 1;\n"
    new = "		} else if (idx == 121u)\n			r3 = 0;\n"
    text = _replace_once(text, old, new, "cpu-pef-ea0")
    return _insert_after_enter(text, _log_enter(m, "nea0"), "cpu-pef-ea0")


def patch_cpu_pef_split79(text: str) -> str:
    m = _MARKERS["pef-split79"]
    if m in text:
        return text
    old = "	} else if (idx == 149u || idx == 121u || idx == 79u) {\n"
    new = (
        "	} else if (idx == 79u) {\n"
        "		int16 ref = (int16)(a3 & 0xffffu);\n"
        "		if (ref == 4)\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "		else\n"
        "			g3_res_src_off = g3_rf_off;\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nurf;\n"
        "			if (nurf < 16) {\n"
        "				char buf[96];\n"
        "				nurf++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF UseResFile ref=%d src=%08x\",\n"
        "					 (int)ref, (unsigned)g3_res_src_off);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 149u || idx == 121u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-split79")
    return _insert_after_enter(text, _log_enter(m, "nsplit79"), "cpu-pef-split79")


def patch_cpu_pef_srcdoc(text: str) -> str:
    m = _MARKERS["pef-srcdoc"]
    if m in text:
        return text
    block = (
        "	g3_res_src_off = g3_doc_rf_off;\n" + _log_enter(m, "nsrcdoc")
    )
    return _insert_after_enter(text, block, "cpu-pef-srcdoc")


def patch_cpu_pef_fspset(text: str) -> str:
    m = _MARKERS["pef-fspset"]
    if m in text:
        return text
    old = "	if (idx == 262u) {\n		r3 = 4;\n"
    new = (
        "	if (idx == 262u) {\n"
        "		g3_res_src_off = g3_doc_rf_off;\n"
        "		r3 = 4;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fspset")
    return _insert_after_enter(text, _log_enter(m, "nfspset"), "cpu-pef-fspset")


def patch_cpu_pef_crfsrc(text: str) -> str:
    m = _MARKERS["pef-crfsrc"]
    if m in text:
        return text
    old = (
        "		if (idx == 270u || idx == 195u)\n"
        "			r3 = 3;\n"
    )
    new = (
        "		if (idx == 270u || idx == 195u)\n"
        "			r3 = (g3_res_src_off == g3_doc_rf_off) ? 4u : 3u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-crfsrc")
    return _insert_after_enter(text, _log_enter(m, "ncrfsrc"), "cpu-pef-crfsrc")


def patch_cpu_pef_aegot(text: str) -> str:
    m = _MARKERS["pef-aegot"]
    if m in text:
        return text
    old = (
        "	if (idx == 143u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 143u) {\n"
        "		uint32 d = a6 ? a6 : a5;\n"
        "		if (d && g3_ea_data(d + 7u)) {\n"
        "			vm_write_memory_4(d, 0x74787475u);\n"
        "			vm_write_memory_4(d + 4u, a3);\n"
        "		}\n"
        "		r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aegot")
    return _insert_after_enter(text, _log_enter(m, "naegot"), "cpu-pef-aegot")


def patch_cpu_pef_catnm(text: str) -> str:
    m = _MARKERS["pef-catnm"]
    if m in text:
        return text
    old = (
        "	if (idx == 269u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 269u) {\n"
        "		uint32 pb = a3, np;\n"
        "		if (pb && g3_ea_data(pb + 67u)) {\n"
        "			const char *nm = \"Install Mac OS 9.2.1\";\n"
        "			unsigned i, n = 20;\n"
        "			np = vm_read_memory_4(pb + 18u);\n"
        "			if (np && g3_ea_data(np + n))\n"
        "				vm_write_memory_1(np, (uint8)n);\n"
        "			if (np && g3_ea_data(np + n)) {\n"
        "				for (i = 0; i < n; i++)\n"
        "					vm_write_memory_1(np + 1u + i, (uint8)nm[i]);\n"
        "			}\n"
        "			vm_write_memory_2(pb + 22u, 0xffffu);\n"
        "		}\n"
        "		r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-catnm")
    return _insert_after_enter(text, _log_enter(m, "ncatnm"), "cpu-pef-catnm")


def _fsspec_fill() -> str:
    return (
        "		if (a3 && g3_ea_data(a3 + 70u)) {\n"
        "			const char *nm = \"Install Mac OS 9.2.1\";\n"
        "			unsigned i, n = 20;\n"
        "			vm_write_memory_2(a3, 0xffffu);\n"
        "			vm_write_memory_4(a3 + 2u, 1);\n"
        "			vm_write_memory_1(a3 + 6u, (uint8)n);\n"
        "			for (i = 0; i < n; i++)\n"
        "				vm_write_memory_1(a3 + 7u + i, (uint8)nm[i]);\n"
        "		}\n"
    )


def patch_cpu_pef_fsspec(text: str) -> str:
    m = _MARKERS["pef-fsspec"]
    if m in text:
        return text
    old = (
        "	if (idx == 262u) {\n"
        "		g3_res_src_off = g3_doc_rf_off;\n"
        "		r3 = 4;\n"
    )
    if old not in text:
        old = "	if (idx == 262u) {\n		r3 = 4;\n"
        new = "	if (idx == 262u) {\n" + _fsspec_fill() + "		r3 = 4;\n"
    else:
        new = (
            "	if (idx == 262u) {\n"
            + _fsspec_fill()
            + "		g3_res_src_off = g3_doc_rf_off;\n"
            "		r3 = 4;\n"
        )
    text = _replace_once(text, old, new, "cpu-pef-fsspec")
    return _insert_after_enter(text, _log_enter(m, "nfsspec"), "cpu-pef-fsspec")


def patch_cpu_pef_fspnm(text: str) -> str:
    m = _MARKERS["pef-fspnm"]
    if m in text:
        return text
    old = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenResFile r3=%08x\",\n"
        "					 (unsigned)r3);\n"
    )
    new = (
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenResFile r3=%08x nlen=%u\",\n"
        "					 (unsigned)r3,\n"
        "					 (unsigned)(a3 && g3_ea_data(a3 + 6u) ?\n"
        "					  vm_read_memory_1(a3 + 6u) : 0));\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fspnm")
    return _insert_after_enter(text, _log_enter(m, "nfspnm"), "cpu-pef-fspnm")


def patch_cpu_pef_ners0(text: str) -> str:
    m = _MARKERS["pef-ners0"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 92u || idx == 153u) {\n"
        "		uint32 doff = 0, ln = 0;\n"
        "		int16 rid = (int16)(a4 & 0xffffu);\n"
        "		if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    new = (
        "	} else if (idx == 92u || idx == 153u) {\n"
        "		uint32 doff = 0, ln = 0;\n"
        "		int16 rid = (int16)(a4 & 0xffffu);\n"
        "		if (a3 == 0x6e657273u && rid == 500)\n"
        "			r3 = 0;\n"
        "		else if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ners0")
    return _insert_after_enter(text, _log_enter(m, "nners0"), "cpu-pef-ners0")


def patch_cpu_pef_str3500(text: str) -> str:
    m = _MARKERS["pef-str3500"]
    if m in text:
        return text
    old = (
        "		if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    if "a3 == 0x6e657273u" in text:
        old = (
            "		else if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "			r3 = g3_res_plant(doff, ln);\n"
        )
        new = (
            "		else if (a3 == 0x53545223u && rid == 3500) {\n"
            "			g3_res_src_off = g3_doc_rf_off;\n"
            "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "				r3 = g3_res_plant(doff, ln);\n"
            "		} else if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "			r3 = g3_res_plant(doff, ln);\n"
        )
    else:
        new = (
            "		if (a3 == 0x53545223u && rid == 3500)\n"
            "			g3_res_src_off = g3_doc_rf_off;\n"
            "		if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "			r3 = g3_res_plant(doff, ln);\n"
        )
    text = _replace_once(text, old, new, "cpu-pef-str3500")
    return _insert_after_enter(text, _log_enter(m, "nstr3500"), "cpu-pef-str3500")


def patch_cpu_pef_txt3502(text: str) -> str:
    m = _MARKERS["pef-txt3502"]
    if m in text:
        return text
    needle = "		else if (a3 == 0x53545223u && rid == 3500) {\n"
    if needle in text:
        old = needle
        new = (
            "		else if (a3 == 0x54455854u && rid == 3502) {\n"
            "			g3_res_src_off = g3_doc_rf_off;\n"
            "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "				r3 = g3_res_plant(doff, ln);\n"
            "		} else if (a3 == 0x53545223u && rid == 3500) {\n"
        )
        text = _replace_once(text, old, new, "cpu-pef-txt3502")
    else:
        old = (
            "		if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "			r3 = g3_res_plant(doff, ln);\n"
        )
        new = (
            "		if (a3 == 0x54455854u && rid == 3502)\n"
            "			g3_res_src_off = g3_doc_rf_off;\n"
            "		if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
            "			r3 = g3_res_plant(doff, ln);\n"
        )
        text = _replace_once(text, old, new, "cpu-pef-txt3502")
    return _insert_after_enter(text, _log_enter(m, "ntxt3502"), "cpu-pef-txt3502")


def patch_cpu_pef_gis3500(text: str) -> str:
    m = _MARKERS["pef-gis3500"]
    if m in text:
        return text
    old = (
        "		if (g3_res_lookup(0x53545223u, sid, &doff, &ln) && ln >= 2u) {\n"
    )
    new = (
        "		if (sid == 3500)\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "		if (g3_res_lookup(0x53545223u, sid, &doff, &ln) && ln >= 2u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gis3500")
    return _insert_after_enter(text, _log_enter(m, "ngis3500"), "cpu-pef-gis3500")


def patch_cpu_pef_wneodoc(text: str) -> str:
    m = _MARKERS["pef-wneodoc"]
    if m in text:
        return text
    old = (
        "			}\n"
        "			r3 = 0;\n"
        "			did_wne = 1;\n"
    )
    new = (
        "				if (!did_wne) {\n"
        "					vm_write_memory_2(a4, 23);\n"
        "					vm_write_memory_4(a4 + 2u, 0x6f646f63u);\n"
        "				}\n"
        "			}\n"
        "			r3 = did_wne ? 0 : 1;\n"
        "			did_wne = 1;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-wneodoc")
    return _insert_after_enter(text, _log_enter(m, "nwneodoc"), "cpu-pef-wneodoc")


def patch_cpu_pef_use4(text: str) -> str:
    m = _MARKERS["pef-use4"]
    if m in text:
        return text
    old = (
        "		if (ref == 4)\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "		else\n"
        "			g3_res_src_off = g3_rf_off;\n"
    )
    new = (
        "		g3_res_src_off = g3_doc_rf_off;\n"
        "		(void)ref;\n"
    )
    if old not in text:
        return _insert_after_enter(text, _log_enter(m, "nuse4"), "cpu-pef-use4")
    text = _replace_once(text, old, new, "cpu-pef-use4")
    return _insert_after_enter(text, _log_enter(m, "nuse4"), "cpu-pef-use4")


def _stamp_only(kind: str, old: str, new: str, var: str) -> Callable[[str], str]:
    def fn(text: str) -> str:
        m = _MARKERS[kind]
        if m in text:
            return text
        if old and old in text:
            text = _replace_once(text, old, new, "cpu-" + kind)
        return _insert_after_enter(text, _log_enter(m, var), "cpu-" + kind)

    return fn


patch_cpu_pef_paramt = _stamp_only(
    "pef-paramt",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ParamText\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ParamText\");\n",
    "nparamt",
)
patch_cpu_pef_setdt = _stamp_only(
    "pef-setdt",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetDialogItemText\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetDialogItemText\");\n",
    "nsetdt",
)
patch_cpu_pef_hgetv = _stamp_only(
    "pef-hgetv",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HGetVol\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF HGetVol\");\n",
    "nhgetv",
)
patch_cpu_pef_fspdf = _stamp_only(
    "pef-fspdf",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenDF\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenDF\");\n",
    "nfspdf",
)
patch_cpu_pef_setvol = _stamp_only(
    "pef-setvol",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetVol\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF SetVol\");\n",
    "nsetvol",
)
patch_cpu_pef_getvi = _stamp_only(
    "pef-getvi",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetVInfoSync\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetVInfoSync\");\n",
    "ngetvi",
)
patch_cpu_pef_noexit = _stamp_only(
    "pef-noexit",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ExitToShell\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF ExitToShell\");\n",
    "nnoexit",
)
patch_cpu_pef_aecls = _stamp_only(
    "pef-aecls",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF AEGetParamDesc\");\n",
    "					\"G3: 68k Launch A9F2 CFM Upgrader PEF AEGetParamDesc\");\n",
    "naecls",
)


def patch_cpu_pef_pbdir(text: str) -> str:
    m = _MARKERS["pef-pbdir"]
    if m in text:
        return text
    old = (
        "			vm_write_memory_2(pb + 22u, 0xffffu);\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetCatInfoSync\");\n"
    )
    if old not in text:
        return _insert_after_enter(text, _log_enter(m, "npbdir"), "cpu-pef-pbdir")
    new = (
        "			vm_write_memory_2(pb + 22u, 0xffffu);\n"
        "			vm_write_memory_2(pb + 30u, 0x4000u);\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetCatInfoSync\");\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pbdir")
    return _insert_after_enter(text, _log_enter(m, "npbdir"), "cpu-pef-pbdir")


_PATCH: Dict[str, Callable[[str], str]] = {
    "pef-wne0": patch_cpu_pef_wne0,
    "pef-ea0": patch_cpu_pef_ea0,
    "pef-split79": patch_cpu_pef_split79,
    "pef-srcdoc": patch_cpu_pef_srcdoc,
    "pef-fspset": patch_cpu_pef_fspset,
    "pef-crfsrc": patch_cpu_pef_crfsrc,
    "pef-aegot": patch_cpu_pef_aegot,
    "pef-catnm": patch_cpu_pef_catnm,
    "pef-fsspec": patch_cpu_pef_fsspec,
    "pef-fspnm": patch_cpu_pef_fspnm,
    "pef-ners0": patch_cpu_pef_ners0,
    "pef-str3500": patch_cpu_pef_str3500,
    "pef-txt3502": patch_cpu_pef_txt3502,
    "pef-gis3500": patch_cpu_pef_gis3500,
    "pef-wneodoc": patch_cpu_pef_wneodoc,
    "pef-use4": patch_cpu_pef_use4,
    "pef-paramt": patch_cpu_pef_paramt,
    "pef-setdt": patch_cpu_pef_setdt,
    "pef-hgetv": patch_cpu_pef_hgetv,
    "pef-fspdf": patch_cpu_pef_fspdf,
    "pef-setvol": patch_cpu_pef_setvol,
    "pef-pbdir": patch_cpu_pef_pbdir,
    "pef-getvi": patch_cpu_pef_getvi,
    "pef-noexit": patch_cpu_pef_noexit,
    "pef-aecls": patch_cpu_pef_aecls,
}


def apply_pef_docq(kind: str, cpu: Path) -> None:
    fn = _PATCH.get(kind)
    if fn is None:
        raise ValueError("unknown pef doc kind %s" % kind)
    cpu.write_text(fn(cpu.read_text()))
