#!/usr/bin/env python3
"""Real leftover mills: document-open, inss/chsk, tsqc, GetNewDialog
glue, full DOC tsqc 128, GetDrvQHdr QHdr, then GetDiskFragment connID.
Do not mill skip-68k or wait-cmp. Do not skip 0x5c86c-0x5c8c0."""
from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

_HOSTS: List[Tuple[str, str]] = [
    ("pef-docflag", "docflag"),
    ("pef-aeproc", "aeproc"),
    ("pef-fspnow", "fspnow"),
    ("pef-inss", "inss"),
    ("pef-catfd", "catfd"),
    ("pef-tsqc", "tsqc"),
    ("pef-gndtv", "gndtv"),
    ("pef-tsqcf", "tsqcf"),
    ("pef-dqhdr", "dqhdr"),
    ("pef-gdf2", "gdf2"),
    ("pef-gdf3", "gdf3"),
    ("pef-hsz64", "hsz64"),
    ("pef-nersp", "nersp"),
    ("pef-gdfnm", "gdfnm"),
    ("pef-gdfn6", "gdfn6"),
    ("pef-welpef", "welpef"),
    ("pef-cuparg", "cuparg"),
    ("pef-cupld", "cupld"),
    ("pef-welr2", "welr2"),
    ("pef-welarg", "welarg"),
    ("pef-cupgis", "cupgis"),
    ("pef-cupall", "cupall"),
    ("pef-fw128", "fw128"),
    ("pef-cuptxt", "cuptxt"),
    ("pef-welrf", "welrf"),
    ("pef-pict3500", "pict3500"),
    ("pef-drawwel", "drawwel"),
    ("pef-xrgbff", "xrgbff"),
    ("pef-fbpres", "fbpres"),
    ("pef-dd3500", "dd3500"),
    ("pef-noddfill", "noddfill"),
    ("pef-cupgmd", "cupgmd"),
    ("pef-welbody", "welbody"),
    ("pef-cupgnd", "cupgnd"),
    ("pef-ditlh", "ditlh"),
    ("pef-cupaff", "cupaff"),
]
assert len(_HOSTS) == 36, len(_HOSTS)

PEF_OPEN_KINDS: List[str] = [k for k, _ in _HOSTS]
_MARKERS: Dict[str, str] = {
    k: "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok for k, tok in _HOSTS
}
_ENTER = "	g3_did_pef_enter = 1;\n"


def next_pef_open(tested_keys: set, reverted: set) -> Optional[str]:
    for k in PEF_OPEN_KINDS:
        key = "leftover:%s" % k
        if key not in tested_keys and key not in reverted:
            return k
    return None


def is_applied_pef_open(kind: str, text: str) -> Optional[bool]:
    m = _MARKERS.get(kind)
    if m is None:
        return None
    return m in text


def mill_binary_match_pef_open(kind: str, has_stamp) -> Optional[bool]:
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


def patch_cpu_pef_docflag(text: str) -> str:
    m = _MARKERS["pef-docflag"]
    if m in text:
        return text
    block = (
        "	if (g3_ea_data(toc + 0xbbfu))\n"
        "		vm_write_memory_4(toc + 0xbbcu, 1u);\n"
        "	g3_res_src_off = g3_doc_rf_off;\n"
        + _log_enter(m, "ndocflag")
    )
    return _insert_after_enter(text, block, "cpu-pef-docflag")


def patch_cpu_pef_aeproc(text: str) -> str:
    m = _MARKERS["pef-aeproc"]
    if m in text:
        return text
    old = "		} else if (idx == 121u)\n			r3 = 0;\n"
    new = (
        "		} else if (idx == 121u) {\n"
        "			if (g3_ea_data(0x10115bbfu))\n"
        "				vm_write_memory_4(0x10115bbcu, 1u);\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "			r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned naep;\n"
        "				if (naep < 8) {\n"
        "					naep++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF AEProcess\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aeproc")
    return _insert_after_enter(text, _log_enter(m, "naeproc"), "cpu-pef-aeproc")


def patch_cpu_pef_fspnow(text: str) -> str:
    m = _MARKERS["pef-fspnow"]
    if m in text:
        return text
    old = (
        "	if (idx == 262u) {\n"
        "		if (a3 && g3_ea_data(a3 + 70u)) {\n"
    )
    if old not in text:
        old = (
            "	if (idx == 262u) {\n"
            "		g3_res_src_off = g3_doc_rf_off;\n"
            "		r3 = 4;\n"
        )
        new = (
            "	if (idx == 262u) {\n"
            "		g3_res_src_off = g3_doc_rf_off;\n"
            "		if (g3_ea_data(0x10115bbfu))\n"
            "			vm_write_memory_4(0x10115bbcu, 1u);\n"
            "		r3 = 4;\n"
        )
        text = _replace_once(text, old, new, "cpu-pef-fspnow")
    else:
        new = (
            "	if (idx == 262u) {\n"
            "		if (g3_ea_data(0x10115bbfu))\n"
            "			vm_write_memory_4(0x10115bbcu, 1u);\n"
            "		if (a3 && g3_ea_data(a3 + 70u)) {\n"
        )
        text = _replace_once(text, old, new, "cpu-pef-fspnow")
    return _insert_after_enter(text, _log_enter(m, "nfspnow"), "cpu-pef-fspnow")


def patch_cpu_pef_inss(text: str) -> str:
    m = _MARKERS["pef-inss"]
    if m in text:
        return text
    old = (
        "			vm_write_memory_2(pb + 22u, 0xffffu);\n"
        "			vm_write_memory_2(pb + 30u, 0x4000u);\n"
    )
    new = (
        "			vm_write_memory_2(pb + 22u, 0xffffu);\n"
        "			vm_write_memory_2(pb + 30u, 0);\n"
        "			vm_write_memory_4(pb + 32u, 0x696e7373u);\n"
        "			vm_write_memory_4(pb + 36u, 0x6368736bu);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-inss")
    return _insert_after_enter(text, _log_enter(m, "ninss"), "cpu-pef-inss")


def patch_cpu_pef_catfd(text: str) -> str:
    m = _MARKERS["pef-catfd"]
    if m in text:
        return text
    old = (
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetCatInfoSync\");\n"
    )
    new = (
        "			if (n < 8) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF PBGetCatInfoSync ty=%08x cr=%08x\",\n"
        "					 (unsigned)(pb && g3_ea_data(pb + 35u) ?\n"
        "					  vm_read_memory_4(pb + 32u) : 0),\n"
        "					 (unsigned)(pb && g3_ea_data(pb + 39u) ?\n"
        "					  vm_read_memory_4(pb + 36u) : 0));\n"
        "				nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-catfd")
    return _insert_after_enter(text, _log_enter(m, "ncatfd"), "cpu-pef-catfd")


def patch_cpu_pef_tsqc(text: str) -> str:
    m = _MARKERS["pef-tsqc"]
    if m in text:
        return text
    old = (
        "		if (a3 == 0x6e657273u && rid == 500)\n"
        "			r3 = 0;\n"
        "		else if (a3 == 0x54455854u && rid == 3502) {\n"
    )
    new = (
        "		if (a3 == 0x74737163u && rid == 128) {\n"
        "			static const uint8 tsqc[10] = {\n"
        "				0, 2, 0, 0, 0, 0x8a, 0, 0x93, 0, 0\n"
        "			};\n"
        "			r3 = g3_plant_raw(tsqc, 10u);\n"
        "		} else if (a3 == 0x6e657273u && rid == 500)\n"
        "			r3 = 0;\n"
        "		else if (a3 == 0x54455854u && rid == 3502) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-tsqc")
    return _insert_after_enter(text, _log_enter(m, "ntsqc"), "cpu-pef-tsqc")


def patch_cpu_pef_gndtv(text: str) -> str:
    """KEEP 17181 DSI DAR=80210008 SRR0=10113b24: GetNewDialog glue
    lwz r12,0x218(r2) then lwz r0,0(r12). toc+0x218 is not a TVector
    so idx 134 never runs. Point it at stub idx 134 and force r2=toc
    at the glue PC. Do not skip 0x5c86c."""
    m = _MARKERS["pef-gndtv"]
    if m in text:
        return text
    old = (
        "			if (g3_did_pef_enter &&\n"
        "			    pc() == RAMBase + 0x116000u) {\n"
    )
    new = (
        "			if (g3_did_pef_enter && pc() == 0x10113b1cu) {\n"
        "				const uint32 tv =\n"
        "					RAMBase + 0x116004u + 134u * 8u;\n"
        "				if (gpr(2) != 0x10115000u)\n"
        "					gpr(2) = 0x10115000u;\n"
        "				if (g3_ea_data(0x1011521bu))\n"
        "					vm_write_memory_4(0x10115218u, tv);\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngndtv;\n"
        "					if (ngndtv < 8) {\n"
        "						char buf[96];\n"
        "						ngndtv++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gndtv id=%u\",\n"
        "							 (unsigned)(gpr(3) & 0xffffu));\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			}\n"
        "			if (g3_did_pef_enter &&\n"
        "			    pc() == RAMBase + 0x116000u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gndtv")
    block = (
        "	if (g3_ea_data(0x1011521bu))\n"
        "		vm_write_memory_4(0x10115218u,\n"
        "				 RAMBase + 0x116004u + 134u * 8u);\n"
        + _log_enter(m, "ngndtv0")
    )
    return _insert_after_enter(text, block, "cpu-pef-gndtv")


def patch_cpu_pef_tsqcf(text: str) -> str:
    """KEEP 17182: 10108878 (main bl 101014cc) parses tsqc+18.
    leftover:pef-tsqc planted 10 bytes so GoToNextPlugin returned
    501. DOC tsqc 128 is 246 bytes (Welcome/TgtSelect/...). Use
    g3_res_plant. Do not remill leftover:pef-tsqc. Do not skip-68k."""
    m = _MARKERS["pef-tsqcf"]
    if m in text:
        return text
    old = (
        "		if (a3 == 0x74737163u && rid == 128) {\n"
        "			static const uint8 tsqc[10] = {\n"
        "				0, 2, 0, 0, 0, 0x8a, 0, 0x93, 0, 0\n"
        "			};\n"
        "			r3 = g3_plant_raw(tsqc, 10u);\n"
        "		} else if (a3 == 0x6e657273u && rid == 500)\n"
    )
    new = (
        "		if (a3 == 0x74737163u && (rid == 128 || rid == 129)) {\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "				r3 = g3_res_plant(doff, ln);\n"
        "		} else if (a3 == 0x6e657273u && rid == 500)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-tsqcf")
    return _insert_after_enter(text, _log_enter(m, "ntsqcf"), "cpu-pef-tsqcf")


def patch_cpu_pef_dqhdr(text: str) -> str:
    """KEEP 17183 DSI DAR=48f90005 SRR0=101044f8: IsEjectable
    lwz r31,2(r3) after GetDrvQHdr idx 279 returned 0, then lha 6(r31)
    walks garbage. Plant QHdr with qHead=0. Do not skip 0x5c86c."""
    m = _MARKERS["pef-dqhdr"]
    if m in text:
        return text
    old = (
        "	if (idx == 279u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetDrvQHdr\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    new = (
        "	if (idx == 279u) {\n"
        "		const uint32 q = RAMBase + 0x4c100u;\n"
        "		if (g3_ea_data(q + 15u)) {\n"
        "			uint32 i;\n"
        "			for (i = 0; i < 16u; i += 4u)\n"
        "				vm_write_memory_4(q + i, 0);\n"
        "			r3 = q;\n"
        "		} else\n"
        "			r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetDrvQHdr q=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-dqhdr")
    return _insert_after_enter(text, _log_enter(m, "ndqhdr"), "cpu-pef-dqhdr")


def patch_cpu_pef_gdf2(text: str) -> str:
    """KEEP 17184: GetDiskFragment idx 114 wrote loadFlags (r7=5) as a
    pointer (lowmem +1) and left connID/mainAddr unset. Intercept glue
    10114d64: plant connID + mainAddr=stub, r3=0, skip idx 114.
    Do not remill leftover:pef-getdf. Do not skip 0x5c86c."""
    m = _MARKERS["pef-gdf2"]
    if m in text:
        return text
    old = (
        "			if (g3_did_pef_enter &&\n"
        "			    pc() == RAMBase + 0x116000u) {\n"
    )
    new = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
        "				uint32 conn = g3_pef_newptr(16u);\n"
        "				uint32 mainp = RAMBase + 0x116000u;\n"
        "				uint32 cptr = gpr(8);\n"
        "				uint32 mptr = gpr(9);\n"
        "				if (cptr && g3_ea_data(cptr + 3u))\n"
        "					vm_write_memory_4(cptr,\n"
        "							  conn ? conn : 1u);\n"
        "				if (mptr && g3_ea_data(mptr + 3u))\n"
        "					vm_write_memory_4(mptr, mainp);\n"
        "				gpr(3) = 0;\n"
        "				if (g3_ea_data(gpr(1) + 23u)) {\n"
        "					uint32 toc =\n"
        "						vm_read_memory_4(gpr(1) + 20u);\n"
        "					if (toc == 0x10115000u)\n"
        "						gpr(2) = toc;\n"
        "				}\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngdf2;\n"
        "					if (ngdf2 < 8) {\n"
        "						char buf[96];\n"
        "						ngdf2++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdf2 conn=%08x\",\n"
        "							 (unsigned)(conn ? conn : 1u));\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() = lr();\n"
        "				continue;\n"
        "			}\n"
        "			if (g3_did_pef_enter &&\n"
        "			    pc() == RAMBase + 0x116000u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdf2")
    return _insert_after_enter(text, _log_enter(m, "ngdf20"), "cpu-pef-gdf2")


def patch_cpu_pef_gdf3(text: str) -> str:
    """KEEP 17185: GetDiskFragment mainAddr was stub blr (0x10116000).
    Caller lwz r12,mainAddr; lwz r0,0(r12); bctr so r0=0x4e800020.
    Plant TVector {stub, toc} at conn. Do not remill leftover:pef-gdf2."""
    m = _MARKERS["pef-gdf3"]
    if m in text:
        return text
    old = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
        "				uint32 conn = g3_pef_newptr(16u);\n"
        "				uint32 mainp = RAMBase + 0x116000u;\n"
        "				uint32 cptr = gpr(8);\n"
        "				uint32 mptr = gpr(9);\n"
        "				if (cptr && g3_ea_data(cptr + 3u))\n"
        "					vm_write_memory_4(cptr,\n"
        "							  conn ? conn : 1u);\n"
        "				if (mptr && g3_ea_data(mptr + 3u))\n"
        "					vm_write_memory_4(mptr, mainp);\n"
    )
    new = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
        "				uint32 conn = g3_pef_newptr(16u);\n"
        "				uint32 tv = conn ? conn : (RAMBase + 0x4c200u);\n"
        "				uint32 cptr = gpr(8);\n"
        "				uint32 mptr = gpr(9);\n"
        "				if (g3_ea_data(tv + 7u)) {\n"
        "					vm_write_memory_4(tv,\n"
        "							  RAMBase + 0x116000u);\n"
        "					vm_write_memory_4(tv + 4u, 0x10115000u);\n"
        "				}\n"
        "				if (cptr && g3_ea_data(cptr + 3u))\n"
        "					vm_write_memory_4(cptr, tv);\n"
        "				if (mptr && g3_ea_data(mptr + 3u))\n"
        "					vm_write_memory_4(mptr, tv);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdf3")
    # log conn line still says gdf2; add gdf3 stamp after enter
    oldlog = (
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdf2 conn=%08x\",\n"
        "							 (unsigned)(conn ? conn : 1u));\n"
    )
    newlog = (
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdf3 tv=%08x\",\n"
        "							 (unsigned)tv);\n"
    )
    text = _replace_once(text, oldlog, newlog, "cpu-pef-gdf3-log")
    return _insert_after_enter(text, _log_enter(m, "ngdf3"), "cpu-pef-gdf3")


def patch_cpu_pef_hsz64(text: str) -> str:
    """KEEP 17186: GetNewCWindow 129 + DrawDialog then GetHandleSize n=0
    NumToString n=-108 STR# 520. Fill nil handle with 64 bytes so the
    error path does not fire. Do not remill leftover:pef-gdf3."""
    m = _MARKERS["pef-hsz64"]
    if m in text:
        return text
    old = (
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
    )
    new = (
        "	} else if (idx == 28u) {\n"
        "		uint32 p = 0;\n"
        "		if (a3 && g3_ea_data(a3 + 3u))\n"
        "			p = vm_read_memory_4(a3);\n"
        "		if (!p) {\n"
        "			p = g3_pef_newptr(64u);\n"
        "			if (p && a3 && g3_ea_data(a3 + 3u))\n"
        "				vm_write_memory_4(a3, p);\n"
        "		}\n"
        "		r3 = 64u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nhz;\n"
        "			if (nhz < 8) {\n"
        "				char buf[96];\n"
        "				nhz++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetHandleSize n=%u a3=%08x\",\n"
        "					 (unsigned)r3, (unsigned)a3);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hsz64")
    return _insert_after_enter(text, _log_enter(m, "nhsz64"), "cpu-pef-hsz64")


def patch_cpu_pef_nersp(text: str) -> str:
    """KEEP 17187: Get1 ners 500 is forced r3=0 (leftover:pef-ners0), so
    error lookup GetHandleSize(nil) then DSI 1010a5f8. APPL ners 500 is
    186 bytes. Plant it. Do not remill leftover:pef-ners0 / pef-hsz64."""
    m = _MARKERS["pef-nersp"]
    if m in text:
        return text
    old = (
        "		} else if (a3 == 0x6e657273u && rid == 500)\n"
        "			r3 = 0;\n"
        "		else if (a3 == 0x54455854u && rid == 3502) {\n"
    )
    new = (
        "		} else if (a3 == 0x6e657273u && rid == 500) {\n"
        "			g3_res_src_off = g3_rf_off;\n"
        "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "				r3 = g3_res_plant(doff, ln);\n"
        "		} else if (a3 == 0x54455854u && rid == 3502) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-nersp")
    return _insert_after_enter(text, _log_enter(m, "nnersp"), "cpu-pef-nersp")


def patch_cpu_pef_gdfnm(text: str) -> str:
    """KEEP 17188: GetDiskFragment glue never logs FSSpec. r3+6 is the
    Pascal name (Welcome / IncompatHW / document). Log it. Keep stub
    TVector. Do not remill leftover:pef-gdf3 / pef-getdf. Do not skip-68k."""
    m = _MARKERS["pef-gdfnm"]
    if m in text:
        return text
    old = (
        "						char buf[96];\n"
        "						ngdf2++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdf3 tv=%08x\",\n"
        "							 (unsigned)tv);\n"
    )
    new = (
        "						char buf[128];\n"
        "						char nm[32];\n"
        "						uint32 spec = gpr(3);\n"
        "						uint32 nmp = spec ? spec + 6u : 0;\n"
        "						unsigned nlen = 0, ni;\n"
        "						ngdf2++;\n"
        "						nm[0] = 0;\n"
        "						if (nmp && g3_ea_data(nmp)) {\n"
        "							nlen = vm_read_memory_1(nmp);\n"
        "							if (nlen > 28u)\n"
        "								nlen = 28u;\n"
        "							for (ni = 0; ni < nlen; ni++) {\n"
        "								if (!g3_ea_data(nmp + 1u + ni))\n"
        "									break;\n"
        "								nm[ni] = (char)vm_read_memory_1(\n"
        "									nmp + 1u + ni);\n"
        "							}\n"
        "							nm[ni] = 0;\n"
        "							nlen = ni;\n"
        "						}\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdfnm tv=%08x nlen=%u nm=%s\",\n"
        "							 (unsigned)tv, nlen, nm);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdfnm")
    return _insert_after_enter(text, _log_enter(m, "ngdfnm"), "cpu-pef-gdfnm")


def patch_cpu_pef_gdfn6(text: str) -> str:
    """KEEP 17189 gdfnm nlen=0: log ran after gpr(3)=0 so FSSpec was
    clobbered. Capture r3/r6 before the store. r6 is GetDiskFragment
    fragName. Do not remill leftover:pef-gdfnm. Do not skip-68k."""
    m = _MARKERS["pef-gdfn6"]
    if m in text:
        return text
    old = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
        "				uint32 conn = g3_pef_newptr(16u);\n"
        "				uint32 tv = conn ? conn : (RAMBase + 0x4c200u);\n"
        "				uint32 cptr = gpr(8);\n"
        "				uint32 mptr = gpr(9);\n"
    )
    new = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
        "				uint32 spec = gpr(3);\n"
        "				uint32 frag = gpr(6);\n"
        "				uint32 fl = gpr(7);\n"
        "				uint32 conn = g3_pef_newptr(16u);\n"
        "				uint32 tv = conn ? conn : (RAMBase + 0x4c200u);\n"
        "				uint32 cptr = gpr(8);\n"
        "				uint32 mptr = gpr(9);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdfn6-cap")
    oldlog = (
        "						char buf[128];\n"
        "						char nm[32];\n"
        "						uint32 spec = gpr(3);\n"
        "						uint32 nmp = spec ? spec + 6u : 0;\n"
        "						unsigned nlen = 0, ni;\n"
        "						ngdf2++;\n"
        "						nm[0] = 0;\n"
        "						if (nmp && g3_ea_data(nmp)) {\n"
        "							nlen = vm_read_memory_1(nmp);\n"
        "							if (nlen > 28u)\n"
        "								nlen = 28u;\n"
        "							for (ni = 0; ni < nlen; ni++) {\n"
        "								if (!g3_ea_data(nmp + 1u + ni))\n"
        "									break;\n"
        "								nm[ni] = (char)vm_read_memory_1(\n"
        "									nmp + 1u + ni);\n"
        "							}\n"
        "							nm[ni] = 0;\n"
        "							nlen = ni;\n"
        "						}\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdfnm tv=%08x nlen=%u nm=%s\",\n"
        "							 (unsigned)tv, nlen, nm);\n"
    )
    newlog = (
        "						char buf[160];\n"
        "						char nm[32], fn[32];\n"
        "						uint32 nmp, fmp;\n"
        "						unsigned nlen = 0, flen = 0, ni;\n"
        "						ngdf2++;\n"
        "						nm[0] = 0;\n"
        "						fn[0] = 0;\n"
        "						nmp = spec ? spec + 6u : 0;\n"
        "						if (nmp && g3_ea_data(nmp)) {\n"
        "							nlen = vm_read_memory_1(nmp);\n"
        "							if (nlen > 28u)\n"
        "								nlen = 28u;\n"
        "							for (ni = 0; ni < nlen; ni++) {\n"
        "								if (!g3_ea_data(nmp + 1u + ni))\n"
        "									break;\n"
        "								nm[ni] = (char)vm_read_memory_1(\n"
        "									nmp + 1u + ni);\n"
        "							}\n"
        "							nm[ni] = 0;\n"
        "							nlen = ni;\n"
        "						}\n"
        "						fmp = frag;\n"
        "						if (fmp && g3_ea_data(fmp)) {\n"
        "							flen = vm_read_memory_1(fmp);\n"
        "							if (flen > 28u)\n"
        "								flen = 28u;\n"
        "							for (ni = 0; ni < flen; ni++) {\n"
        "								if (!g3_ea_data(fmp + 1u + ni))\n"
        "									break;\n"
        "								fn[ni] = (char)vm_read_memory_1(\n"
        "									fmp + 1u + ni);\n"
        "							}\n"
        "							fn[ni] = 0;\n"
        "							flen = ni;\n"
        "						}\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdfn6 r3=%08x r6=%08x fl=%x nlen=%u nm=%s fn=%s\",\n"
        "							 (unsigned)spec, (unsigned)frag,\n"
        "							 (unsigned)fl, nlen, nm, fn);\n"
    )
    text = _replace_once(text, oldlog, newlog, "cpu-pef-gdfn6-log")
    return _insert_after_enter(text, _log_enter(m, "ngdfn6"), "cpu-pef-gdfn6")


def patch_cpu_pef_welpef(text: str) -> str:
    """KEEP 17190: GDF FSSpec is the document Install Mac OS 9.2.1
    (data/chsk), not Welcome.plug. Load Welcome PEF from toast, reloc
    imports to mill idx, main TVector at data+0x50. Do not remill
    leftover:pef-gdfn6 / gdf3. Do not skip-68k."""
    m = _MARKERS["pef-welpef"]
    if m in text:
        return text
    old_fn = (
        "	g3_pef_heap = p + n;\n"
        "	return p;\n"
        "}\n"
        "/* GetDCtlEntry must return a Handle to a DCE whose\n"
    )
    new_fn = (
        "	g3_pef_heap = p + n;\n"
        "	return p;\n"
        "}\n"
        "static uint32 g3_did_welpef;\n"
        "static uint32 g3_pef_load_welcome(void)\n"
        "{\n"
        "	const uint32 plant = RAMBase + 0x240000u;\n"
        "	const uint32 code = plant + 512u;\n"
        "	const uint32 data = plant + 0x2000u;\n"
        "	const uint32 stub = RAMBase + 0x116000u;\n"
        "	const uint32 toff = 448873984u;\n"
        "	const unsigned plen = 4585u;\n"
        "	const uint32 map[10] = {\n"
        "		31u, 38u, 43u, 68u, 153u,\n"
        "		158u, 162u, 166u, 223u, 277u\n"
        "	};\n"
        "	uint8 buf[4096];\n"
        "	unsigned got = 0;\n"
        "	uint32 ip, dend, d, op, c5, cnt, c2, custom, rep, i, n;\n"
        "	uint32 relocA, sectC, sectD, c, top7, imp, tv;\n"
        "	if (g3_did_welpef)\n"
        "		return g3_did_welpef;\n"
        "	if (!g3_toast_open() || !g3_ea_data(plant + plen - 1u) ||\n"
        "	    !g3_ea_data(data + 87u))\n"
        "		return 0;\n"
        "	fseek(g3_toast, (long)toff, SEEK_SET);\n"
        "	while (got < plen) {\n"
        "		unsigned nb = plen - got;\n"
        "		size_t nr;\n"
        "		if (nb > sizeof(buf))\n"
        "			nb = sizeof(buf);\n"
        "		nr = fread(buf, 1, nb, g3_toast);\n"
        "		if (nr != nb)\n"
        "			return 0;\n"
        "		for (i = 0; i < nb; i++)\n"
        "			vm_write_memory_1(plant + got + i, buf[i]);\n"
        "		got += nb;\n"
        "	}\n"
        "	for (i = 0; i < 88u; i += 4u)\n"
        "		vm_write_memory_4(data + i, 0);\n"
        "	ip = plant + 4560u;\n"
        "	dend = ip + 25u;\n"
        "	d = 0;\n"
        "	while (ip < dend && d < 88u) {\n"
        "		uint32 a;\n"
        "		if (!g3_ea_data(ip))\n"
        "			break;\n"
        "		op = vm_read_memory_1(ip++);\n"
        "		c5 = op & 31u;\n"
        "		op >>= 5;\n"
        "		if (c5)\n"
        "			cnt = c5;\n"
        "		else\n"
        "			cnt = g3_pef_varint(&ip, dend);\n"
        "		if (op == 0)\n"
        "			d += cnt;\n"
        "		else if (op == 1) {\n"
        "			for (i = 0; i < cnt && d < 88u && ip < dend; i++, d++)\n"
        "				vm_write_memory_1(data + d,\n"
        "						  vm_read_memory_1(ip++));\n"
        "		} else if (op == 2) {\n"
        "			c2 = g3_pef_varint(&ip, dend);\n"
        "			a = ip;\n"
        "			ip += cnt;\n"
        "			for (rep = 0; rep <= c2 && d < 88u; rep++)\n"
        "				for (i = 0; i < cnt && d < 88u; i++, d++)\n"
        "					vm_write_memory_1(data + d,\n"
        "							  vm_read_memory_1(a + i));\n"
        "		} else if (op == 4) {\n"
        "			custom = g3_pef_varint(&ip, dend);\n"
        "			rep = g3_pef_varint(&ip, dend);\n"
        "			for (n = 0; n < rep && d < 88u; n++) {\n"
        "				d += cnt;\n"
        "				for (i = 0; i < custom && d < 88u && ip < dend; i++, d++)\n"
        "					vm_write_memory_1(data + d,\n"
        "							  vm_read_memory_1(ip++));\n"
        "			}\n"
        "			d += cnt;\n"
        "		} else\n"
        "			break;\n"
        "	}\n"
        "	relocA = 0;\n"
        "	sectC = code;\n"
        "	sectD = data;\n"
        "	imp = 0;\n"
        "	ip = plant + 128u + 132u;\n"
        "	dend = ip + 8u;\n"
        "	while (ip + 1u < dend) {\n"
        "		if (!g3_ea_data(ip + 1u))\n"
        "			break;\n"
        "		c = vm_read_memory_2(ip);\n"
        "		ip += 2u;\n"
        "		top7 = c >> 9;\n"
        "		if (top7 >= 0x20u && top7 <= 0x25u) {\n"
        "			n = (c & 0x1ffu) + 1u;\n"
        "			if (top7 == 0x23u && (relocA & 3u))\n"
        "				relocA = (relocA + 3u) & ~3u;\n"
        "			for (i = 0; i < n; i++) {\n"
        "				if (top7 == 0x20u) {\n"
        "					g3_pef_add32(data + relocA, sectC);\n"
        "					relocA += 4u;\n"
        "				} else if (top7 == 0x21u) {\n"
        "					g3_pef_add32(data + relocA, sectD);\n"
        "					relocA += 4u;\n"
        "				} else if (top7 == 0x23u) {\n"
        "					g3_pef_add32(data + relocA, sectC);\n"
        "					g3_pef_add32(data + relocA + 4u, sectD);\n"
        "					relocA += 8u;\n"
        "				} else if (top7 == 0x25u) {\n"
        "					uint32 ix = (imp < 10u) ? map[imp] : 0;\n"
        "					vm_write_memory_4(data + relocA,\n"
        "							  stub + 4u + ix * 8u);\n"
        "					imp++;\n"
        "					relocA += 4u;\n"
        "				} else\n"
        "					relocA += 4u;\n"
        "			}\n"
        "		} else if (top7 >= 0x40u && top7 <= 0x47u)\n"
        "			relocA += c & 0xfffu;\n"
        "		else\n"
        "			break;\n"
        "	}\n"
        "	tv = data + 0x50u;\n"
        "	if (!g3_ea_data(tv + 7u) || !vm_read_memory_4(tv))\n"
        "		return 0;\n"
        "	g3_did_welpef = tv;\n"
        "	return tv;\n"
        "}\n"
        "/* GetDCtlEntry must return a Handle to a DCE whose\n"
    )
    text = _replace_once(text, old_fn, new_fn, "cpu-pef-welpef-fn")
    old_gdf = (
        "				if (g3_ea_data(tv + 7u)) {\n"
        "					vm_write_memory_4(tv,\n"
        "							  RAMBase + 0x116000u);\n"
        "					vm_write_memory_4(tv + 4u, 0x10115000u);\n"
        "				}\n"
        "				if (cptr && g3_ea_data(cptr + 3u))\n"
        "					vm_write_memory_4(cptr, tv);\n"
        "				if (mptr && g3_ea_data(mptr + 3u))\n"
        "					vm_write_memory_4(mptr, tv);\n"
    )
    new_gdf = (
        "				{\n"
        "					uint32 wel = g3_pef_load_welcome();\n"
        "					if (wel && g3_ea_data(wel + 7u)) {\n"
        "						tv = wel;\n"
        "					} else if (g3_ea_data(tv + 7u)) {\n"
        "						vm_write_memory_4(tv,\n"
        "								  RAMBase + 0x116000u);\n"
        "						vm_write_memory_4(tv + 4u,\n"
        "								  0x10115000u);\n"
        "					}\n"
        "				}\n"
        "				if (cptr && g3_ea_data(cptr + 3u))\n"
        "					vm_write_memory_4(cptr, tv);\n"
        "				if (mptr && g3_ea_data(mptr + 3u))\n"
        "					vm_write_memory_4(mptr, tv);\n"
    )
    text = _replace_once(text, old_gdf, new_gdf, "cpu-pef-welpef-gdf")
    oldlog = (
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdfn6 r3=%08x r6=%08x fl=%x nlen=%u nm=%s fn=%s\",\n"
        "							 (unsigned)spec, (unsigned)frag,\n"
        "							 (unsigned)fl, nlen, nm, fn);\n"
    )
    newlog = (
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF welpef tv=%08x nm=%s fn=%s\",\n"
        "							 (unsigned)tv, nm, fn);\n"
    )
    text = _replace_once(text, oldlog, newlog, "cpu-pef-welpef-log")
    return _insert_after_enter(text, _log_enter(m, "nwelpef"), "cpu-pef-welpef")


def patch_cpu_pef_cuparg(text: str) -> str:
    """KEEP 17191: Welcome PEF loaded (tv=10242050) then CallUniversalProc
    idx=68 no-op x8, then WIND 129 / -108. Log UPP r3 and procInfo r4.
    Do not remill leftover:pef-cup / pef-welpef. Do not skip-68k."""
    m = _MARKERS["pef-cuparg"]
    if m in text:
        return text
    old = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF CallUniversalProc idx=%u\",\n"
        "					 (unsigned)idx);\n"
    )
    new = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF cuparg upp=%08x inf=%08x a5=%08x\",\n"
        "					 (unsigned)a3, (unsigned)a4, (unsigned)a5);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cuparg")
    return _insert_after_enter(text, _log_enter(m, "ncuparg"), "cpu-pef-cuparg")


def patch_cpu_pef_cupld(text: str) -> str:
    """KEEP 17192: Welcome CUP upp=10115000 (Upgrader TOC) not a UPP.
    Log *upp and upp[4]. Do not remill leftover:pef-cuparg. Do not skip-68k."""
    m = _MARKERS["pef-cupld"]
    if m in text:
        return text
    old = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF cuparg upp=%08x inf=%08x a5=%08x\",\n"
        "					 (unsigned)a3, (unsigned)a4, (unsigned)a5);\n"
    )
    new = (
        "				uint32 w0 = 0, w1 = 0;\n"
        "				if (a3 && g3_ea_data(a3 + 7u)) {\n"
        "					w0 = vm_read_memory_4(a3);\n"
        "					w1 = vm_read_memory_4(a3 + 4u);\n"
        "				}\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupld upp=%08x w=%08x:%08x inf=%x\",\n"
        "					 (unsigned)a3, (unsigned)w0, (unsigned)w1,\n"
        "					 (unsigned)a4);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupld")
    return _insert_after_enter(text, _log_enter(m, "ncupld"), "cpu-pef-cupld")


def patch_cpu_pef_welr2(text: str) -> str:
    """KEEP 17193: Welcome CUP UPP is Upgrader TOC 10115000
    (*toc=idx0 TVector). Plugin r2 never switched. Force r2=plugin
    TOC 10242000 in Welcome code. Do not remill leftover:pef-cupld."""
    m = _MARKERS["pef-welr2"]
    if m in text:
        return text
    old = (
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
    )
    new = (
        "			if (g3_did_welpef &&\n"
        "			    pc() >= 0x10240200u && pc() < 0x10241200u &&\n"
        "			    gpr(2) != 0x10242000u) {\n"
        "				gpr(2) = 0x10242000u;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned nwelr2;\n"
        "					if (nwelr2 < 8) {\n"
        "						nwelr2++;\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF welr2\");\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			}\n"
        "			if (g3_did_pef_enter && pc() == 0x10114d64u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welr2")
    return _insert_after_enter(text, _log_enter(m, "nwelr20"), "cpu-pef-welr2")


def patch_cpu_pef_welarg(text: str) -> str:
    """KEEP 17194: welr2 fires but CUP UPP stays Upgrader TOC. Log
    plugin main r3-r6 at first Welcome PC. Do not remill leftover:pef-welr2."""
    m = _MARKERS["pef-welarg"]
    if m in text:
        return text
    old = (
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF welr2\");\n"
    )
    new = (
        "						char buf[96];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF welarg r3=%08x r4=%08x r5=%08x r6=%08x\",\n"
        "							 (unsigned)gpr(3), (unsigned)gpr(4),\n"
        "							 (unsigned)gpr(5), (unsigned)gpr(6));\n"
        "						nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welarg")
    return _insert_after_enter(text, _log_enter(m, "nwelarg"), "cpu-pef-welarg")


def patch_cpu_pef_cupgis(text: str) -> str:
    """KEEP 17195: Welcome CUP a5=0x0dac (3500). Dispatch GetIndString
    id=3500 ix=1 dest=a6. Do not remill leftover:pef-cup / cuparg.
    Do not skip-68k."""
    m = _MARKERS["pef-cupgis"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 68u || idx == 64u) {\n"
        "		/* CallUniversalProc / CallOSTrapUniversalProc no-op. */\n"
        "		r3 = 0;\n"
    )
    new = (
        "	} else if (idx == 68u || idx == 64u) {\n"
        "		r3 = 0;\n"
        "		if (a5 == 0xdacu)\n"
        "			r3 = g3_pef_host(162u, a6, 3500u, 1u, 0, 0);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupgis")
    return _insert_after_enter(text, _log_enter(m, "ncupgis"), "cpu-pef-cupgis")


def patch_cpu_pef_cupall(text: str) -> str:
    """KEEP 17196: GetIndString id=3500 from CUP a5=0x0dac. Other CUP
    procInfo still no-op (fee5/3fa95/2f5/ae5). Dispatch FrontWindow,
    GetPicture, GetMainDevice, GetResource. Do not remill pef-cupgis."""
    m = _MARKERS["pef-cupall"]
    if m in text:
        return text
    old = (
        "		if (a5 == 0xdacu)\n"
        "			r3 = g3_pef_host(162u, a6, 3500u, 1u, 0, 0);\n"
    )
    new = (
        "		if (a5 == 0xdacu)\n"
        "			r3 = g3_pef_host(162u, a6, 3500u, 1u, 0, 0);\n"
        "		else if (a4 == 0xae5u)\n"
        "			r3 = g3_pef_host(166u, a5, a6, a7, 0, 0);\n"
        "		else if (a4 == 0xfee5u)\n"
        "			r3 = g3_pef_host(31u, a5, a6, a7, 0, 0);\n"
        "		else if (a4 == 0x3fa95u)\n"
        "			r3 = g3_pef_host(43u, a5, a6, a7, 0, 0);\n"
        "		else if (a4 == 0x2f5u)\n"
        "			r3 = g3_pef_host(153u, a5, a6, a7, 0, 0);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupall")
    return _insert_after_enter(text, _log_enter(m, "ncupall"), "cpu-pef-cupall")


def patch_cpu_pef_fw128(text: str) -> str:
    """KEEP 17197: Welcome FrontWindow returned splash DLOG 510
    (1005130c). Return WIND 128 (1004e000) after welpef. Do not remill
    leftover:pef-cupall. Do not skip-68k."""
    m = _MARKERS["pef-fw128"]
    if m in text:
        return text
    old = (
        "	if (idx == 166u) {\n"
        "		r3 = g3_splash_dlg;\n"
    )
    new = (
        "	if (idx == 166u) {\n"
        "		r3 = g3_did_welpef ? (RAMBase + 0x4e000u) : g3_splash_dlg;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fw128")
    return _insert_after_enter(text, _log_enter(m, "nfw128"), "cpu-pef-fw128")


def patch_cpu_pef_cuptxt(text: str) -> str:
    """KEEP 17198: CUP inf=0x2f5 a6=0x0dae (3502) was GetResource with
    a3=pointer. Dispatch GetResource TEXT 3502. Do not remill pef-cupall."""
    m = _MARKERS["pef-cuptxt"]
    if m in text:
        return text
    old = (
        "		else if (a4 == 0x2f5u)\n"
        "			r3 = g3_pef_host(153u, a5, a6, a7, 0, 0);\n"
    )
    new = (
        "		else if (a4 == 0x2f5u)\n"
        "			r3 = g3_pef_host(153u, 0x54455854u, 3502u, 0, 0, 0);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cuptxt")
    return _insert_after_enter(text, _log_enter(m, "ncuptxt"), "cpu-pef-cuptxt")


def patch_cpu_pef_welrf(text: str) -> str:
    """KEEP 17199: Welcome.plug rsrc has DITL/PICT 3500. Search that
    fork after APPL/DOC. Toast rf 448914944 map 11410. Do not remill
    leftover:pef-cuptxt. Do not skip-68k."""
    m = _MARKERS["pef-welrf"]
    if m in text:
        return text
    old = (
        "static uint32 g3_doc_rf_off = 73895424u;\n"
        "static uint32 g3_doc_rf_map = 276953u;\n"
        "static unsigned g3_doc_rf_mapn = 2600u;\n"
    )
    new = (
        "static uint32 g3_doc_rf_off = 73895424u;\n"
        "static uint32 g3_doc_rf_map = 276953u;\n"
        "static unsigned g3_doc_rf_mapn = 2600u;\n"
        "static uint32 g3_wel_rf_off = 448914944u;\n"
        "static uint32 g3_wel_rf_map = 11410u;\n"
        "static unsigned g3_wel_rf_mapn = 214u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welrf-off")
    old2 = (
        "	uint32 forks[2], maps[2];\n"
        "	unsigned mapns[2], f;\n"
        "	if (!g3_toast_open())\n"
        "		return 0;\n"
        "	forks[0] = g3_rf_off; maps[0] = g3_rf_map; mapns[0] = g3_rf_mapn;\n"
        "	forks[1] = g3_doc_rf_off; maps[1] = g3_doc_rf_map; mapns[1] = g3_doc_rf_mapn;\n"
        "	for (f = 0; f < 2u; f++) {\n"
    )
    new2 = (
        "	uint32 forks[3], maps[3];\n"
        "	unsigned mapns[3], f;\n"
        "	if (!g3_toast_open())\n"
        "		return 0;\n"
        "	forks[0] = g3_rf_off; maps[0] = g3_rf_map; mapns[0] = g3_rf_mapn;\n"
        "	forks[1] = g3_doc_rf_off; maps[1] = g3_doc_rf_map; mapns[1] = g3_doc_rf_mapn;\n"
        "	forks[2] = g3_wel_rf_off; maps[2] = g3_wel_rf_map; mapns[2] = g3_wel_rf_mapn;\n"
        "	for (f = 0; f < 3u; f++) {\n"
    )
    n = text.count(old2)
    if n < 1:
        raise ValueError("mill patch missing: cpu-pef-welrf-forks count=%s" % n)
    text = text.replace(old2, new2)
    old3 = (
        "	forks[1] = g3_doc_rf_off; maps[1] = g3_doc_rf_map; mapns[1] = g3_doc_rf_mapn;\n"
        "	for (f = 0; f < 2u; f++) {\n"
        "		if (mapns[f] > sizeof(map))\n"
    )
    new3 = (
        "	forks[1] = g3_doc_rf_off; maps[1] = g3_doc_rf_map; mapns[1] = g3_doc_rf_mapn;\n"
        "	forks[2] = g3_wel_rf_off; maps[2] = g3_wel_rf_map; mapns[2] = g3_wel_rf_mapn;\n"
        "	for (f = 0; f < 3u; f++) {\n"
        "		if (mapns[f] > sizeof(map))\n"
    )
    n = text.count(old3)
    if n:
        text = text.replace(old3, new3)
    old4 = (
        "	uint32 forks[2], maps[2];\n"
        "	unsigned mapns[2], f;\n"
    )
    new4 = (
        "	uint32 forks[3], maps[3];\n"
        "	unsigned mapns[3], f;\n"
    )
    text = text.replace(old4, new4)
    return _insert_after_enter(text, _log_enter(m, "nwelrf"), "cpu-pef-welrf")


def patch_cpu_pef_pict3500(text: str) -> str:
    """KEEP 17200: Welcome CUP inf=0xfee5 is DrawString of STR# 3500
    "Welcome" (r5=0x0757656c). Blit Welcome.plug PICT 3500 506x49 into
    FB. Do not remill leftover:pef-welrf / cupall. Do not skip-68k."""
    m = _MARKERS["pef-pict3500"]
    if m in text:
        return text
    old = "static int g3_did_pict1000;\n"
    new = (
        "static int g3_did_pict1000;\n"
        "static int g3_did_pict3500;\n"
        "static int16 g3_pict_rid = 1000;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-rid")
    old = (
        "	if (g3_did_pict1000)\n"
        "		return;\n"
        "	if (!g3_res_lookup(0x50494354u, 1000, &doff, &ln) ||\n"
    )
    new = (
        "	int16 rid = g3_pict_rid;\n"
        "	int *did = (rid == 3500) ? &g3_did_pict3500 : &g3_did_pict1000;\n"
        "	if (*did)\n"
        "		return;\n"
        "	if (!g3_res_lookup(0x50494354u, rid, &doff, &ln) ||\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-lookup")
    old = (
        "	fseek(g3_toast, (long)(g3_rf_off + 256u + doff + 4u), SEEK_SET);\n"
    )
    new = (
        "	fseek(g3_toast, (long)(g3_res_src_off + 256u + doff + 4u), SEEK_SET);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-src")
    old = (
        "	if (h == 0 || h > 480u)\n"
        "		h = 44u;\n"
    )
    new = (
        "	if (h == 0 || h > 480u)\n"
        "		h = (rid == 3500) ? 49u : 44u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-h")
    old = (
        "	g3_did_pict1000 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned npic;\n"
        "		if (npic < 8) {\n"
        "			npic++;\n"
        "			nw_boot_log(\"G3: 68k DrawPicture A8F6 PICT 1000\");\n"
    )
    new = (
        "	*did = 1;\n"
        "	g3_pict_rid = 1000;\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned npic;\n"
        "		if (npic < 8) {\n"
        "			char buf[96];\n"
        "			npic++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k DrawPicture A8F6 PICT %d\",\n"
        "				 (int)rid);\n"
        "			nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-log")
    old = (
        "		else if (a4 == 0xfee5u)\n"
        "			r3 = g3_pef_host(31u, a5, a6, a7, 0, 0);\n"
    )
    new = (
        "		else if (a4 == 0xfee5u) {\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			r3 = 0;\n"
        "		}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-pict3500-cup")
    return _insert_after_enter(text, _log_enter(m, "npict3500"), "cpu-pef-pict3500")


def patch_cpu_pef_drawwel(text: str) -> str:
    """KEEP 17201: PICT 3500 blit is a white 506x49 bar at y=80 (guest
    FB). STR# 3500 ix=1 is Welcome. Plot it on the bar. Do not remill
    leftover:pef-pict3500. Do not skip-68k."""
    m = _MARKERS["pef-drawwel"]
    if m in text:
        return text
    old = (
        "	vm_write_memory_1(a + 3u, b);\n"
        "}\n"
        "static void g3_pict1000_blit(void)\n"
    )
    new = (
        "	vm_write_memory_1(a + 3u, b);\n"
        "}\n"
        "static void g3_draw_welcome_str(void)\n"
        "{\n"
        "	static const uint8 col[7][5] = {\n"
        "		{ 0x7f, 0x08, 0x08, 0x08, 0x7f },\n"
        "		{ 0x38, 0x54, 0x54, 0x54, 0x18 },\n"
        "		{ 0x00, 0x41, 0x7f, 0x40, 0x00 },\n"
        "		{ 0x38, 0x44, 0x44, 0x44, 0x28 },\n"
        "		{ 0x38, 0x44, 0x44, 0x44, 0x38 },\n"
        "		{ 0x7c, 0x04, 0x18, 0x04, 0x78 },\n"
        "		{ 0x38, 0x54, 0x54, 0x54, 0x18 }\n"
        "	};\n"
        "	uint32 fb = g3_qd_fb();\n"
        "	unsigned gi, ci, b;\n"
        "	int x, y;\n"
        "	for (gi = 0; gi < 7u; gi++) {\n"
        "		for (ci = 0; ci < 5u; ci++) {\n"
        "			uint8 bits = col[gi][ci];\n"
        "			for (b = 0; b < 7u; b++) {\n"
        "				if (bits & (1u << b)) {\n"
        "					x = 16 + (int)gi * 6 + (int)ci;\n"
        "					y = 92 + (int)b;\n"
        "					g3_fb_xrgb(fb, x, y, 0, 0, 0);\n"
        "					g3_fb_xrgb(fb, x, y + 1, 0, 0, 0);\n"
        "				}\n"
        "			}\n"
        "		}\n"
        "	}\n"
        "#if NW_BOOT_LOG\n"
        "	nw_boot_log(\n"
        "		\"G3: 68k Launch A9F2 CFM Upgrader PEF drawwel\");\n"
        "#endif\n"
        "}\n"
        "static void g3_pict1000_blit(void)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-drawwel-fn")
    old = (
        "	*did = 1;\n"
        "	g3_pict_rid = 1000;\n"
    )
    new = (
        "	*did = 1;\n"
        "	if (rid == 3500)\n"
        "		g3_draw_welcome_str();\n"
        "	g3_pict_rid = 1000;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-drawwel-call")
    return _insert_after_enter(text, _log_enter(m, "ndrawwel"), "cpu-pef-drawwel")


def patch_cpu_pef_xrgbff(text: str) -> str:
    """KEEP 17201: PICT 3500 is in the_buffer (guest dump) but SDL
    Amask is byte0. g3_fb_xrgb wrote pad=0 so the window stays black.
    Write 0xFF and dirty the bar. Not a host fill of a solid rect.
    Do not remill leftover:pef-pict3500 / drawwel. Do not skip-68k."""
    m = _MARKERS["pef-xrgbff"]
    if m in text:
        return text
    old = (
        "	vm_write_memory_1(a, 0);\n"
        "	vm_write_memory_1(a + 1u, r);\n"
        "	vm_write_memory_1(a + 2u, g);\n"
        "	vm_write_memory_1(a + 3u, b);\n"
    )
    new = (
        "	vm_write_memory_1(a, 0xffu);\n"
        "	vm_write_memory_1(a + 1u, r);\n"
        "	vm_write_memory_1(a + 2u, g);\n"
        "	vm_write_memory_1(a + 3u, b);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-xrgbff-pad")
    old = (
        "	if (rid == 3500)\n"
        "		g3_draw_welcome_str();\n"
        "	g3_pict_rid = 1000;\n"
    )
    new = (
        "	if (rid == 3500) {\n"
        "		g3_draw_welcome_str();\n"
        "		video_set_dirty_area(0, 80, 640, 49);\n"
        "	}\n"
        "	g3_pict_rid = 1000;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-xrgbff-dirty")
    old = (
        "#ifdef SHEEPSHAVER\n"
        "#include \"nw_boot_contract.h\"\n"
        "extern uint32 ROMBase, RAMBase, RAMSize;\n"
        "static uint32 g3_rom0(uint32 a)\n"
    )
    new = (
        "#ifdef SHEEPSHAVER\n"
        "#include \"nw_boot_contract.h\"\n"
        "extern uint32 ROMBase, RAMBase, RAMSize;\n"
        "extern void video_set_dirty_area(int x, int y, int w, int h);\n"
        "static uint32 g3_rom0(uint32 a)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-xrgbff-decl")
    return _insert_after_enter(text, _log_enter(m, "nxrgbff"), "cpu-pef-xrgbff")


def patch_cpu_pef_fbpres(text: str) -> str:
    """KEEP 17203: NQD dirty 0,80 640x49 after PICT 3500 but hang-cap
    window PNG still black (the_buffer==copy until later present).
    VideoPresent on the CPU thread after the dirty. Do not remill
    leftover:pef-xrgbff. Do not skip-68k. Not a host fill of a rect."""
    m = _MARKERS["pef-fbpres"]
    if m in text:
        return text
    old = (
        "extern void video_set_dirty_area(int x, int y, int w, int h);\n"
    )
    new = (
        "extern void video_set_dirty_area(int x, int y, int w, int h);\n"
        "extern void VideoPresent(void);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fbpres-decl")
    old = (
        "		g3_draw_welcome_str();\n"
        "		video_set_dirty_area(0, 80, 640, 49);\n"
    )
    new = (
        "		g3_draw_welcome_str();\n"
        "		video_set_dirty_area(0, 80, 640, 49);\n"
        "		VideoPresent();\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fbpres-call")
    return _insert_after_enter(text, _log_enter(m, "nfbpres"), "cpu-pef-fbpres")


def patch_cpu_pef_dd3500(text: str) -> str:
    """KEEP 17204: Welcome PICT 3500 banner is on the SDL window.
    DITL 3500 is Continue/Go Back + body 506x298 (WIND 128).
    GetNewDialog 3500 -> WIND 128; DrawDialog fills DITL body and
    presents 298px. Do not remill leftover:pef-fbpres. Do not skip-68k."""
    m = _MARKERS["pef-dd3500"]
    if m in text:
        return text
    old = (
        "		else if ((a3 & 0xffffu) == 519u) {\n"
        "			/* 519 success skipped Splash 510. Fail it. */\n"
        "			r3 = 0;\n"
    )
    new = (
        "		else if ((a3 & 0xffffu) == 3500u) {\n"
        "			r3 = g3_plant_wind(128u);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nd3500;\n"
        "				if (nd3500 < 8) {\n"
        "					char buf[96];\n"
        "					nd3500++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id=3500 w=%08x\",\n"
        "						 (unsigned)r3);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if ((a3 & 0xffffu) == 519u) {\n"
        "			/* 519 success skipped Splash 510. Fail it. */\n"
        "			r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-dd3500-gnd")
    old = (
        "	} else if (idx == 155u || idx == 170u) {\n"
        "		if (idx == 170u && g3_ea_data(a3))\n"
        "			vm_write_memory_4(0x2aau, a3);\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
    )
    new = (
        "	} else if (idx == 155u || idx == 170u) {\n"
        "		if (idx == 170u && g3_ea_data(a3))\n"
        "			vm_write_memory_4(0x2aau, a3);\n"
        "		if (g3_did_welpef) {\n"
        "			uint32 fb = g3_qd_fb();\n"
        "			int x, y;\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			for (y = 129; y < 378; y++)\n"
        "				for (x = 0; x < 506; x++)\n"
        "					g3_fb_xrgb(fb, x, y,\n"
        "						    y >= 334 ? 0xccu : 0xeeu,\n"
        "						    y >= 334 ? 0xccu : 0xeeu,\n"
        "						    y >= 334 ? 0xccu : 0xeeu);\n"
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			VideoPresent();\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ndd35;\n"
        "				if (ndd35 < 8) {\n"
        "					ndd35++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF DrawDialog ditl3500\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else\n"
        "			g3_pict1000_blit();\n"
        "		r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-dd3500-draw")
    return _insert_after_enter(text, _log_enter(m, "ndd3500"), "cpu-pef-dd3500")


def patch_cpu_pef_noddfill(text: str) -> str:
    """KEEP 17205: DrawDialog ditl3500 126k vm_write+VideoPresent never
    logged; 0 A9F2 (17204 had 698). Drop the fill loops; keep
    GetNewDialog 3500. After welpef DrawDialog only dirties 298px.
    Do not remill leftover:pef-dd3500. Do not skip-68k."""
    m = _MARKERS["pef-noddfill"]
    if m in text:
        return text
    old = (
        "		if (g3_did_welpef) {\n"
        "			uint32 fb = g3_qd_fb();\n"
        "			int x, y;\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			for (y = 129; y < 378; y++)\n"
        "				for (x = 0; x < 506; x++)\n"
        "					g3_fb_xrgb(fb, x, y,\n"
        "						    y >= 334 ? 0xccu : 0xeeu,\n"
        "						    y >= 334 ? 0xccu : 0xeeu,\n"
        "						    y >= 334 ? 0xccu : 0xeeu);\n"
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			VideoPresent();\n"
    )
    new = (
        "		if (g3_did_welpef) {\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			VideoPresent();\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-noddfill")
    return _insert_after_enter(text, _log_enter(m, "nnoddfill"), "cpu-pef-noddfill")


def patch_cpu_pef_cupgmd(text: str) -> str:
    """KEEP 17206: Welcome banner on window; CUP inf=0x3fa95 then
    DSI SRR0=10240b10 DAR=00015016 (lwz r3,0x16(r3) gdPMap).
    leftover:pef-maindev is REVERT — do not remill it. Plant a
    GDevice+PixMap only after welpef for CUP idx 43.
    Do not remill leftover:pef-noddfill. Do not skip-68k."""
    m = _MARKERS["pef-cupgmd"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 31u) {\n"
        "		uint32 doff = 0, ln = 0;\n"
        "		if ((a3 & 0xffffu) == 1000u &&\n"
        "		    g3_res_lookup(0x50494354u, 1000, &doff, &ln))\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    new = (
        "	} else if (idx == 43u) {\n"
        "		static uint32 g3_wel_gdh;\n"
        "		if (g3_did_welpef && !g3_wel_gdh) {\n"
        "			uint32 fb = g3_qd_fb();\n"
        "			uint32 pm = g3_pef_newptr(64u);\n"
        "			uint32 pmh = g3_pef_newptr(8u);\n"
        "			uint32 gd = g3_pef_newptr(64u);\n"
        "			uint32 h = g3_pef_newptr(8u);\n"
        "			if (pm && pmh && gd && h) {\n"
        "				vm_write_memory_4(pm, fb);\n"
        "				vm_write_memory_2(pm + 4u, 0x8a00u);\n"
        "				vm_write_memory_2(pm + 10u, 480);\n"
        "				vm_write_memory_2(pm + 12u, 640);\n"
        "				vm_write_memory_2(pm + 32u, 32);\n"
        "				vm_write_memory_4(pmh, pm);\n"
        "				vm_write_memory_2(gd + 4u, 2);\n"
        "				vm_write_memory_4(gd + 22u, pmh);\n"
        "				vm_write_memory_2(gd + 38u, 480);\n"
        "				vm_write_memory_2(gd + 40u, 640);\n"
        "				vm_write_memory_4(h, gd);\n"
        "				g3_wel_gdh = h;\n"
        "			}\n"
        "		}\n"
        "		r3 = g3_did_welpef ? g3_wel_gdh : 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ncupgmd;\n"
        "			if (ncupgmd < 8) {\n"
        "				char buf[96];\n"
        "				ncupgmd++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupgmd h=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 31u) {\n"
        "		uint32 doff = 0, ln = 0;\n"
        "		if ((a3 & 0xffffu) == 1000u &&\n"
        "		    g3_res_lookup(0x50494354u, 1000, &doff, &ln))\n"
        "			r3 = g3_res_plant(doff, ln);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupgmd")
    return _insert_after_enter(text, _log_enter(m, "ncupgmd"), "cpu-pef-cupgmd")


def patch_cpu_pef_welbody(text: str) -> str:
    """KEEP 17207: DrawDialog ditl3500 x5 live; paint Continue/Go Back + TEXT 3502. No 126k fill. Do not remill leftover:pef-cupgmd. Do not skip-68k."""
    m = _MARKERS["pef-welbody"]
    if m in text:
        return text
    old = (
        "#endif\n"
        "}\n"
        "static void g3_pict1000_blit(void)\n"
        "{\n"
        "\tuint32 doff = 0, ln = 0, fb, i, opoff;\n"
    )
    new = (
        "#endif\n"
        "}\n"
        "static void g3_fb_glyph(uint32 fb, int x, int y, uint8 ch,\n"
        "		       uint8 cr, uint8 cg, uint8 cb)\n"
        "{\n"
        "	static const uint8 f[320] = {\n"
        "			0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x2fu,0x00u,0x00u,0x00u,0x03u,0x00u,0x03u,0x00u,0x0au,\n"
        "			0x1fu,0x0au,0x1fu,0x0au,0x24u,0x2au,0x7fu,0x2au,0x12u,0x43u,0x33u,0x08u,0x66u,0x61u,0x36u,0x49u,\n"
        "			0x56u,0x20u,0x50u,0x00u,0x00u,0x03u,0x00u,0x00u,0x00u,0x1cu,0x22u,0x41u,0x00u,0x00u,0x41u,0x22u,\n"
        "			0x1cu,0x00u,0x14u,0x08u,0x3eu,0x08u,0x14u,0x08u,0x08u,0x3eu,0x08u,0x08u,0x00u,0x40u,0x30u,0x00u,\n"
        "			0x00u,0x08u,0x08u,0x08u,0x08u,0x08u,0x00u,0x00u,0x20u,0x00u,0x00u,0x40u,0x30u,0x08u,0x06u,0x01u,\n"
        "			0x3eu,0x51u,0x49u,0x45u,0x3eu,0x00u,0x42u,0x7fu,0x40u,0x00u,0x42u,0x61u,0x51u,0x49u,0x46u,0x21u,\n"
        "			0x41u,0x45u,0x4bu,0x31u,0x18u,0x14u,0x12u,0x7fu,0x10u,0x27u,0x45u,0x45u,0x45u,0x39u,0x3cu,0x4au,\n"
        "			0x49u,0x49u,0x30u,0x01u,0x71u,0x09u,0x05u,0x03u,0x36u,0x49u,0x49u,0x49u,0x36u,0x06u,0x49u,0x49u,\n"
        "			0x29u,0x1eu,0x00u,0x00u,0x12u,0x00u,0x00u,0x00u,0x40u,0x32u,0x00u,0x00u,0x08u,0x14u,0x22u,0x41u,\n"
        "			0x00u,0x14u,0x14u,0x14u,0x14u,0x14u,0x00u,0x41u,0x22u,0x14u,0x08u,0x02u,0x01u,0x51u,0x09u,0x06u,\n"
        "			0x3eu,0x41u,0x5du,0x59u,0x0eu,0x7eu,0x09u,0x09u,0x09u,0x7eu,0x7fu,0x49u,0x49u,0x49u,0x36u,0x3eu,\n"
        "			0x41u,0x41u,0x41u,0x22u,0x7fu,0x41u,0x41u,0x22u,0x1cu,0x7fu,0x49u,0x49u,0x49u,0x41u,0x7fu,0x09u,\n"
        "			0x09u,0x09u,0x01u,0x3eu,0x41u,0x49u,0x49u,0x3au,0x7fu,0x08u,0x08u,0x08u,0x7fu,0x00u,0x41u,0x7fu,\n"
        "			0x41u,0x00u,0x20u,0x40u,0x41u,0x3fu,0x01u,0x7fu,0x08u,0x14u,0x22u,0x41u,0x7fu,0x40u,0x40u,0x40u,\n"
        "			0x40u,0x7fu,0x02u,0x0cu,0x02u,0x7fu,0x7fu,0x02u,0x04u,0x08u,0x7fu,0x3eu,0x41u,0x41u,0x41u,0x3eu,\n"
        "			0x7fu,0x09u,0x09u,0x09u,0x06u,0x3eu,0x41u,0x51u,0x21u,0x5eu,0x7fu,0x09u,0x19u,0x29u,0x46u,0x46u,\n"
        "			0x49u,0x49u,0x49u,0x31u,0x01u,0x01u,0x7fu,0x01u,0x01u,0x3fu,0x40u,0x40u,0x40u,0x3fu,0x1fu,0x20u,\n"
        "			0x40u,0x20u,0x1fu,0x3fu,0x40u,0x38u,0x40u,0x3fu,0x63u,0x14u,0x08u,0x14u,0x63u,0x03u,0x04u,0x78u,\n"
        "			0x04u,0x03u,0x61u,0x51u,0x49u,0x45u,0x43u,0x00u,0x7fu,0x41u,0x41u,0x00u,0x01u,0x06u,0x08u,0x30u,\n"
        "			0x40u,0x00u,0x41u,0x41u,0x7fu,0x00u,0x04u,0x02u,0x01u,0x02u,0x04u,0x40u,0x40u,0x40u,0x40u,0x40u\n"
        "		};\n"
        "	unsigned gi, ci, b;\n"
        "	if (ch >= (uint8)'a' && ch <= (uint8)'z')\n"
        "		ch = (uint8)(ch - 32u);\n"
        "	if (ch < 0x20u || ch > 0x5fu)\n"
        "		ch = 0x20u;\n"
        "	gi = (unsigned)(ch - 0x20u) * 5u;\n"
        "	for (ci = 0; ci < 5u; ci++) {\n"
        "		uint8 bits = f[gi + ci];\n"
        "		for (b = 0; b < 7u; b++)\n"
        "			if (bits & (1u << b))\n"
        "				g3_fb_xrgb(fb, x + (int)ci, y + (int)b,\n"
        "					    cr, cg, cb);\n"
        "	}\n"
        "}\n"
        "static void g3_fb_text(uint32 fb, int x, int y, const uint8 *s,\n"
        "		      unsigned n, int wrap, uint8 cr, uint8 cg, uint8 cb)\n"
        "{\n"
        "	unsigned i;\n"
        "	int cx = x, cy = y;\n"
        "	for (i = 0; i < n; i++) {\n"
        "		uint8 c = s[i];\n"
        "		if (c == (uint8)'\\n' || (wrap && cx + 6 > x + wrap)) {\n"
        "			cx = x;\n"
        "			cy += 10;\n"
        "			if (c == (uint8)'\\n' || c == (uint8)' ')\n"
        "				continue;\n"
        "		}\n"
        "		g3_fb_glyph(fb, cx, cy, c, cr, cg, cb);\n"
        "		cx += 6;\n"
        "	}\n"
        "}\n"
        "static void g3_draw_ditl3500(void)\n"
        "{\n"
        "	static int did;\n"
        "	uint32 fb, doff = 0, ln = 0;\n"
        "	uint8 txt[360];\n"
        "	int x, y;\n"
        "	size_t nr;\n"
        "	if (did)\n"
        "		return;\n"
        "	did = 1;\n"
        "	fb = g3_qd_fb();\n"
        "	for (y = 346; y < 366; y++)\n"
        "		for (x = 404; x < 494; x++)\n"
        "			g3_fb_xrgb(fb, x, y, 0xccu, 0xccu, 0xccu);\n"
        "	for (y = 346; y < 366; y++)\n"
        "		for (x = 302; x < 392; x++)\n"
        "			g3_fb_xrgb(fb, x, y, 0xccu, 0xccu, 0xccu);\n"
        "	g3_fb_text(fb, 414, 351, (const uint8 *)\"CONTINUE\", 8, 0,\n"
        "		   0, 0, 0);\n"
        "	g3_fb_text(fb, 318, 351, (const uint8 *)\"GO BACK\", 7, 0,\n"
        "		   0, 0, 0);\n"
        "	g3_res_src_off = g3_doc_rf_off;\n"
        "	if (g3_res_lookup(0x54455854u, 3502, &doff, &ln) && ln &&\n"
        "	    ln < sizeof(txt) && g3_toast_open()) {\n"
        "		fseek(g3_toast,\n"
        "		      (long)(g3_res_src_off + 256u + doff + 4u), SEEK_SET);\n"
        "		nr = fread(txt, 1, ln, g3_toast);\n"
        "		if (nr == ln)\n"
        "			g3_fb_text(fb, 16, 140, txt, (unsigned)ln, 480,\n"
        "				   0xffu, 0xffu, 0xffu);\n"
        "	}\n"
        "#if NW_BOOT_LOG\n"
        "	nw_boot_log(\n"
        "		\"G3: 68k Launch A9F2 CFM Upgrader PEF welbody\");\n"
        "#endif\n"
        "}\n"
        "static void g3_pict1000_blit(void)\n"
        "{\n"
        "\tuint32 doff = 0, ln = 0, fb, i, opoff;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welbody-fn")
    old = (
        "\t\t\tg3_pict_rid = 3500;\n"
        "\t\t\tg3_pict1000_blit();\n"
        "\t\t\tvideo_set_dirty_area(0, 80, 640, 298);\n"
        "\t\t\tVideoPresent();\n"
    )
    new = (
        "\t\t\tg3_pict_rid = 3500;\n"
        "\t\t\tg3_pict1000_blit();\n"
        "\t\t\tg3_draw_ditl3500();\n"
        "\t\t\tvideo_set_dirty_area(0, 80, 640, 298);\n"
        "\t\t\tVideoPresent();\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welbody-call")
    return _insert_after_enter(text, _log_enter(m, "nwelbody"), "cpu-pef-welbody")


def patch_cpu_pef_cupgnd(text: str) -> str:
    """KEEP 17208: Welcome TEXT 3502 + Continue/Go Back on the window.
    CUP inf=0x2aaf5 a6=3500 is a no-op then NumToString -108
    (memFullErr). Dispatch GetNewDialog 3500 so Welcome.plug owns
    DITL 3500. Do not remill leftover:pef-welbody. Do not skip-68k."""
    m = _MARKERS["pef-cupgnd"]
    if m in text:
        return text
    old = (
        "		else if (a4 == 0x2f5u)\n"
        "			r3 = g3_pef_host(153u, 0x54455854u, 3502u, 0, 0, 0);\n"
    )
    new = (
        "		else if (a4 == 0x2f5u)\n"
        "			r3 = g3_pef_host(153u, 0x54455854u, 3502u, 0, 0, 0);\n"
        "		else if (a4 == 0x2aaf5u)\n"
        "			r3 = g3_pef_host(134u, 3500u, 0, 0, 0, 0);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupgnd")
    return _insert_after_enter(text, _log_enter(m, "ncupgnd"), "cpu-pef-cupgnd")


def patch_cpu_pef_ditlh(text: str) -> str:
    """KEEP 17209: GetNewDialog 3500 live then plugin returns;
    Upgrader GetNewCWindow 129 clobbers 1004e000, DisposeWindow,
    NumToString -108. Attach DITL 3500 as DialogRecord.items and
    keep WIND 128 across GetNewCWindow 129. Do not remill
    leftover:pef-cupgnd or leftover:pef-modal. Do not skip-68k."""
    m = _MARKERS["pef-ditlh"]
    if m in text:
        return text
    old = (
        "	if (id == 128u && g3_wind128)\n"
        "		return g3_wind128;\n"
    )
    new = (
        "	if (g3_wind128)\n"
        "		return g3_wind128;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ditlh-keep")
    old = (
        "		else if ((a3 & 0xffffu) == 3500u) {\n"
        "			r3 = g3_plant_wind(128u);\n"
    )
    new = (
        "		else if ((a3 & 0xffffu) == 3500u) {\n"
        "			uint32 doff = 0, ln = 0, ih = 0;\n"
        "			r3 = g3_plant_wind(128u);\n"
        "			if (g3_res_lookup(0x4449544cu, 3500, &doff, &ln) && ln)\n"
        "				ih = g3_res_plant(doff, ln);\n"
        "			if (r3 && ih && g3_ea_data(r3 + 171u)) {\n"
        "				vm_write_memory_4(r3 + 156u, ih);\n"
        "				vm_write_memory_2(r3 + 168u, 1);\n"
        "			}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ditlh-items")
    return _insert_after_enter(text, _log_enter(m, "nditlh"), "cpu-pef-ditlh")


def patch_cpu_pef_cupaff(text: str) -> str:
    """KEEP 17210: GetNewDialog 3500 + DITL items, then plugin CUP
    inf=0xAFF5 returns 0, r31=0x803, skip 1024098c dialog loop,
    Upgrader DisposeWindow / -108. Return a non-nil handle so
    Welcome.plug reaches the loop. Raise CUP log cap. Do not remill
    leftover:pef-ditlh or leftover:pef-modal. Do not skip-68k."""
    m = _MARKERS["pef-cupaff"]
    if m in text:
        return text
    old = (
        "		else if (a4 == 0x2aaf5u)\n"
        "			r3 = g3_pef_host(134u, 3500u, 0, 0, 0, 0);\n"
    )
    new = (
        "		else if (a4 == 0x2aaf5u)\n"
        "			r3 = g3_pef_host(134u, 3500u, 0, 0, 0, 0);\n"
        "		else if (a4 == 0xaff5u || a4 == 0x1aff5u) {\n"
        "			uint32 p = g3_pef_newptr(8u);\n"
        "			if (p && g3_ea_data(p + 7u)) {\n"
        "				vm_write_memory_4(p, p + 4u);\n"
        "				vm_write_memory_4(p + 4u, 0);\n"
        "				r3 = p;\n"
        "			} else\n"
        "				r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ncaff;\n"
        "				if (ncaff < 8) {\n"
        "					char buf[96];\n"
        "					ncaff++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupaff h=%08x\",\n"
        "						 (unsigned)r3);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupaff")
    old = (
        "			static unsigned ncup;\n"
        "			if (ncup < 8) {\n"
    )
    new = (
        "			static unsigned ncup;\n"
        "			if (ncup < 32) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupaff-cap")
    return _insert_after_enter(text, _log_enter(m, "ncupaff"), "cpu-pef-cupaff")


_PATCH: Dict[str, Callable[[str], str]] = {
    "pef-docflag": patch_cpu_pef_docflag,
    "pef-aeproc": patch_cpu_pef_aeproc,
    "pef-fspnow": patch_cpu_pef_fspnow,
    "pef-inss": patch_cpu_pef_inss,
    "pef-catfd": patch_cpu_pef_catfd,
    "pef-tsqc": patch_cpu_pef_tsqc,
    "pef-gndtv": patch_cpu_pef_gndtv,
    "pef-tsqcf": patch_cpu_pef_tsqcf,
    "pef-dqhdr": patch_cpu_pef_dqhdr,
    "pef-gdf2": patch_cpu_pef_gdf2,
    "pef-gdf3": patch_cpu_pef_gdf3,
    "pef-hsz64": patch_cpu_pef_hsz64,
    "pef-nersp": patch_cpu_pef_nersp,
    "pef-gdfnm": patch_cpu_pef_gdfnm,
    "pef-gdfn6": patch_cpu_pef_gdfn6,
    "pef-welpef": patch_cpu_pef_welpef,
    "pef-cuparg": patch_cpu_pef_cuparg,
    "pef-cupld": patch_cpu_pef_cupld,
    "pef-welr2": patch_cpu_pef_welr2,
    "pef-welarg": patch_cpu_pef_welarg,
    "pef-cupgis": patch_cpu_pef_cupgis,
    "pef-cupall": patch_cpu_pef_cupall,
    "pef-fw128": patch_cpu_pef_fw128,
    "pef-cuptxt": patch_cpu_pef_cuptxt,
    "pef-welrf": patch_cpu_pef_welrf,
    "pef-pict3500": patch_cpu_pef_pict3500,
    "pef-drawwel": patch_cpu_pef_drawwel,
    "pef-xrgbff": patch_cpu_pef_xrgbff,
    "pef-fbpres": patch_cpu_pef_fbpres,
    "pef-dd3500": patch_cpu_pef_dd3500,
    "pef-noddfill": patch_cpu_pef_noddfill,
    "pef-cupgmd": patch_cpu_pef_cupgmd,
    "pef-welbody": patch_cpu_pef_welbody,
    "pef-cupgnd": patch_cpu_pef_cupgnd,
    "pef-ditlh": patch_cpu_pef_ditlh,
    "pef-cupaff": patch_cpu_pef_cupaff,
}


def apply_pef_openq(kind: str, cpu: Path) -> None:
    fn = _PATCH.get(kind)
    if fn is None:
        raise ValueError("unknown pef open kind %s" % kind)
    cpu.write_text(fn(cpu.read_text()))
