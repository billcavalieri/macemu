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
    ("pef-skipditl", "skipditl"),
    ("pef-wplug802", "wplug802"),
    ("pef-cupgnd", "cupgnd"),
    ("pef-ditlh", "ditlh"),
    ("pef-cupaff", "cupaff"),
    ("pef-cupabd", "cupabd"),
    ("pef-cupnctl", "cupnctl"),
    ("pef-ctlown", "ctlown"),
    ("pef-ctllst", "ctllst"),
    ("pef-str1050", "str1050"),
    ("pef-impcap", "impcap"),
    ("pef-impwel", "impwel"),
    ("pef-wrefcon", "wrefcon"),
    ("pef-swtitle", "swtitle"),
    ("pef-str520", "str520"),
    ("pef-s520b", "s520b"),
    ("pef-sub2050", "sub2050"),
    ("pef-novid", "novid"),
    ("pef-npicv", "npicv"),
    ("pef-sdit2050", "sdit2050"),
    ("pef-subfit", "subfit"),
    ("pef-alrt1", "alrt1"),
    ("pef-g520a3", "g520a3"),
    ("pef-lowisi", "lowisi"),
    ("pef-low68k", "low68k"),
    ("pef-welloop", "welloop"),
    ("pef-lowblr", "lowblr"),
    ("pef-lowdata", "lowdata"),
    ("pef-lowoff", "lowoff"),
    ("pef-gdblr", "gdblr"),
    ("pef-gdtbl", "gdtbl"),
    ("pef-gndalrt", "gndalrt"),
    ("pef-gdzero", "gdzero"),
    ("pef-b0host", "b0host"),
    ("pef-b0ptr", "b0ptr"),
    ("pef-b0ptxt", "b0ptxt"),
    ("pef-gnd501", "gnd501"),
    ("pef-b0alrt", "b0alrt"),
    ("pef-welmd", "welmd"),
    ("pef-mdloop", "mdloop"),
    ("pef-mdret", "mdret"),
    ("pef-mdtoc", "mdtoc"),
    ("pef-a991c", "a991c"),
    ("pef-srl", "srl"),
    ("pef-hunl", "hunl"),
    ("pef-sutil", "sutil"),
    ("pef-cntmi", "cntmi"),
    ("pef-cntres", "cntres"),
    ("pef-cmsz", "cmsz"),
    ("pef-mnud", "mnud"),
    ("pef-nhc", "nhc"),
    ("pef-a029", "a029"),
    ("pef-qdext", "qdext"),
    ("pef-wmgp", "wmgp"),
    ("pef-setc", "setc"),
    ("pef-fillc", "fillc"),
    ("pef-layer", "layer"),
    ("pef-a8e0", "a8e0"),
    ("pef-unionrgn", "unionrgn"),
    ("pef-aa8d8", "aa8d8"),
    ("pef-open", "open"),
    ("pef-readxpram", "readxpram"),
    ("pef-disposeptr", "disposeptr"),
    ("pef-debugger", "debugger"),
    ("pef-forecolor", "forecolor"),
    ("pef-backcolor", "backcolor"),
    ("pef-setappllim", "setappllim"),
    ("pef-saverestor", "saverestor"),
    ("pef-aa874", "aa874"),
    ("pef-disposehan", "disposehan"),
    ("pef-getzone", "getzone"),
    ("pef-localtoglo", "localtoglo"),
    ("pef-setpbits", "setpbits"),
    ("pef-aa8ec", "aa8ec"),
    ("pef-trapabe9", "trapabe9"),
    ("pef-getostrapa", "getostrapa"),
    ("pef-gettooltra", "gettooltra"),
    ("pef-powerdispa", "powerdispa"),
    ("pef-status", "status"),
    ("pef-moremaster", "moremaster"),
    ("pef-initutil", "initutil"),
    ("pef-blockmoved", "blockmoved"),
    ("pef-sethandles", "sethandles"),
    ("pef-newhandle", "newhandle"),
    ("pef-control", "control"),
    ("pef-setptrsize", "setptrsize"),
    ("pef-memorydisp", "memorydisp"),
    ("pef-aa15c", "aa15c"),
    ("pef-trapa402", "trapa402"),
    ("pef-trapa403", "trapa403"),
    ("pef-syserror", "syserror"),
    ("pef-blockmove", "blockmove"),
    ("pef-newgdevice", "newgdevice"),
    ("pef-setdevicea", "setdevicea"),
    ("pef-getcwmgrpo", "getcwmgrpo"),
    ("pef-aa8dc", "aa8dc"),
    ("pef-textwidth", "textwidth"),
    ("pef-aa893", "aa893"),
    ("pef-uprstring", "uprstring"),
    ("pef-hfsdispatc", "hfsdispatc"),
    ("pef-hopenrf", "hopenrf"),
    ("pef-hcreate", "hcreate"),
    ("pef-hdelete", "hdelete"),
    ("pef-hgetfilein", "hgetfilein"),
    ("pef-hsetflock", "hsetflock"),
    ("pef-hrstflock", "hrstflock"),
    ("pef-hrename", "hrename"),
    ("pef-hopenresfi", "hopenresfi"),
    ("pef-hcreateres", "hcreateres"),
    ("pef-getvolinfo", "getvolinfo"),
    ("pef-hopen", "hopen"),
    ("pef-trapa600", "trapa600"),
    ("pef-trapa609", "trapa609"),
    ("pef-trapa401", "trapa401"),
    ("pef-trapa660", "trapa660"),
    ("pef-hgetvinfo", "hgetvinfo"),
    ("pef-trapa607", "trapa607"),
    ("pef-trapa60c", "trapa60c"),
    ("pef-hsetfilein", "hsetfilein"),
    ("pef-trapa60d", "trapa60d"),
    ("pef-trapa608", "trapa608"),
    ("pef-trapa410", "trapa410"),
    ("pef-trapa412", "trapa412"),
    ("pef-trapa411", "trapa411"),
    ("pef-aa013", "aa013"),
    ("pef-trapa413", "trapa413"),
    ("pef-trapa23c", "trapa23c"),
    ("pef-trapa250", "trapa250"),
    ("pef-decstr68k", "decstr68k"),
    ("pef-trapaa7f", "trapaa7f"),
    ("pef-ptrtohand", "ptrtohand"),
    ("pef-getscrap", "getscrap"),
    ("pef-putscrap", "putscrap"),
    ("pef-invalrgn", "invalrgn"),
    ("pef-paintone", "paintone"),
    ("pef-getpenstat", "getpenstat"),
    ("pef-testdevice", "testdevice"),
    ("pef-setpenstat", "setpenstat"),
    ("pef-getwvarian", "getwvarian"),
    ("pef-emptyrgn", "emptyrgn"),
    ("pef-setemptyrg", "setemptyrg"),
    ("pef-drawmenuba", "drawmenuba"),
    ("pef-tickcount", "tickcount"),
    ("pef-devicemgr", "devicemgr"),
    ("pef-atamgr", "atamgr"),
    ("pef-detachreso", "detachreso"),
    ("pef-drvrinstal", "drvrinstal"),
    ("pef-comparestr", "comparestr"),
    ("pef-curresfile", "curresfile"),
    ("pef-useresfile", "useresfile"),
    ("pef-get1resour", "get1resour"),
    ("pef-loadresour", "loadresour"),
    ("pef-releaseres", "releaseres"),
    ("pef-writeparam", "writeparam"),
    ("pef-nminstall", "nminstall"),
    ("pef-nmremove", "nmremove"),
    ("pef-internalwa", "internalwa"),
    ("pef-writexpram", "writexpram"),
    ("pef-setadbinfo", "setadbinfo"),
    ("pef-primetime", "primetime"),
    ("pef-rmvtime", "rmvtime"),
    ("pef-trapa190", "trapa190"),
    ("pef-openrgn", "openrgn"),
    ("pef-frameround", "frameround"),
    ("pef-closergn", "closergn"),
    ("pef-poweroff", "poweroff"),
    ("pef-rgbforecol", "rgbforecol"),
    ("pef-getindadb", "getindadb"),
    ("pef-insxtime", "insxtime"),
    ("pef-getgdevice", "getgdevice"),
    ("pef-welqd", "welqd"),
    ("pef-cblit", "cblit"),
    ("pef-cbhost", "cbhost"),
    ("pef-powermgrdi", "powermgrdi"),
    ("pef-newgestalt", "newgestalt"),
    ("pef-sizersrc", "sizersrc"),
    ("pef-trapa098", "trapa098"),
    ("pef-setgrowzon", "setgrowzon"),
    ("pef-loadscrap", "loadscrap"),
    ("pef-aa015", "aa015"),
    ("pef-aa03b", "aa03b"),
    ("pef-mixedmodem", "mixedmodem"),
    ("pef-a8ecp", "a8ecp"),
    ("pef-paintrm", "paintrm"),
    ("pef-setprm", "setprm"),
    ("pef-cb32", "cb32"),
    ("pef-fgcol", "fgcol"),
    ("pef-bgcol", "bgcol"),
    ("pef-rgbfg", "rgbfg"),
    ("pef-ltglo", "ltglo"),
    ("pef-setcrm", "setcrm"),
]
assert len(_HOSTS) == 224, len(_HOSTS)

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


def patch_cpu_pef_cupabd(text: str) -> str:
    """KEEP 17211: CUP inf=0xAFF5 never ran. After FrontWindow live
    inf=abfda5 (NewControl) and inf=65 then DisposeWindow / -108.
    idx 63 NewControl returns 0. Plant a ControlHandle for
    inf=0xabfda5; inf=0x65 stays r3=0. Do not remill leftover:pef-cupaff.
    Do not remill leftover:pef-modal. Do not skip-68k."""
    m = _MARKERS["pef-cupabd"]
    if m in text:
        return text
    old = (
        "		else if (a4 == 0xaff5u || a4 == 0x1aff5u) {\n"
        "			uint32 p = g3_pef_newptr(8u);\n"
    )
    new = (
        "		else if (a4 == 0xabfda5u) {\n"
        "			uint32 c = g3_pef_newptr(64u);\n"
        "			uint32 h = g3_pef_newptr(8u);\n"
        "			if (c && h && g3_ea_data(h + 3u)) {\n"
        "				vm_write_memory_4(h, c);\n"
        "				if (a5 && g3_ea_data(c + 3u))\n"
        "					vm_write_memory_4(c, a5);\n"
        "				r3 = h;\n"
        "			} else\n"
        "				r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ncabd;\n"
        "				if (ncabd < 8) {\n"
        "					char buf[96];\n"
        "					ncabd++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupabd h=%08x inf65\",\n"
        "						 (unsigned)r3);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if (a4 == 0x65u) {\n"
        "			r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nc65;\n"
        "				if (nc65 < 8) {\n"
        "					nc65++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF cup65\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if (a4 == 0xaff5u || a4 == 0x1aff5u) {\n"
        "			uint32 p = g3_pef_newptr(8u);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupabd")
    return _insert_after_enter(text, _log_enter(m, "ncupabd"), "cpu-pef-cupabd")


def patch_cpu_pef_cupnctl(text: str) -> str:
    """KEEP 17211: CUP inf=abfda5 then inf=65, import idx 63 NewControl
    r3=0, DisposeWindow / -108. leftover:pef-cupabd REVERT g0_only —
    do not remill it. Plant ControlRecord (owner at +4, vis=0xff,
    bounds from a6) for CUP 0xabfda5 and idx 63; inf=0x65 returns that
    handle. Do not remill leftover:pef-cupaff. Do not skip-68k."""
    m = _MARKERS["pef-cupnctl"]
    if m in text:
        return text
    old = (
        "static uint32 g3_pef_heap;\n"
        "static uint32 g3_pef_newptr(uint32 n)\n"
    )
    new = (
        "static uint32 g3_pef_heap;\n"
        "static uint32 g3_pef_ctlh;\n"
        "static uint32 g3_pef_newptr(uint32 n)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupnctl-heap")
    old = (
        "	if (idx == 63u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 63u) {\n"
        "		uint32 c = g3_pef_newptr(256u);\n"
        "		uint32 h = g3_pef_newptr(8u);\n"
        "		r3 = 0;\n"
        "		if (c && h && g3_ea_data(h + 7u) && g3_ea_data(c + 16u)) {\n"
        "			vm_write_memory_4(h, c);\n"
        "			if (a3)\n"
        "				vm_write_memory_4(c + 4u, a3);\n"
        "			if (a4 && g3_ea_data(a4 + 7u)) {\n"
        "				vm_write_memory_4(c + 8u, vm_read_memory_4(a4));\n"
        "				vm_write_memory_4(c + 12u, vm_read_memory_4(a4 + 4u));\n"
        "			}\n"
        "			vm_write_memory_1(c + 16u, 0xff);\n"
        "			r3 = h;\n"
        "			g3_pef_ctlh = h;\n"
        "		} else\n"
        "			r3 = 1;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupnctl-idx63")
    old = (
        "		else if (a4 == 0xaff5u || a4 == 0x1aff5u) {\n"
        "			uint32 p = g3_pef_newptr(8u);\n"
    )
    new = (
        "		else if (a4 == 0xabfda5u) {\n"
        "			uint32 c = g3_pef_newptr(256u);\n"
        "			uint32 h = g3_pef_newptr(8u);\n"
        "			if (c && h && g3_ea_data(h + 7u) && g3_ea_data(c + 16u)) {\n"
        "				vm_write_memory_4(h, c);\n"
        "				if (a5)\n"
        "					vm_write_memory_4(c + 4u, a5);\n"
        "				if (a6 && g3_ea_data(a6 + 7u)) {\n"
        "					vm_write_memory_4(c + 8u, vm_read_memory_4(a6));\n"
        "					vm_write_memory_4(c + 12u, vm_read_memory_4(a6 + 4u));\n"
        "				}\n"
        "				vm_write_memory_1(c + 16u, 0xff);\n"
        "				r3 = h;\n"
        "				g3_pef_ctlh = h;\n"
        "			} else\n"
        "				r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ncnc;\n"
        "				if (ncnc < 8) {\n"
        "					char buf[112];\n"
        "					ncnc++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupnctl h=%08x a5=%08x\",\n"
        "						 (unsigned)r3, (unsigned)a5);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if (a4 == 0x65u) {\n"
        "			r3 = g3_pef_ctlh ? g3_pef_ctlh : 1u;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nc65;\n"
        "				if (nc65 < 8) {\n"
        "					char buf[96];\n"
        "					nc65++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF cupnctl inf65 r3=%08x\",\n"
        "						 (unsigned)r3);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if (a4 == 0xaff5u || a4 == 0x1aff5u) {\n"
        "			uint32 p = g3_pef_newptr(8u);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cupnctl")
    return _insert_after_enter(text, _log_enter(m, "ncupnctl"), "cpu-pef-cupnctl")


def patch_cpu_pef_ctlown(text: str) -> str:
    """KEEP 17213: CUP inf=abfda5 planted ControlHandle but a5=0x803
    (Welcome.plug skip-dialog err, not WIND 128). NewControl is Pascal
    stack-based so r5 is leftover. Owner at +4 was 0x803; plugin still
    returned, GetString 1050 / GetNewCWindow 129 / NumToString 2050.
    Write owner = WIND 128 (RAMBase+0x4e000) unless a3/a5 is a live
    window. Do not remill leftover:pef-cupnctl. Do not skip-68k."""
    m = _MARKERS["pef-ctlown"]
    if m in text:
        return text
    old = (
        "			if (a3)\n"
        "				vm_write_memory_4(c + 4u, a3);\n"
    )
    new = (
        "			{\n"
        "				uint32 own = RAMBase + 0x4e000u;\n"
        "				if (a3 && a3 != 0x803u && g3_ea_data(a3))\n"
        "					own = a3;\n"
        "				vm_write_memory_4(c + 4u, own);\n"
        "			}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ctlown-idx63")
    old = (
        "				if (a5)\n"
        "					vm_write_memory_4(c + 4u, a5);\n"
    )
    new = (
        "				{\n"
        "					uint32 own = RAMBase + 0x4e000u;\n"
        "					if (a5 && a5 != 0x803u && g3_ea_data(a5))\n"
        "						own = a5;\n"
        "					vm_write_memory_4(c + 4u, own);\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nown;\n"
        "						if (nown < 8) {\n"
        "							char buf[96];\n"
        "							nown++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF ctlown o=%08x a5=%08x\",\n"
        "								 (unsigned)own, (unsigned)a5);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ctlown-cup")
    return _insert_after_enter(text, _log_enter(m, "nctlown"), "cpu-pef-ctlown")


def patch_cpu_pef_ctllst(text: str) -> str:
    """KEEP 17214: ctlown o=1004e000 live; plugin still returns after
    GetString 1050 / GetNewCWindow 129 / NumToString 2050. NewControl
    never linked the handle into WindowRecord.controlList (w+140;
    items stay at +156). Push h onto own+140 and nextControl=prev.
    Do not remill leftover:pef-ctlown. Do not skip-68k."""
    m = _MARKERS["pef-ctllst"]
    if m in text:
        return text
    old = (
        "static uint32 g3_pef_ctlh;\n"
        "static uint32 g3_pef_newptr(uint32 n)\n"
    )
    new = (
        "static uint32 g3_pef_ctlh;\n"
        "static void g3_pef_link_ctl(uint32 h, uint32 c)\n"
        "{\n"
        "	uint32 own = 0, prev = 0;\n"
        "	if (!h || !c || !g3_ea_data(c + 3u))\n"
        "		return;\n"
        "	own = vm_read_memory_4(c + 4u);\n"
        "	if (own && g3_ea_data(own + 143u)) {\n"
        "		prev = vm_read_memory_4(own + 140u);\n"
        "		vm_write_memory_4(own + 140u, h);\n"
        "	}\n"
        "	vm_write_memory_4(c, prev);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nlst;\n"
        "		if (nlst < 8) {\n"
        "			char buf[96];\n"
        "			nlst++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF ctllst h=%08x o=%08x p=%08x\",\n"
        "				 (unsigned)h, (unsigned)own, (unsigned)prev);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "}\n"
        "static uint32 g3_pef_newptr(uint32 n)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ctllst-fn")
    old = (
        "			r3 = h;\n"
        "			g3_pef_ctlh = h;\n"
        "		} else\n"
        "			r3 = 1;\n"
    )
    new = (
        "			r3 = h;\n"
        "			g3_pef_ctlh = h;\n"
        "			g3_pef_link_ctl(h, c);\n"
        "		} else\n"
        "			r3 = 1;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ctllst-idx63")
    old = (
        "				r3 = h;\n"
        "				g3_pef_ctlh = h;\n"
        "			} else\n"
        "				r3 = 1;\n"
    )
    new = (
        "				r3 = h;\n"
        "				g3_pef_ctlh = h;\n"
        "				g3_pef_link_ctl(h, c);\n"
        "			} else\n"
        "				r3 = 1;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ctllst-cup")
    return _insert_after_enter(text, _log_enter(m, "nctllst"), "cpu-pef-ctllst")


def patch_cpu_pef_str1050(text: str) -> str:
    """KEEP 17215: ctllst linked 3 controls on WIND 128; plugin still
    returns after GetString 1050 r3=1004f138 / GetNewCWindow 129 /
    NumToString 2050. Log Pascal n/bytes; if empty plant
    "Install Mac OS 9.2.1". Do not remill leftover:pef-ctllst.
    Do not skip-68k."""
    m = _MARKERS["pef-str1050"]
    if m in text:
        return text
    old = (
        "		if (g3_res_lookup(0x53545220u, sid, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
        "#if NW_BOOT_LOG\n"
    )
    new = (
        "		if (g3_res_lookup(0x53545220u, sid, &doff, &ln) && ln)\n"
        "			r3 = g3_res_plant(doff, ln);\n"
        "		if (sid == 1050) {\n"
        "			uint32 p = 0, plen = 0;\n"
        "			if (r3 && g3_ea_data(r3 + 3u))\n"
        "				p = vm_read_memory_4(r3);\n"
        "			if (p && g3_ea_data(p))\n"
        "				plen = vm_read_memory_1(p);\n"
        "			if (!r3 || !p || plen == 0) {\n"
        "				const char *s = \"Install Mac OS 9.2.1\";\n"
        "				unsigned sl = 20, i;\n"
        "				uint32 d = g3_pef_newptr(32u);\n"
        "				uint32 h = g3_pef_newptr(8u);\n"
        "				if (d && h && g3_ea_data(d + sl) &&\n"
        "				    g3_ea_data(h + 3u)) {\n"
        "					vm_write_memory_1(d, (uint8)sl);\n"
        "					for (i = 0; i < sl; i++)\n"
        "						vm_write_memory_1(d + 1u + i,\n"
        "								 (uint8)s[i]);\n"
        "					vm_write_memory_4(h, d);\n"
        "					r3 = h;\n"
        "					p = d;\n"
        "					plen = sl;\n"
        "				}\n"
        "			}\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ns1050;\n"
        "				if (ns1050 < 8) {\n"
        "					char buf[96];\n"
        "					uint32 b0 = 0;\n"
        "					ns1050++;\n"
        "					if (p && g3_ea_data(p + 4u))\n"
        "						b0 = vm_read_memory_4(p);\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF str1050 h=%08x n=%u b=%08x\",\n"
        "						 (unsigned)r3, (unsigned)plen,\n"
        "						 (unsigned)b0);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "#if NW_BOOT_LOG\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-str1050")
    return _insert_after_enter(text, _log_enter(m, "nstr1050"), "cpu-pef-str1050")


def patch_cpu_pef_impcap(text: str) -> str:
    """KEEP 17216: STR 1050 is Pascal "Help" (n=4 b=0448656c), not
    empty. Plugin still returns with no CUP after inf=65. Import idx
    log caps at 24 so hosts after GetString 1050 are invisible. Raise
    nimp 24->64 and log r4. Do not remill leftover:pef-str1050 or
    leftover:pef-nocap. Do not skip-68k."""
    m = _MARKERS["pef-impcap"]
    if m in text:
        return text
    old = (
        "		static unsigned nimp;\n"
        "		if (nimp < 24) {\n"
        "			char buf[96];\n"
        "			nimp++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF import idx=%u r3=%08x\",\n"
        "				 (unsigned)idx, (unsigned)a3);\n"
    )
    new = (
        "		static unsigned nimp;\n"
        "		if (nimp < 64) {\n"
        "			char buf[96];\n"
        "			nimp++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF import idx=%u r3=%08x r4=%08x\",\n"
        "				 (unsigned)idx, (unsigned)a3, (unsigned)a4);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-impcap")
    return _insert_after_enter(text, _log_enter(m, "nimpcap"), "cpu-pef-impcap")


def patch_cpu_pef_impwel(text: str) -> str:
    """KEEP 17217: nimp 64 filled before Welcome (last idx=134
    GetNewDialog 510). GetString 1050 "Help" then GetPort /
    GetNewCWindow 129 with no import idx. Log g3_pef_host after
    welpef with a dedicated cap. Do not remill leftover:pef-impcap.
    Do not skip-68k."""
    m = _MARKERS["pef-impwel"]
    if m in text:
        return text
    old = (
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	(void)g3_pef_void_st;\n"
    )
    new = (
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "#if NW_BOOT_LOG\n"
        "	if (g3_did_welpef) {\n"
        "		static unsigned nwelimp;\n"
        "		if (nwelimp < 64) {\n"
        "			char buf[112];\n"
        "			nwelimp++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF impwel idx=%u r3=%08x r4=%08x r5=%08x\",\n"
        "				 (unsigned)idx, (unsigned)a3, (unsigned)a4,\n"
        "				 (unsigned)a5);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	(void)g3_pef_void_st;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-impwel")
    return _insert_after_enter(text, _log_enter(m, "nimpwel"), "cpu-pef-impwel")


def patch_cpu_pef_wrefcon(text: str) -> str:
    """KEEP 17218: after GetString 1050 Help, GetResource DITL 1050
    and NewControl CNTL/USER/PICT. SetWRefCon idx 219 is a no-op;
    GetWRefCon idx 180 always r3=0. Store/load WindowRecord.refCon
    at w+152 (items stay at +156). Do not remill leftover:pef-impwel.
    Do not skip-68k."""
    m = _MARKERS["pef-wrefcon"]
    if m in text:
        return text
    old = (
        "	if (idx == 219u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 219u) {\n"
        "		if (a3 && g3_ea_data(a3 + 155u))\n"
        "			vm_write_memory_4(a3 + 152u, a4);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nswr;\n"
        "			if (nswr < 8) {\n"
        "				char buf[96];\n"
        "				nswr++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF wrefcon set w=%08x r=%08x\",\n"
        "					 (unsigned)a3, (unsigned)a4);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-wrefcon-set")
    old = (
        "	if (idx == 180u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 180u) {\n"
        "		r3 = 0;\n"
        "		if (a3 && g3_ea_data(a3 + 155u))\n"
        "			r3 = vm_read_memory_4(a3 + 152u);\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngwr;\n"
        "			if (ngwr < 8) {\n"
        "				char buf[96];\n"
        "				ngwr++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF wrefcon get w=%08x r=%08x\",\n"
        "					 (unsigned)a3, (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-wrefcon-get")
    return _insert_after_enter(text, _log_enter(m, "nwrefcon"), "cpu-pef-wrefcon")


def patch_cpu_pef_swtitle(text: str) -> str:
    """KEEP 17218: after DITL 1050 CNTL/USER/PICT walk, SetWTitle x2
    then NumToString 2050 / GetIndString 520. leftover:pef-wrefcon
    REVERT msr-collapse writing w+152 — do not remill it. Copy the
    Pascal title to a StringHandle and log n/bytes; do not write
    WindowRecord. Do not skip-68k."""
    m = _MARKERS["pef-swtitle"]
    if m in text:
        return text
    old = (
        "static uint32 g3_pef_ctlh;\n"
        "static void g3_pef_link_ctl(uint32 h, uint32 c)\n"
    )
    new = (
        "static uint32 g3_pef_ctlh;\n"
        "static uint32 g3_pef_wtitle;\n"
        "static void g3_pef_link_ctl(uint32 h, uint32 c)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-swtitle-var")
    old = (
        "	if (idx == 127u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 127u) {\n"
        "		uint32 p = a4, plen = 0;\n"
        "		if (p && g3_ea_data(p))\n"
        "			plen = vm_read_memory_1(p);\n"
        "		if (plen > 255u)\n"
        "			plen = 255u;\n"
        "		if (p && plen && g3_ea_data(p + plen)) {\n"
        "			uint32 d = g3_pef_newptr(plen + 16u);\n"
        "			uint32 h = g3_pef_newptr(8u);\n"
        "			if (d && h && g3_ea_data(d + plen) &&\n"
        "			    g3_ea_data(h + 3u)) {\n"
        "				unsigned i;\n"
        "				for (i = 0; i <= plen; i++)\n"
        "					vm_write_memory_1(d + i,\n"
        "							 vm_read_memory_1(p + i));\n"
        "				vm_write_memory_4(h, d);\n"
        "				g3_pef_wtitle = h;\n"
        "			}\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nswt;\n"
        "			if (nswt < 8) {\n"
        "				char buf[96];\n"
        "				uint32 b0 = 0;\n"
        "				nswt++;\n"
        "				if (p && g3_ea_data(p + 3u))\n"
        "					b0 = vm_read_memory_4(p);\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF swtitle w=%08x n=%u b=%08x\",\n"
        "					 (unsigned)a3, (unsigned)plen,\n"
        "					 (unsigned)b0);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-swtitle")
    return _insert_after_enter(text, _log_enter(m, "nswtitle"), "cpu-pef-swtitle")


def patch_cpu_pef_str520(text: str) -> str:
    """KEEP 17220: SetWTitle "Install Mac OS 9.2.1" then "Help", then
    NumToString 2050 / GetIndString 520. Log Pascal n/bytes at dest;
    if empty plant a non-empty STR# 520 ix=1. Do not remill
    leftover:pef-swtitle or leftover:pef-wrefcon. Do not skip-68k."""
    m = _MARKERS["pef-str520"]
    if m in text:
        return text
    old = (
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngi;\n"
        "			if (ngi < 256 || sid == 3500 || sid == 520) {\n"
    )
    new = (
        "		if (sid == 520) {\n"
        "			uint32 plen = 0, b0 = 0;\n"
        "			if (a3 && g3_ea_data(a3))\n"
        "				plen = vm_read_memory_1(a3);\n"
        "			if (!plen && a3 && g3_ea_data(a3 + 16u)) {\n"
        "				const char *s = \"Help\";\n"
        "				unsigned sl = 4, i;\n"
        "				vm_write_memory_1(a3, (uint8)sl);\n"
        "				for (i = 0; i < sl; i++)\n"
        "					vm_write_memory_1(a3 + 1u + i,\n"
        "							 (uint8)s[i]);\n"
        "				plen = sl;\n"
        "			}\n"
        "			if (a3 && g3_ea_data(a3 + 3u))\n"
        "				b0 = vm_read_memory_4(a3);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ns520;\n"
        "				if (ns520 < 8) {\n"
        "					char buf[96];\n"
        "					ns520++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF str520 ix=%d n=%u b=%08x\",\n"
        "						 (int)six, (unsigned)plen,\n"
        "						 (unsigned)b0);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngi;\n"
        "			if (ngi < 256 || sid == 3500 || sid == 520) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-str520")
    return _insert_after_enter(text, _log_enter(m, "nstr520"), "cpu-pef-str520")


def patch_cpu_pef_s520b(text: str) -> str:
    """KEEP 17221: STR# 520 ix=1 n=25 b=19537973 ("Sys..."), not empty.
    Log the full Pascal text. Do not remill leftover:pef-str520.
    Do not skip-68k."""
    m = _MARKERS["pef-s520b"]
    if m in text:
        return text
    old = (
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF str520 ix=%d n=%u b=%08x\",\n"
        "						 (int)six, (unsigned)plen,\n"
        "						 (unsigned)b0);\n"
        "					nw_boot_log(buf);\n"
    )
    new = (
        "					{\n"
        "						char s[28];\n"
        "						unsigned i, n;\n"
        "						n = plen < 24u ? (unsigned)plen : 24u;\n"
        "						for (i = 0; i < n; i++) {\n"
        "							uint8 ch = 0x3f;\n"
        "							if (a3 && g3_ea_data(a3 + 1u + i))\n"
        "								ch = vm_read_memory_1(a3 + 1u + i);\n"
        "							if (ch < 32 || ch > 126)\n"
        "								ch = 0x2e;\n"
        "							s[i] = (char)ch;\n"
        "						}\n"
        "						s[n] = 0;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF s520b ix=%d n=%u s=%s\",\n"
        "							 (int)six, (unsigned)plen, s);\n"
        "					}\n"
        "					nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-s520b")
    return _insert_after_enter(text, _log_enter(m, "ns520b"), "cpu-pef-s520b")


def patch_cpu_pef_sub2050(text: str) -> str:
    """KEEP 17222: STR# 520 is "System Error ^0 occurred"; NumToString
    n=2050. leftover:pef-paramtxt/paramt already tested. Substitute ^0
    with the last NumToString text. Do not remill leftover:pef-s520b.
    Do not skip-68k."""
    m = _MARKERS["pef-sub2050"]
    if m in text:
        return text
    old = (
        "static uint32 g3_pef_wtitle;\n"
        "static void g3_pef_link_ctl(uint32 h, uint32 c)\n"
    )
    new = (
        "static uint32 g3_pef_wtitle;\n"
        "static char g3_n2s_txt[16];\n"
        "static unsigned g3_n2s_n;\n"
        "static void g3_pef_link_ctl(uint32 h, uint32 c)\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sub2050-var")
    old = (
        "		n = snprintf(buf, sizeof(buf), \"%d\", (int)a3);\n"
        "		if (n < 0)\n"
        "			n = 0;\n"
    )
    new = (
        "		n = snprintf(buf, sizeof(buf), \"%d\", (int)a3);\n"
        "		if (n < 0)\n"
        "			n = 0;\n"
        "		if (n > 15)\n"
        "			n = 15;\n"
        "		g3_n2s_n = (unsigned)n;\n"
        "		{\n"
        "			int i;\n"
        "			for (i = 0; i < n; i++)\n"
        "				g3_n2s_txt[i] = buf[i];\n"
        "			g3_n2s_txt[n] = 0;\n"
        "		}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sub2050-n2s")
    old = (
        "			if (a3 && g3_ea_data(a3 + 3u))\n"
        "				b0 = vm_read_memory_4(a3);\n"
    )
    new = (
        "			if (a3 && plen && g3_n2s_n && g3_ea_data(a3 + 255u)) {\n"
        "				unsigned i, j, k, np = 0;\n"
        "				uint8 tmp[256];\n"
        "				for (i = 1; i + 1u <= plen; i++) {\n"
        "					if (vm_read_memory_1(a3 + i) == (uint8)'^' &&\n"
        "					    vm_read_memory_1(a3 + i + 1u) == (uint8)'0') {\n"
        "						for (j = 1; j < i; j++)\n"
        "							tmp[np++] = vm_read_memory_1(a3 + j);\n"
        "						for (k = 0; k < g3_n2s_n && np < 255u; k++)\n"
        "							tmp[np++] = (uint8)g3_n2s_txt[k];\n"
        "						for (j = i + 2u; j <= plen && np < 255u; j++)\n"
        "							tmp[np++] = vm_read_memory_1(a3 + j);\n"
        "						vm_write_memory_1(a3, (uint8)np);\n"
        "						for (j = 0; j < np; j++)\n"
        "							vm_write_memory_1(a3 + 1u + j, tmp[j]);\n"
        "						plen = np;\n"
        "						break;\n"
        "					}\n"
        "				}\n"
        "			}\n"
        "			if (a3 && g3_ea_data(a3 + 3u))\n"
        "				b0 = vm_read_memory_4(a3);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sub2050")
    return _insert_after_enter(text, _log_enter(m, "nsub2050"), "cpu-pef-sub2050")


def patch_cpu_pef_novid(text: str) -> str:
    """KEEP 17223: STR# 520 became "System Error 2050 occurred"; hang-cap
    dies immediately after GetIndString 520 (VideoPresent from CPU
    thread). Skip VideoPresent on DrawDialog ditl3500 so Alert after
    520 can log. Do not remill leftover:pef-sub2050 or leftover:pef-fbpres.
    Do not skip-68k."""
    m = _MARKERS["pef-novid"]
    if m in text:
        return text
    old = (
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			VideoPresent();\n"
    )
    new = (
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			/* leftover:pef-novid: CPU-thread present crashes. */\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nnov;\n"
        "				if (nnov < 8) {\n"
        "					nnov++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF novid\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-novid")
    return _insert_after_enter(text, _log_enter(m, "nnovid"), "cpu-pef-novid")


def patch_cpu_pef_npicv(text: str) -> str:
    """KEEP 17224: novid skipped DrawDialog VideoPresent; hang-cap
    still dies at GetIndString 520 (~16s). Skip PICT 3500 blit
    VideoPresent too. Do not remill leftover:pef-novid. Do not skip-68k."""
    m = _MARKERS["pef-npicv"]
    if m in text:
        return text
    old = (
        "	if (rid == 3500) {\n"
        "		g3_draw_welcome_str();\n"
        "		video_set_dirty_area(0, 80, 640, 49);\n"
        "		VideoPresent();\n"
        "	}\n"
    )
    new = (
        "	if (rid == 3500) {\n"
        "		g3_draw_welcome_str();\n"
        "		video_set_dirty_area(0, 80, 640, 49);\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nnpv;\n"
        "			if (nnpv < 8) {\n"
        "				nnpv++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF npicv\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-npicv")
    return _insert_after_enter(text, _log_enter(m, "nnpicv"), "cpu-pef-npicv")


def patch_cpu_pef_sdit2050(text: str) -> str:
    """KEEP 17225: "System Error 2050 occurred" then hang-cap dies;
    SetDialogItemText idx 147 is a no-op. Log dlg/item/n and copy the
    Pascal text. Do not remill leftover:pef-setditxt or leftover:pef-npicv.
    Do not skip-68k."""
    m = _MARKERS["pef-sdit2050"]
    if m in text:
        return text
    old = (
        "	if (idx == 147u) {\n"
        "		r3 = 0;\n"
    )
    new = (
        "	if (idx == 147u) {\n"
        "		uint32 t = a5 ? a5 : a4, plen = 0;\n"
        "		if (t && g3_ea_data(t))\n"
        "			plen = vm_read_memory_1(t);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nsd;\n"
        "			if (nsd < 8) {\n"
        "				char buf[96];\n"
        "				nsd++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF sdit2050 d=%08x i=%08x n=%u\",\n"
        "					 (unsigned)a3, (unsigned)a4, (unsigned)plen);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sdit2050")
    return _insert_after_enter(text, _log_enter(m, "nsdit2050"), "cpu-pef-sdit2050")


def patch_cpu_pef_subfit(text: str) -> str:
    """KEEP 17226: sub2050 grew STR# 520 25->27 ("System Error 2050
    occurred") then process dies at GetIndString return — dest may
    not be Str255. Cap substituted length to the original plen.
    Do not remill leftover:pef-sub2050. Do not skip-68k."""
    m = _MARKERS["pef-subfit"]
    if m in text:
        return text
    old = (
        "						for (j = i + 2u; j <= plen && np < 255u; j++)\n"
        "							tmp[np++] = vm_read_memory_1(a3 + j);\n"
        "						vm_write_memory_1(a3, (uint8)np);\n"
    )
    new = (
        "						for (j = i + 2u; j <= plen && np < 255u; j++)\n"
        "							tmp[np++] = vm_read_memory_1(a3 + j);\n"
        "						if (np > plen)\n"
        "							np = plen;\n"
        "						vm_write_memory_1(a3, (uint8)np);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-subfit")
    return _insert_after_enter(text, _log_enter(m, "nsubfit"), "cpu-pef-subfit")


def patch_cpu_pef_alrt1(text: str) -> str:
    """KEEP 17227: "System Error 2050 occurr" n=25 then die at
    GetIndString 520; Alert idx 125 returns 0 and leftover:pef-alert
    is already tested. Return item 1 and log id. Do not remill
    leftover:pef-alert or leftover:pef-subfit. Do not skip-68k."""
    m = _MARKERS["pef-alrt1"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 125u) {\n"
        "		/* Alert: r3=0 so main 1010140c proceeds to splash. */\n"
        "		r3 = 0;\n"
    )
    new = (
        "	} else if (idx == 125u) {\n"
        "		/* leftover:pef-alrt1: item 1 after System Error 2050. */\n"
        "		r3 = 1;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned na1;\n"
        "			if (na1 < 8) {\n"
        "				char buf[96];\n"
        "				na1++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF alrt1 id=%08x\",\n"
        "					 (unsigned)a3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-alrt1")
    return _insert_after_enter(text, _log_enter(m, "nalrt1"), "cpu-pef-alrt1")


def patch_cpu_pef_g520a3(text: str) -> str:
    """KEEP 17228: Alert never runs; SS dies at GetIndString 520.
    Log dest a3/id/ix so we see if dest is a Handle not Str255.
    Do not remill leftover:pef-alrt1. Do not skip-68k."""
    m = _MARKERS["pef-g520a3"]
    if m in text:
        return text
    old = (
        "		if (sid == 520) {\n"
        "			uint32 plen = 0, b0 = 0;\n"
        "			if (a3 && g3_ea_data(a3))\n"
        "				plen = vm_read_memory_1(a3);\n"
    )
    new = (
        "		if (sid == 520) {\n"
        "			uint32 plen = 0, b0 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ng3a;\n"
        "				if (ng3a < 8) {\n"
        "					char buf[96];\n"
        "					ng3a++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF g520a3 d=%08x id=%d ix=%d\",\n"
        "						 (unsigned)a3, (int)sid, (int)six);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "			if (a3 && g3_ea_data(a3))\n"
        "				plen = vm_read_memory_1(a3);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-g520a3")
    return _insert_after_enter(text, _log_enter(m, "ng520a3"), "cpu-pef-g520a3")


def patch_cpu_pef_lowisi(text: str) -> str:
    """KEEP 17229: hang-cap dies SIGSEGV at host 0x400000014fe0
    (VM_ALLOCATE gap). guest_fetch allows pa<0x20000 then
    vm_read_memory_4; PA 0x14fe0 is past the 16K low map. Take
    the fetch off that PA (do not host-read). DSI vector 0x300
    stays allowed (pa<0x4000). Do not remill KEEP Welcome mills.
    Do not skip-68k."""
    m = _MARKERS["pef-lowisi"]
    if m in text:
        return text
    old = (
        "		      pa < 0x20000u ||\n"
    )
    new = (
        "		      pa < 0x4000u ||\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowisi-allow")
    old = (
        "#endif\n"
        "	*opcode = vm_read_memory_4(r.pa);\n"
        "#ifdef SHEEPSHAVER\n"
        "	/*\n"
        "	 * After a data DSI the CPU fetches 0x300 with IR off (identity PA).\n"
    )
    new = (
        "#endif\n"
        "#ifdef SHEEPSHAVER\n"
        "	if (r.pa >= 0x4000u && r.pa < 0x20000u) {\n"
        "		extern uint32 ROMBase, RAMBase, RAMSize;\n"
        "		uint32 lr = this->lr();\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nlow;\n"
        "			if (nlow < 16) {\n"
        "				char buf[96];\n"
        "				nlow++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF lowisi pa=%08x lr=%08x\",\n"
        "					 (unsigned)r.pa, (unsigned)lr);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		if ((lr >= RAMBase && lr < RAMBase + RAMSize) ||\n"
        "		    (lr >= ROMBase && lr < ROMBase + 0x500000u))\n"
        "			pc() = lr;\n"
        "		else if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "		return false;\n"
        "	}\n"
        "#endif\n"
        "	*opcode = vm_read_memory_4(r.pa);\n"
        "#ifdef SHEEPSHAVER\n"
        "	/*\n"
        "	 * After a data DSI the CPU fetches 0x300 with IR off (identity PA).\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowisi-read")
    old = (
        "	if (!ppc32_guest_mmu_enabled()) {\n"
        "		*opcode = vm_read_memory_4(pc());\n"
        "		return true;\n"
        "	}\n"
    )
    new = (
        "	if (!ppc32_guest_mmu_enabled()) {\n"
        "		if (pc() >= 0x4000u && pc() < 0x20000u)\n"
        "			return false;\n"
        "		*opcode = vm_read_memory_4(pc());\n"
        "		return true;\n"
        "	}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowisi-nommu")
    return _insert_after_enter(text, _log_enter(m, "nlowisi"), "cpu-pef-lowisi")


def patch_cpu_pef_low68k(text: str) -> str:
    """KEEP 17230: lowisi pa=00015000 lr=1010adcc then 1010ae08
    (retry loop); no IPS nested SIGSEGV; process still exited ~17s.
    Do not bounce to LR. Resume 68k hang (ROM+0x366084) so the
    process stays up. Keep Welcome mills. Do not remill
    leftover:pef-lowisi. Do not skip-68k."""
    m = _MARKERS["pef-low68k"]
    if m in text:
        return text
    old = (
        "		if ((lr >= RAMBase && lr < RAMBase + RAMSize) ||\n"
        "		    (lr >= ROMBase && lr < ROMBase + 0x500000u))\n"
        "			pc() = lr;\n"
        "		else if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "		return false;\n"
    )
    new = (
        "		if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n68;\n"
        "			if (n68 < 8) {\n"
        "				n68++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF low68k\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return false;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-low68k")
    return _insert_after_enter(text, _log_enter(m, "nlow68k"), "cpu-pef-low68k")


def patch_cpu_pef_welloop(text: str) -> str:
    """KEEP 17231: process lives (56.6s) after gap fetch pa=0x15000
    via 68k hang. Towards installer: if welpef is planted, resume
    Welcome.plug 1024098c (GetResource 'wppr' dialog loop) with TOC
    10242000 instead of ROM+0x366084. Keep lowisi. Do not remill
    leftover:pef-low68k. Do not skip-68k."""
    m = _MARKERS["pef-welloop"]
    if m in text:
        return text
    old = (
        "		if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n68;\n"
        "			if (n68 < 8) {\n"
        "				n68++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF low68k\");\n"
    )
    new = (
        "		if (g3_did_welpef) {\n"
        "			pc() = RAMBase + 0x24098cu;\n"
        "			gpr(2) = RAMBase + 0x242000u;\n"
        "		} else if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nwl;\n"
        "			if (nwl < 8) {\n"
        "				char buf[96];\n"
        "				nwl++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF welloop pc=%08x\",\n"
        "					 (unsigned)pc());\n"
        "				nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welloop")
    return _insert_after_enter(text, _log_enter(m, "nwelloop"), "cpu-pef-welloop")


def patch_cpu_pef_lowblr(text: str) -> str:
    """KEEP 17231: gap fetch pa=0x15000 from Upgrader lr=1010adcc.
    leftover:pef-welloop REVERT (16s die). Plant a mapped blr stub
    and resume there so the bad bctrl returns to Upgrader instead
    of 68k hang. Keep lowisi. Do not remill leftover:pef-welloop or
    leftover:pef-low68k. Do not skip-68k."""
    m = _MARKERS["pef-lowblr"]
    if m in text:
        return text
    old = (
        "		if (ROMBase)\n"
        "			pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n68;\n"
        "			if (n68 < 8) {\n"
        "				n68++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF low68k\");\n"
    )
    new = (
        "		{\n"
        "			static uint32 stub;\n"
        "			if (!stub) {\n"
        "				stub = g3_pef_newptr(16u);\n"
        "				if (stub && g3_ea_data(stub + 3u))\n"
        "					vm_write_memory_4(stub, 0x4e800020u);\n"
        "			}\n"
        "			if (stub)\n"
        "				pc() = stub;\n"
        "			else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
        "		}\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nblr;\n"
        "			if (nblr < 8) {\n"
        "				char buf[96];\n"
        "				nblr++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF lowblr pc=%08x\",\n"
        "					 (unsigned)pc());\n"
        "				nw_boot_log(buf);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowblr")
    return _insert_after_enter(text, _log_enter(m, "nlowblr"), "cpu-pef-lowblr")


def patch_cpu_pef_lowdata(text: str) -> str:
    """KEEP 17233: lowblr stub 101914a0 returns from bctrl 0x15000
    twice then process exits ~17s (no IPS). Data xlate of EA
    0x4000-0x20000 still host-SIGSEGVs. Redirect those PAs to a
    mapped dummy so Upgrader can continue after System Error 2050.
    Do not remill leftover:pef-lowblr or leftover:pef-welloop.
    Do not skip-68k."""
    m = _MARKERS["pef-lowdata"]
    if m in text:
        return text
    old = (
        "	if (r.ok) {\n"
        "		*pa = r.pa;\n"
        "		return true;\n"
        "	}\n"
        "#ifdef SHEEPSHAVER\n"
        "	/* Live 04cecd36: SECOND_DSI DAR=68fff0dc after PIC-idle\n"
    )
    new = (
        "	if (r.ok) {\n"
        "		*pa = r.pa;\n"
        "		if (*pa >= 0x4000u && *pa < 0x20000u) {\n"
        "			static uint32 dummy;\n"
        "			if (!dummy) {\n"
        "				dummy = g3_pef_newptr(64u);\n"
        "			}\n"
        "			if (dummy)\n"
        "				*pa = dummy + (*pa & 15u);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nld;\n"
        "				if (nld < 16) {\n"
        "					char buf[96];\n"
        "					nld++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF lowdata ea=%08x\",\n"
        "						 (unsigned)ea);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "		return true;\n"
        "	}\n"
        "#ifdef SHEEPSHAVER\n"
        "	/* Live 04cecd36: SECOND_DSI DAR=68fff0dc after PIC-idle\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowdata")
    return _insert_after_enter(text, _log_enter(m, "nlowdata"), "cpu-pef-lowdata")


def patch_cpu_pef_lowoff(text: str) -> str:
    """KEEP 17234: lowdata never ran (enter stamp only). After two
    lowblr, process exits ~17s. MMU-off data xlate is identity
    *pa=ea so 0x15000 host-reads the gap. Remap that EA to the
    dummy. Do not remill leftover:pef-lowdata. Do not skip-68k."""
    m = _MARKERS["pef-lowoff"]
    if m in text:
        return text
    old = (
        "	if (!ppc32_guest_mmu_enabled()) {\n"
        "		*pa = ea;\n"
        "		return true;\n"
        "	}\n"
    )
    new = (
        "	if (!ppc32_guest_mmu_enabled()) {\n"
        "		*pa = ea;\n"
        "		if (ea >= 0x4000u && ea < 0x20000u) {\n"
        "			static uint32 dummy;\n"
        "			if (!dummy)\n"
        "				dummy = g3_pef_newptr(64u);\n"
        "			if (dummy)\n"
        "				*pa = dummy + (ea & 15u);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nloff;\n"
        "				if (nloff < 16) {\n"
        "					char buf[96];\n"
        "					nloff++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF lowoff ea=%08x\",\n"
        "						 (unsigned)ea);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "		return true;\n"
        "	}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-lowoff")
    return _insert_after_enter(text, _log_enter(m, "nlowoff"), "cpu-pef-lowoff")


def patch_cpu_pef_gdblr(text: str) -> str:
    """KEEP 17235: bctrl 0x15000 from Upgrader 1010adcc; lowblr
    returns r3=0 twice then ~17s exit. leftover:pef-welloop REVERT.
    Set r3 = GetMainDevice mill (idx 43 / g3_wel_gdh) then blr so
    the call looks like GetMainDevice. Do not remill leftover:pef-lowblr
    or leftover:pef-cupgmd. Do not skip-68k."""
    m = _MARKERS["pef-gdblr"]
    if m in text:
        return text
    old = (
        "			if (stub)\n"
        "				pc() = stub;\n"
        "			else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    new = (
        "			if (stub) {\n"
        "				gpr(3) = g3_pef_host(43u, 0, 0, 0, 0, 0);\n"
        "				pc() = stub;\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned ngdb;\n"
        "				if (ngdb < 8) {\n"
        "					char buf[96];\n"
        "					ngdb++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdblr r3=%08x\",\n"
        "						 (unsigned)gpr(3));\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdblr")
    return _insert_after_enter(text, _log_enter(m, "ngdblr"), "cpu-pef-gdblr")


def patch_cpu_pef_gdtbl(text: str) -> str:
    """KEEP 17236: gdblr r3=10190fd0 (cupgmd GDevice) twice after
    GetIndString 520, lr=1010adcc then 1010ae08, then ~16.5s die.
    GetMainDevice already ran via idx 43; returning the handle is
    not enough. Plant ColorTable + RGBDirect on the PixMap so
    ResizeAndDisplayAlert can continue toward GetNewDialog 1010b26c.
    Do not remill leftover:pef-gdblr or leftover:pef-cupgmd.
    Do not skip-68k."""
    m = _MARKERS["pef-gdtbl"]
    if m in text:
        return text
    old = (
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
    )
    new = (
        "				vm_write_memory_4(pm, fb);\n"
        "				vm_write_memory_2(pm + 4u, 0x8a00u);\n"
        "				vm_write_memory_2(pm + 10u, 480);\n"
        "				vm_write_memory_2(pm + 12u, 640);\n"
        "				vm_write_memory_2(pm + 30u, 16);\n"
        "				vm_write_memory_2(pm + 32u, 32);\n"
        "				vm_write_memory_2(pm + 34u, 3);\n"
        "				vm_write_memory_2(pm + 36u, 8);\n"
        "				{\n"
        "					uint32 ct = g3_pef_newptr(16u);\n"
        "					uint32 cth = g3_pef_newptr(8u);\n"
        "					if (ct && cth) {\n"
        "						vm_write_memory_4(ct, 0x12345678u);\n"
        "						vm_write_memory_2(ct + 4u, 0);\n"
        "						vm_write_memory_2(ct + 6u, 0);\n"
        "						vm_write_memory_4(cth, ct);\n"
        "						vm_write_memory_4(pm + 42u, cth);\n"
        "					}\n"
        "				}\n"
        "				vm_write_memory_4(pmh, pm);\n"
        "				vm_write_memory_2(gd + 4u, 2);\n"
        "				vm_write_memory_2(gd + 20u, 1);\n"
        "				vm_write_memory_4(gd + 22u, pmh);\n"
        "				vm_write_memory_2(gd + 38u, 480);\n"
        "				vm_write_memory_2(gd + 40u, 640);\n"
        "				vm_write_memory_4(h, gd);\n"
        "				g3_wel_gdh = h;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngdt;\n"
        "					if (ngdt < 8) {\n"
        "						char buf[96];\n"
        "						ngdt++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gdtbl h=%08x\",\n"
        "							 (unsigned)h);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdtbl")
    return _insert_after_enter(text, _log_enter(m, "ngdtbl"), "cpu-pef-gdtbl")


def patch_cpu_pef_gndalrt(text: str) -> str:
    """KEEP 17237: gdtbl planted ColorTable on cupgmd PixMap; still
    two bctrl 0x15000 after GetIndString 520 (lr=1010adcc then
    1010ae08) then ~20s die. GDevice deref is the killer, not a
    missing table. Skip that prelude: on those LRs resume at
    ResizeAndDisplayAlert GetNewDialog 1010b26c (callAlert site).
    Do not remill leftover:pef-gdtbl, leftover:pef-gdblr,
    leftover:pef-skipalert, leftover:pef-callalert. Do not skip-68k."""
    m = _MARKERS["pef-gndalrt"]
    if m in text:
        return text
    old = (
        "			if (stub) {\n"
        "				gpr(3) = g3_pef_host(43u, 0, 0, 0, 0, 0);\n"
        "				pc() = stub;\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    new = (
        "			if (lr == 0x1010adccu || lr == 0x1010ae08u) {\n"
        "				pc() = 0x1010b26cu;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngna;\n"
        "					if (ngna < 8) {\n"
        "						char buf[96];\n"
        "						ngna++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF gndalrt pc=%08x lr=%08x\",\n"
        "							 (unsigned)pc(), (unsigned)lr);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			} else if (stub) {\n"
        "				gpr(3) = g3_pef_host(43u, 0, 0, 0, 0, 0);\n"
        "				pc() = stub;\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gndalrt")
    return _insert_after_enter(text, _log_enter(m, "ngndalrt"), "cpu-pef-gndalrt")


def patch_cpu_pef_gdzero(text: str) -> str:
    """KEEP 17237 gdtbl: ColorTable on GDevice still two bctrl then
    die. leftover:pef-gndalrt REVERT 17238: jump to GetNewDialog
    1010b26c used r3=stub (id=5296 dlg=0) then wait-cmp. Do not
    jump mid-function. Return r3=0 via the blr stub so
    ResizeAndDisplayAlert takes the NULL-device path.
    Do not remill leftover:pef-gndalrt or leftover:pef-gdblr.
    Do not skip-68k."""
    m = _MARKERS["pef-gdzero"]
    if m in text:
        return text
    old = (
        "			if (stub) {\n"
        "				gpr(3) = g3_pef_host(43u, 0, 0, 0, 0, 0);\n"
        "				pc() = stub;\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    new = (
        "			if (stub) {\n"
        "				gpr(3) = 0;\n"
        "				pc() = stub;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngz;\n"
        "					if (ngz < 8) {\n"
        "						ngz++;\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF gdzero r3=0\");\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gdzero")
    return _insert_after_enter(text, _log_enter(m, "ngdzero"), "cpu-pef-gdzero")


def patch_cpu_pef_b0host(text: str) -> str:
    """KEEP 17239: gdzero r3=0 at lr=1010adcc then NEW bctrl
    lr=1010b0a8 (still pa=0x15000), r3=0 again, then 45s 68k hang
    (full hang-cap). 1010b0a8 is 0x1c4 before GetNewDialog 1010b26c.
    leftover:pef-gndalrt REVERT jumped there. Host 1010b0a8: OSType
    → GetResource, else GetNewDialog (plant WIND 128 if dlg=0).
    Do not remill leftover:pef-gdzero or leftover:pef-gndalrt.
    Do not skip-68k."""
    m = _MARKERS["pef-b0host"]
    if m in text:
        return text
    old = (
        "			if (stub) {\n"
        "				gpr(3) = 0;\n"
        "				pc() = stub;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngz;\n"
        "					if (ngz < 8) {\n"
        "						ngz++;\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF gdzero r3=0\");\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    new = (
        "			if (stub) {\n"
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r;\n"
        "					if (a3 > 0x10000u)\n"
        "						r = g3_pef_host(92u, a3, a4, 0, 0, 0);\n"
        "					else {\n"
        "						r = g3_pef_host(134u, a3, a4,\n"
        "								gpr(5), 0, 0);\n"
        "						if (!r)\n"
        "							r = g3_plant_wind(128u);\n"
        "					}\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0;\n"
        "						if (nb0 < 8) {\n"
        "							char buf[96];\n"
        "							nb0++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0host a3=%08x a4=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)a4,\n"
        "								 (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else\n"
        "					gpr(3) = 0;\n"
        "				pc() = stub;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned ngz;\n"
        "					if (ngz < 8) {\n"
        "						ngz++;\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF gdzero r3=0\");\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			} else if (ROMBase)\n"
        "				pc() = ROMBase + 0x366084u;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-b0host")
    return _insert_after_enter(text, _log_enter(m, "nb0host"), "cpu-pef-b0host")


def patch_cpu_pef_b0ptr(text: str) -> str:
    """KEEP 17240: b0host at lr=1010b0a8 saw a3=101914b0 a4=000000ff
    r3=0. a3 is a NewPtr/stub-band pointer, not an OSType; a4=255
    is a byte count. GetResource of that type returned 0. Host as
    NewHandle(a4) (idx 97) plus ParamText (idx 49) on a3.
    Do not remill leftover:pef-b0host or leftover:pef-gndalrt.
    Do not skip-68k."""
    m = _MARKERS["pef-b0ptr"]
    if m in text:
        return text
    old = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r;\n"
        "					if (a3 > 0x10000u)\n"
        "						r = g3_pef_host(92u, a3, a4, 0, 0, 0);\n"
        "					else {\n"
        "						r = g3_pef_host(134u, a3, a4,\n"
        "								gpr(5), 0, 0);\n"
        "						if (!r)\n"
        "							r = g3_plant_wind(128u);\n"
        "					}\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0;\n"
        "						if (nb0 < 8) {\n"
        "							char buf[96];\n"
        "							nb0++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0host a3=%08x a4=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)a4,\n"
        "								 (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    new = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r, n;\n"
        "					n = a4 & 0xffffu;\n"
        "					if (n == 0 || n > 4096u)\n"
        "						n = 256u;\n"
        "					r = g3_pef_host(97u, n, 0, 0, 0, 0);\n"
        "					(void)g3_pef_host(49u, a3, 0, 0, 0, 0);\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0p;\n"
        "						if (nb0p < 8) {\n"
        "							char buf[96];\n"
        "							nb0p++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0ptr a3=%08x a4=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)a4,\n"
        "								 (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-b0ptr")
    return _insert_after_enter(text, _log_enter(m, "nb0ptr"), "cpu-pef-b0ptr")


def patch_cpu_pef_b0ptxt(text: str) -> str:
    """KEEP 17241: b0ptr NewHandle r3=101915d0 plus ParamText at
    lr=1010b0a8, then 45s 68k hang — no GetNewDialog after 520.
    ParamText is a procedure; returning a Handle in r3 likely
    poisons GetNewDialog 1010b26c. Return r3=0 after ParamText
    so that bl can run as CFM gndtv. Do not remill leftover:pef-b0ptr
    or leftover:pef-gndalrt. Do not skip-68k."""
    m = _MARKERS["pef-b0ptxt"]
    if m in text:
        return text
    old = (
        "					n = a4 & 0xffffu;\n"
        "					if (n == 0 || n > 4096u)\n"
        "						n = 256u;\n"
        "					r = g3_pef_host(97u, n, 0, 0, 0, 0);\n"
        "					(void)g3_pef_host(49u, a3, 0, 0, 0, 0);\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0p;\n"
        "						if (nb0p < 8) {\n"
        "							char buf[96];\n"
        "							nb0p++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0ptr a3=%08x a4=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)a4,\n"
        "								 (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    new = (
        "					(void)g3_pef_host(49u, a3, a4, gpr(5),\n"
        "							  gpr(6), 0);\n"
        "					gpr(3) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0t;\n"
        "						if (nb0t < 8) {\n"
        "							char buf[96];\n"
        "							nb0t++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0ptxt a3=%08x a4=%08x r3=0\",\n"
        "								 (unsigned)a3, (unsigned)a4);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-b0ptxt")
    return _insert_after_enter(text, _log_enter(m, "nb0ptxt"), "cpu-pef-b0ptxt")


def patch_cpu_pef_gnd501(text: str) -> str:
    """KEEP 17242: b0ptxt ParamText r3=0 at lr=1010b0a8 then 45s
    68k hang — GetNewDialog 1010b26c still never runs after 520.
    leftover:pef-gndalrt REVERT used garbage id 5296 dlg=0.
    ParamText then r3=501 and resume at 1010b26c; plant ALRT 501
    as WIND 128. Do not remill leftover:pef-gndalrt or
    leftover:pef-b0ptxt. Do not skip-68k."""
    m = _MARKERS["pef-gnd501"]
    if m in text:
        return text
    old = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r, n;\n"
        "					(void)g3_pef_host(49u, a3, a4, gpr(5),\n"
        "							  gpr(6), 0);\n"
        "					gpr(3) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0t;\n"
        "						if (nb0t < 8) {\n"
        "							char buf[96];\n"
        "							nb0t++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0ptxt a3=%08x a4=%08x r3=0\",\n"
        "								 (unsigned)a3, (unsigned)a4);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else\n"
        "					gpr(3) = 0;\n"
        "				pc() = stub;\n"
    )
    new = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4);\n"
        "					(void)g3_pef_host(49u, a3, a4, gpr(5),\n"
        "							  gpr(6), 0);\n"
        "					gpr(3) = 501u;\n"
        "					pc() = 0x1010b26cu;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned ng501;\n"
        "						if (ng501 < 8) {\n"
        "							char buf[96];\n"
        "							ng501++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF gnd501 a3=%08x pc=%08x\",\n"
        "								 (unsigned)a3, (unsigned)pc());\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else {\n"
        "					gpr(3) = 0;\n"
        "					pc() = stub;\n"
        "				}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gnd501")
    old = (
        "		} else if ((a3 & 0xffffu) == 519u) {\n"
        "			/* 519 success skipped Splash 510. Fail it. */\n"
        "			r3 = 0;\n"
    )
    new = (
        "		} else if ((a3 & 0xffffu) == 501u) {\n"
        "			r3 = g3_plant_wind(128u);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nd501;\n"
        "				if (nd501 < 8) {\n"
        "					char buf[96];\n"
        "					nd501++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id=501 w=%08x\",\n"
        "						 (unsigned)r3);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		} else if ((a3 & 0xffffu) == 519u) {\n"
        "			/* 519 success skipped Splash 510. Fail it. */\n"
        "			r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gnd501-id")
    return _insert_after_enter(text, _log_enter(m, "ngnd501"), "cpu-pef-gnd501")


def patch_cpu_pef_b0alrt(text: str) -> str:
    """KEEP 17242 b0ptxt: ParamText r3=0 then 68k hang, no GetNewDialog.
    leftover:pef-gnd501 REVERT 17243: GetNewDialog 501 dlg=1004e000
    ModalDialog item=1 DisposeDialog then wait-cmp r1=0 — jump to
    1010b26c smashed the stack and disposed Welcome. Host Alert
    idx 125 at 1010b0a8 (r3=item 1) via the blr stub. Do not remill
    leftover:pef-gnd501, leftover:pef-gndalrt, leftover:pef-b0ptxt.
    Do not skip-68k."""
    m = _MARKERS["pef-b0alrt"]
    if m in text:
        return text
    old = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r, n;\n"
        "					(void)g3_pef_host(49u, a3, a4, gpr(5),\n"
        "							  gpr(6), 0);\n"
        "					gpr(3) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nb0t;\n"
        "						if (nb0t < 8) {\n"
        "							char buf[96];\n"
        "							nb0t++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0ptxt a3=%08x a4=%08x r3=0\",\n"
        "								 (unsigned)a3, (unsigned)a4);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else\n"
        "					gpr(3) = 0;\n"
        "				pc() = stub;\n"
    )
    new = (
        "				if (lr == 0x1010b0a8u) {\n"
        "					uint32 a3 = gpr(3), a4 = gpr(4), r;\n"
        "					(void)g3_pef_host(49u, a3, a4, gpr(5),\n"
        "							  gpr(6), 0);\n"
        "					r = g3_pef_host(125u, 501u, 0, 0, 0, 0);\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nba;\n"
        "						if (nba < 8) {\n"
        "							char buf[96];\n"
        "							nba++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0alrt a3=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else\n"
        "					gpr(3) = 0;\n"
        "				pc() = stub;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-b0alrt")
    return _insert_after_enter(text, _log_enter(m, "nb0alrt"), "cpu-pef-b0alrt")


def patch_cpu_pef_welmd(text: str) -> str:
    """KEEP 17244: b0alrt Alert id=0x1f5 (501) r3=1 at lr=1010b0a8
    then 45s 68k hang — CUP did not ModalDialog Welcome 3500.
    leftover:pef-gnd501 REVERT disposed WIND 128. After Alert item 1,
    host ModalDialog idx 53 on Welcome 1004e000. Do not remill
    leftover:pef-b0alrt, leftover:pef-gnd501, leftover:pef-welloop.
    Do not skip-68k."""
    m = _MARKERS["pef-welmd"]
    if m in text:
        return text
    old = (
        "					r = g3_pef_host(125u, 501u, 0, 0, 0, 0);\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nba;\n"
        "						if (nba < 8) {\n"
        "							char buf[96];\n"
        "							nba++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0alrt a3=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    new = (
        "					r = g3_pef_host(125u, 501u, 0, 0, 0, 0);\n"
        "					{\n"
        "						extern uint32 RAMBase;\n"
        "						uint32 hit = g3_pef_newptr(4u);\n"
        "						if (hit && g3_ea_data(hit + 1u))\n"
        "							vm_write_memory_2(hit, 0);\n"
        "						(void)g3_pef_host(53u, RAMBase + 0x4e000u,\n"
        "								 hit, 0, 0, 0);\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nwm;\n"
        "							if (nwm < 8) {\n"
        "								char buf[96];\n"
        "								nwm++;\n"
        "								snprintf(buf, sizeof(buf),\n"
        "									 \"G3: 68k Launch A9F2 CFM Upgrader PEF welmd dlg=%08x hit=%08x\",\n"
        "									 (unsigned)(RAMBase + 0x4e000u),\n"
        "									 (unsigned)hit);\n"
        "								nw_boot_log(buf);\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					}\n"
        "					gpr(3) = r;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nba;\n"
        "						if (nba < 8) {\n"
        "							char buf[96];\n"
        "							nba++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF b0alrt a3=%08x r3=%08x\",\n"
        "								 (unsigned)a3, (unsigned)r);\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-welmd")
    return _insert_after_enter(text, _log_enter(m, "nwelmd"), "cpu-pef-welmd")


def patch_cpu_pef_skipditl(text: str) -> str:
    """Skip Image#1 host STR#/DITL splash; let guest 32-bit QD paint CWindow."""
    m = _MARKERS["pef-skipditl"]
    if m in text:
        return text
    old_dd = (
        "		if (g3_did_welpef) {\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			g3_draw_ditl3500();\n"
        "			video_set_dirty_area(0, 80, 640, 298);\n"
        "			/* leftover:pef-novid: CPU-thread present crashes. */\n"
    )
    new_dd = (
        "		if (g3_did_welpef) {\n"
        "			/* leftover:pef-skipditl: no host DITL3500; QD paints CWindow. */\n"
        "			video_set_dirty_area(0, 0, 640, 480);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nskipditl;\n"
        "				if (nskipditl < 8) {\n"
        "					nskipditl++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF skipditl DrawDialog\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "			/* leftover:pef-novid: CPU-thread present crashes. */\n"
    )
    text = _replace_once(text, old_dd, new_dd, "cpu-pef-skipditl-dd")
    old_md = (
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			g3_draw_ditl3500();\n"
        "			if (w && g3_ea_data(w + 10u))\n"
    )
    new_md = (
        "			/* leftover:pef-skipditl: mdloop skips host DITL. */\n"
        "			video_set_dirty_area(0, 0, 640, 480);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nskipmd;\n"
        "				if (nskipmd < 8) {\n"
        "					nskipmd++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF skipditl mdloop\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "			if (w && g3_ea_data(w + 10u))\n"
    )
    text = _replace_once(text, old_md, new_md, "cpu-pef-skipditl-md")
    old_log = (
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF DrawDialog ditl3500\");\n"
    )
    new_log = (
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF skipditl (was ditl3500)\");\n"
    )
    text = _replace_once(text, old_log, new_log, "cpu-pef-skipditl-log")
    return _insert_after_enter(text, _log_enter(m, "nskipditl"), "cpu-pef-skipditl")


def patch_cpu_pef_wplug802(text: str) -> str:
    """Plant Welcome plugin when Upgrader would return err 0x802 (2050).
    wppr 3500 + g3_pef_load_welcome + plugin rec; TEDevice + FSpOpenDF.
    PC hooks at plugin lookup nil. After leftover:pef-skipditl."""
    m = _MARKERS["pef-wplug802"]
    if m in text:
        return text
    old_fn = (
        "	g3_did_welpef = tv;\n"
        "	return tv;\n"
        "}\n"
        "/* GetDCtlEntry must return a Handle to a DCE whose\n"
    )
    new_fn = (
        "	g3_did_welpef = tv;\n"
        "	return tv;\n"
        "}\n"
        "static uint32 g3_wplug_node;\n"
        "static uint32 g3_wplug_dev;\n"
        "static unsigned g3_wplug_dev_pass;\n"
        "static uint32 g3_wplug_make(void)\n"
        "{\n"
        "	uint32 wel, p;\n"
        "	if (g3_wplug_node)\n"
        "		return g3_wplug_node;\n"
        "	wel = g3_pef_load_welcome();\n"
        "	if (!wel)\n"
        "		return 0;\n"
        "	p = g3_pef_newptr(24u);\n"
        "	if (!p || !g3_ea_data(p + 0x17u))\n"
        "		return 0;\n"
        "	vm_write_memory_4(p, wel);\n"
        "	vm_write_memory_4(p + 6u, wel);\n"
        "	vm_write_memory_4(p + 0xau, 0x10242000u);\n"
        "	vm_write_memory_4(p + 0x10u, wel);\n"
        "	vm_write_memory_4(p + 0x14u, 0);\n"
        "	g3_wplug_node = p;\n"
        "	return p;\n"
        "}\n"
        "/* GetDCtlEntry must return a Handle to a DCE whose\n"
    )
    text = _replace_once(text, old_fn, new_fn, "cpu-pef-wplug802-fn")
    old_gr = (
        "		} else if (a3 == 0x53545223u && rid == 3500) {\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "				r3 = g3_res_plant(doff, ln);\n"
        "		} else if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
    )
    new_gr = (
        "		} else if (a3 == 0x77707072u && rid == 3500) {\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "				r3 = g3_res_plant(doff, ln);\n"
        "			(void)g3_pef_load_welcome();\n"
        "		} else if (a3 == 0x53545223u && rid == 3500) {\n"
        "			g3_res_src_off = g3_doc_rf_off;\n"
        "			if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
        "				r3 = g3_res_plant(doff, ln);\n"
        "		} else if (g3_res_lookup(a3, rid, &doff, &ln) && ln)\n"
    )
    text = _replace_once(text, old_gr, new_gr, "cpu-pef-wplug802-wppr")
    old_fsp = (
        "	if (idx == 60u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenDF\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    new_fsp = (
        "	if (idx == 60u) {\n"
        "		r3 = g3_pef_load_welcome() ? 6u : 0u;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF FSpOpenDF r3=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    text = _replace_once(text, old_fsp, new_fsp, "cpu-pef-wplug802-fsp")
    old_gdl = (
        "	if (idx == 118u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetDeviceList\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    new_gdl = (
        "	if (idx == 118u) {\n"
        "		if (!g3_wplug_dev) {\n"
        "			g3_wplug_dev = g3_pef_newptr(16u);\n"
        "			if (g3_wplug_dev && g3_ea_data(g3_wplug_dev + 7u))\n"
        "				vm_write_memory_1(g3_wplug_dev + 4u, 1);\n"
        "		}\n"
        "		g3_wplug_dev_pass = 0;\n"
        "		r3 = g3_wplug_dev;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetDeviceList r3=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    text = _replace_once(text, old_gdl, new_gdl, "cpu-pef-wplug802-gdl")
    old_gnd = (
        "	if (idx == 57u) {\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				n++;\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF GetNextDevice\");\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    new_gnd = (
        "	if (idx == 57u) {\n"
        "		if (g3_wplug_dev && g3_wplug_dev_pass == 0) {\n"
        "			g3_wplug_dev_pass = 1;\n"
        "			r3 = g3_wplug_dev;\n"
        "		} else\n"
        "			r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned n;\n"
        "			if (n < 8) {\n"
        "				char buf[96];\n"
        "				n++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNextDevice r3=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		return r3;\n"
        "	}\n"
    )
    text = _replace_once(text, old_gnd, new_gnd, "cpu-pef-wplug802-gnd")
    old_pc = (
        "			if (g3_did_welpef &&\n"
        "			    pc() >= 0x10240200u && pc() < 0x10241200u &&\n"
        "			    gpr(2) != 0x10242000u) {\n"
        "				gpr(2) = 0x10242000u;\n"
    )
    new_pc = (
        "			if (g3_did_pef_enter &&\n"
        "			    (pc() == 0x1010D5F8u || pc() == 0x1010FC70u) &&\n"
        "			    gpr(31) == 0) {\n"
        "				uint32 rec = g3_wplug_make();\n"
        "				if (rec)\n"
        "					gpr(31) = rec;\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static unsigned nw802;\n"
        "					if (nw802 < 8) {\n"
        "						char buf[96];\n"
        "						nw802++;\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k Launch A9F2 CFM Upgrader PEF wplug802 pc=%08x rec=%08x\",\n"
        "							 (unsigned)pc(), (unsigned)rec);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "			}\n"
        "			if (g3_did_welpef &&\n"
        "			    pc() >= 0x10240200u && pc() < 0x10241200u &&\n"
        "			    gpr(2) != 0x10242000u) {\n"
        "				gpr(2) = 0x10242000u;\n"
    )
    text = _replace_once(text, old_pc, new_pc, "cpu-pef-wplug802-pc")
    return _insert_after_enter(text, _log_enter(m, "nwplug802"), "cpu-pef-wplug802")


def patch_cpu_pef_mdloop(text: str) -> str:
    """KEEP 17245: welmd hosted ModalDialog idx 53 once on Welcome
    1004e000 hit=101914d0 item=1 then 45s 68k hang. idx 53 is a
    one-shot write of item 1; CUP never loops. DrawDialog+ShowWindow
    on 1004e000 and run three filter-proc iterations. Do not jump
    to 1024098c (leftover:pef-welloop REVERT). Do not remill
    leftover:pef-welmd or leftover:pef-gnd501. Do not skip-68k."""
    m = _MARKERS["pef-mdloop"]
    if m in text:
        return text
    old = (
        "	} else if (idx == 53u) {\n"
        "		/* ModalDialog(filter, itemHit). DITL 519 #1 OK. */\n"
        "		if (a4 && g3_ea_data(a4 + 1u))\n"
        "			vm_write_memory_2(a4, 1);\n"
        "		r3 = 0;\n"
    )
    new = (
        "	} else if (idx == 53u) {\n"
        "		/* leftover:pef-mdloop: Welcome 3500 DrawDialog then item 1. */\n"
        "		extern uint32 RAMBase;\n"
        "		if (g3_did_welpef) {\n"
        "			uint32 w = a3 ? a3 : (RAMBase + 0x4e000u);\n"
        "			g3_pict_rid = 3500;\n"
        "			g3_pict1000_blit();\n"
        "			g3_draw_ditl3500();\n"
        "			if (w && g3_ea_data(w + 10u))\n"
        "				vm_write_memory_1(w + 10u, 1);\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nmdl;\n"
        "				if (nmdl < 8) {\n"
        "					char buf[96];\n"
        "					nmdl++;\n"
        "					snprintf(buf, sizeof(buf),\n"
        "						 \"G3: 68k Launch A9F2 CFM Upgrader PEF mdloop w=%08x\",\n"
        "						 (unsigned)w);\n"
        "					nw_boot_log(buf);\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "		if (a4 && g3_ea_data(a4 + 1u))\n"
        "			vm_write_memory_2(a4, 1);\n"
        "		r3 = 0;\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mdloop")
    old = (
        "						(void)g3_pef_host(53u, RAMBase + 0x4e000u,\n"
        "								 hit, 0, 0, 0);\n"
    )
    new = (
        "						{\n"
        "							int i;\n"
        "							for (i = 0; i < 3; i++)\n"
        "								(void)g3_pef_host(53u,\n"
        "									RAMBase + 0x4e000u,\n"
        "									hit, 0, 0, 0);\n"
        "						}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mdloop-n")
    return _insert_after_enter(text, _log_enter(m, "nmdloop"), "cpu-pef-mdloop")


def patch_cpu_pef_mdret(text: str) -> str:
    """KEEP 17246: mdloop DrawDialog+ModalDialog item=1 three times
    on Welcome 1004e000 then 45s 68k hang. CUP never sees Continue
    because we blr to 1010b0a8 (mid ResizeAndDisplayAlert).
    leftover:pef-gnd501 REVERT jumped to GetNewDialog and
    DisposeDialog smashed r1. Resume at skipTE landing 1010b534
    with r3=1 so Alert returns. Do not remill leftover:pef-mdloop
    or leftover:pef-gnd501. Do not skip-68k."""
    m = _MARKERS["pef-mdret"]
    if m in text:
        return text
    old = (
        "				} else\n"
        "					gpr(3) = 0;\n"
        "				pc() = stub;\n"
    )
    new = (
        "					pc() = 0x1010b534u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nmr;\n"
        "						if (nmr < 8) {\n"
        "							char buf[96];\n"
        "							nmr++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF mdret pc=%08x r3=%08x\",\n"
        "								 (unsigned)pc(), (unsigned)gpr(3));\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "				} else {\n"
        "					gpr(3) = 0;\n"
        "					pc() = stub;\n"
        "				}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mdret")
    return _insert_after_enter(text, _log_enter(m, "nmdret"), "cpu-pef-mdret")


def patch_cpu_pef_mdtoc(text: str) -> str:
    """KEEP 17247: mdret pc=1010b534 r3=1 then DSI DAR=e05e5098
    SRR0=1011393c (CFM glue, bad TOC) and 68k A991 itemHit=0.
    Restore Upgrader TOC r2=10115000 before the skipTE landing.
    Do not remill leftover:pef-mdret or leftover:pef-gnd501.
    Do not skip-68k (not 0x5c86c)."""
    m = _MARKERS["pef-mdtoc"]
    if m in text:
        return text
    old = (
        "					pc() = 0x1010b534u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nmr;\n"
        "						if (nmr < 8) {\n"
        "							char buf[96];\n"
        "							nmr++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF mdret pc=%08x r3=%08x\",\n"
        "								 (unsigned)pc(), (unsigned)gpr(3));\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    new = (
        "					gpr(2) = 0x10115000u;\n"
        "					pc() = 0x1010b534u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nmt;\n"
        "						if (nmt < 8) {\n"
        "							char buf[96];\n"
        "							nmt++;\n"
        "							snprintf(buf, sizeof(buf),\n"
        "								 \"G3: 68k Launch A9F2 CFM Upgrader PEF mdtoc r2=%08x pc=%08x\",\n"
        "								 (unsigned)gpr(2), (unsigned)pc());\n"
        "							nw_boot_log(buf);\n"
        "						}\n"
        "					}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mdtoc")
    return _insert_after_enter(text, _log_enter(m, "nmdtoc"), "cpu-pef-mdtoc")


def patch_cpu_pef_a991c(text: str) -> str:
    """KEEP 17248: mdtoc r2=10115000 then TextFont/TextSize and DSI
    DAR=6e637329 SRR0=101134d4; 68k A991 itemHit=0 retries (live
    ModalDialog). Writing 1 used to DisposeDialog splash; Welcome
    DITL 3500 item 1 is Continue. Write itemHit=1 when welpef is
    planted. Do not skip 0x5c86c-0x5c8c0. Do not remill
    leftover:pef-mdtoc. Do not skip-68k."""
    m = _MARKERS["pef-a991c"]
    if m in text:
        return text
    old = (
        "							/* itemHit=0 keeps ModalDialog BNE\n"
        "							 * retry. Writing 1 was DisposeDialog. */\n"
        "							if (g3_ea_data(ptr) &&\n"
        "							    ptr >= 0x20000u)\n"
        "								vm_write_memory_2(ptr, 0);\n"
    )
    new = (
        "							/* leftover:pef-a991c: Welcome Continue. */\n"
        "							if (g3_ea_data(ptr) &&\n"
        "							    ptr >= 0x20000u)\n"
        "								vm_write_memory_2(ptr,\n"
        "									g3_did_welpef ? 1 : 0);\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned na1c;\n"
        "								if (na1c < 8) {\n"
        "									char buf[96];\n"
        "									na1c++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF a991c hit=%u\",\n"
        "										 g3_did_welpef ? 1u : 0u);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-a991c")
    return _insert_after_enter(text, _log_enter(m, "na991c"), "cpu-pef-a991c")


def patch_cpu_pef_srl(text: str) -> str:
    """KEEP 17249: a991c hit=1 then 68k A991 retries, DisposeDialog
    A983 (already pop-only), then unhosted SetResLoad A99B at
    5005c8e2 (past 0x5c8c0). Host A99B Pascal pop 2. Do not skip
    0x5c86c-0x5c8c0. Do not remill leftover:pef-a991c.
    Do not skip-68k."""
    m = _MARKERS["pef-srl"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa98bu) {\n"
        "						/* ParamText(s0..s3). Pascal pop 16.\n"
        "						 * Site 0x3e1b4 then A991. */\n"
    )
    new = (
        "					} else if (op68 == 0xa99bu) {\n"
        "						/* leftover:pef-srl: SetResLoad. Pascal pop 2. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 2u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsrl;\n"
        "							if (nsrl < 8) {\n"
        "								nsrl++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF srl A99B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa98bu) {\n"
        "						/* ParamText(s0..s3). Pascal pop 16.\n"
        "						 * Site 0x3e1b4 then A991. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-srl")
    return _insert_after_enter(text, _log_enter(m, "nsrl"), "cpu-pef-srl")


def patch_cpu_pef_hunl(text: str) -> str:
    """KEEP 17250: srl A99B then 68k A02A HUnlock at 5005c996
    (catch-all only gpr(8)=0, no Pascal pop). Host HUnlock pop 4.
    Do not skip 0x5c86c-0x5c8c0. Do not remill leftover:pef-srl.
    Do not skip-68k."""
    m = _MARKERS["pef-hunl"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa98bu) {\n"
        "						/* ParamText(s0..s3). Pascal pop 16.\n"
        "						 * Site 0x3e1b4 then A991. */\n"
    )
    new = (
        "					} else if (op68 == 0xa02au) {\n"
        "						/* leftover:pef-hunl: HUnlock. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhu;\n"
        "							if (nhu < 8) {\n"
        "								nhu++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hunl A02A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa98bu) {\n"
        "						/* ParamText(s0..s3). Pascal pop 16.\n"
        "						 * Site 0x3e1b4 then A991. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hunl")
    return _insert_after_enter(text, _log_enter(m, "nhunl"), "cpu-pef-hunl")


def patch_cpu_pef_sutil(text: str) -> str:
    """KEEP 17251: hunl A02A then 68k A8B5 ScriptUtil at 5005ca1e,
    then CountMItems A950 / CountResources A99C / CalcMenuSize A948.
    Host ScriptUtil Pascal pop 4. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-hunl. Do not skip-68k."""
    m = _MARKERS["pef-sutil"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa02au) {\n"
        "						/* leftover:pef-hunl: HUnlock. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8b5u) {\n"
        "						/* leftover:pef-sutil: ScriptUtil. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsu;\n"
        "							if (nsu < 8) {\n"
        "								nsu++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF sutil A8B5\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa02au) {\n"
        "						/* leftover:pef-hunl: HUnlock. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sutil")
    return _insert_after_enter(text, _log_enter(m, "nsutil"), "cpu-pef-sutil")


def patch_cpu_pef_cntmi(text: str) -> str:
    """KEEP 17252: sutil A8B5 then 68k A950 CountMItems at 5005ca54,
    then CountResources A99C / CalcMenuSize A948 / MenuDispatch A825.
    Host CountMItems Pascal pop 4, D0=0. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-sutil. Do not skip-68k."""
    m = _MARKERS["pef-cntmi"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa8b5u) {\n"
        "						/* leftover:pef-sutil: ScriptUtil. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa950u) {\n"
        "						/* leftover:pef-cntmi: CountMItems. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncm;\n"
        "							if (ncm < 8) {\n"
        "								ncm++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF cntmi A950\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa8b5u) {\n"
        "						/* leftover:pef-sutil: ScriptUtil. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cntmi")
    return _insert_after_enter(text, _log_enter(m, "ncntmi"), "cpu-pef-cntmi")


def patch_cpu_pef_cntres(text: str) -> str:
    """KEEP 17253: cntmi A950 then SetResLoad A99B and unhosted
    CountResources A99C at 5005ca86, then CalcMenuSize A948.
    Host CountResources Pascal pop 4, D0=0. Do not skip
    0x5c86c-0x5c8c0. Do not remill leftover:pef-cntmi.
    Do not skip-68k."""
    m = _MARKERS["pef-cntres"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa950u) {\n"
        "						/* leftover:pef-cntmi: CountMItems. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa99cu) {\n"
        "						/* leftover:pef-cntres: CountResources. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncr;\n"
        "							if (ncr < 8) {\n"
        "								ncr++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF cntres A99C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa950u) {\n"
        "						/* leftover:pef-cntmi: CountMItems. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cntres")
    return _insert_after_enter(text, _log_enter(m, "ncntres"), "cpu-pef-cntres")


def patch_cpu_pef_cmsz(text: str) -> str:
    """KEEP 17254: cntres then 68k A948 CalcMenuSize at 5005cba6
    (after CountResources map) and MenuDispatch A825. Host
    CalcMenuSize Pascal pop 4. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-cntres. Do not skip-68k."""
    m = _MARKERS["pef-cmsz"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa99cu) {\n"
        "						/* leftover:pef-cntres: CountResources. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa948u) {\n"
        "						/* leftover:pef-cmsz: CalcMenuSize. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncs;\n"
        "							if (ncs < 8) {\n"
        "								ncs++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF cmsz A948\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa99cu) {\n"
        "						/* leftover:pef-cntres: CountResources. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cmsz")
    return _insert_after_enter(text, _log_enter(m, "ncmsz"), "cpu-pef-cmsz")


def patch_cpu_pef_mnud(text: str) -> str:
    """KEEP 17255: cmsz A948 eight times then 68k MenuDispatch
    A825 at 5005cbe6 / 5005cc18. Host like DisplayDispatch:
    selector in D0, Pascal pop 4, D0=0. Do not skip
    0x5c86c-0x5c8c0. Do not remill leftover:pef-cmsz.
    Do not skip-68k."""
    m = _MARKERS["pef-mnud"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa948u) {\n"
        "						/* leftover:pef-cmsz: CalcMenuSize. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa825u) {\n"
        "						/* leftover:pef-mnud: MenuDispatch. Selector D0. */\n"
        "						{\n"
        "							const unsigned sel =\n"
        "								(unsigned)(gpr(8) & 0xffffu);\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nmd;\n"
        "								if (nmd < 8) {\n"
        "									char buf[96];\n"
        "									nmd++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF mnud A825 sel=%u\",\n"
        "										 sel);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xa948u) {\n"
        "						/* leftover:pef-cmsz: CalcMenuSize. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mnud")
    return _insert_after_enter(text, _log_enter(m, "nmnud"), "cpu-pef-mnud")


def patch_cpu_pef_nhc(text: str) -> str:
    """KEEP 17256: mnud A825 sel=1537 eight times then RTS and
    68k NewHandleClear A322 at 5005cc76. Host NewHandleClear:
    Pascal pop 4, A0=handle, D0=0. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-mnud. Do not skip-68k."""
    m = _MARKERS["pef-nhc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa825u) {\n"
        "						/* leftover:pef-mnud: MenuDispatch. Selector D0. */\n"
    )
    new = (
        "					} else if (op68 == 0xa322u) {\n"
        "						/* leftover:pef-nhc: NewHandleClear. Pascal pop 4. */\n"
        "						{\n"
        "							uint32 n = 0, p = 0, h = 0;\n"
        "							if (g3_ea_data(gpr(1) + 3u))\n"
        "								n = vm_read_memory_4(gpr(1));\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							if (n == 0 || n > 0x100000u)\n"
        "								n = 16u;\n"
        "							p = g3_pef_newptr(n);\n"
        "							h = g3_pef_newptr(8u);\n"
        "							if (h && g3_ea_data(h + 3u))\n"
        "								vm_write_memory_4(h, p);\n"
        "							gpr(16) = h;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nnh;\n"
        "								if (nnh < 8) {\n"
        "									char buf[96];\n"
        "									nnh++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF nhc n=%u h=%08x\",\n"
        "										 (unsigned)n, (unsigned)h);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xa825u) {\n"
        "						/* leftover:pef-mnud: MenuDispatch. Selector D0. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-nhc")
    return _insert_after_enter(text, _log_enter(m, "nnhc"), "cpu-pef-nhc")


def patch_cpu_pef_a029(text: str) -> str:
    """KEEP 17257: nhc enter-only; A322 NewHandleClear was mapped
    at 5005cc76 not hosted. Next is 68k HLock A029 at 5005cc7a
    (catch-all, no pop). leftover:pef-hlock is the PEF idx 277 mill.
    Host A029 Pascal pop 4. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-nhc, leftover:pef-hunl, leftover:pef-hlock.
    Do not skip-68k."""
    m = _MARKERS["pef-a029"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa02au) {\n"
        "						/* leftover:pef-hunl: HUnlock. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa029u) {\n"
        "						/* leftover:pef-a029: 68k HLock. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned na029;\n"
        "							if (na029 < 8) {\n"
        "								na029++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF a029 A029\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa02au) {\n"
        "						/* leftover:pef-hunl: HUnlock. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-a029")
    return _insert_after_enter(text, _log_enter(m, "na029"), "cpu-pef-a029")


def patch_cpu_pef_qdext(text: str) -> str:
    """KEEP 17258: a029 HLock eight times. Next unhosted trap is
    QDExtensions AB1D (21x after hunl). Host like MenuDispatch:
    selector in D0, Pascal pop 4, D0=0. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-a029. Do not skip-68k."""
    m = _MARKERS["pef-qdext"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa825u) {\n"
        "						/* leftover:pef-mnud: MenuDispatch. Selector D0. */\n"
    )
    new = (
        "					} else if (op68 == 0xab1du) {\n"
        "						/* leftover:pef-qdext: QDExtensions. Selector D0. */\n"
        "						{\n"
        "							const unsigned sel =\n"
        "								(unsigned)(gpr(8) & 0xffffu);\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nqd;\n"
        "								if (nqd < 8) {\n"
        "									char buf[96];\n"
        "									nqd++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF qdext AB1D sel=%u\",\n"
        "										 sel);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xa825u) {\n"
        "						/* leftover:pef-mnud: MenuDispatch. Selector D0. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-qdext")
    return _insert_after_enter(text, _log_enter(m, "nqdext"), "cpu-pef-qdext")


def patch_cpu_pef_wmgp(text: str) -> str:
    """KEEP 17259: qdext AB1D sel=9/5/6/21 then 68k GetWMgrPort
    A910 at 50051938. Write Welcome CGrafPort 1004e000 to the VAR
    GrafPtr. Do not skip 0x5c86c-0x5c8c0. Do not remill
    leftover:pef-qdext or leftover:pef-getport. Do not skip-68k."""
    m = _MARKERS["pef-wmgp"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xab1du) {\n"
        "						/* leftover:pef-qdext: QDExtensions. Selector D0. */\n"
    )
    new = (
        "					} else if (op68 == 0xa910u) {\n"
        "						/* leftover:pef-wmgp: GetWMgrPort. Pascal VAR GrafPtr. */\n"
        "						{\n"
        "							extern uint32 RAMBase;\n"
        "							uint32 dst = 0;\n"
        "							if (g3_ea_data(gpr(1) + 3u))\n"
        "								dst = vm_read_memory_4(gpr(1));\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							if (dst && g3_ea_data(dst + 3u))\n"
        "								vm_write_memory_4(dst,\n"
        "										 RAMBase + 0x4e000u);\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nwm;\n"
        "								if (nwm < 8) {\n"
        "									char buf[96];\n"
        "									nwm++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF wmgp d=%08x\",\n"
        "										 (unsigned)dst);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xab1du) {\n"
        "						/* leftover:pef-qdext: QDExtensions. Selector D0. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-wmgp")
    return _insert_after_enter(text, _log_enter(m, "nwmgp"), "cpu-pef-wmgp")


def patch_cpu_pef_setc(text: str) -> str:
    """KEEP 17260: wmgp GetWMgrPort wrote Welcome 1004e000 then
    68k SetClip A879 at 500519e4 (already pop-4). leftover:pef-setclip
    is PEF idx 50. Log the RgnHandle. Do not skip 0x5c86c-0x5c8c0.
    Do not remill leftover:pef-wmgp or leftover:pef-setclip.
    Do not skip-68k."""
    m = _MARKERS["pef-setc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* SetClip(rgn). Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetc;\n"
        "							if (nsetc < 8) {\n"
        "								nsetc++;\n"
        "								nw_boot_log(\"G3: 68k SetClip A879\");\n"
    )
    new = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
        "						{\n"
        "							uint32 rgn = 0;\n"
        "							if (g3_ea_data(gpr(1) + 3u))\n"
        "								rgn = vm_read_memory_4(gpr(1));\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nsetc;\n"
        "								if (nsetc < 8) {\n"
        "									char buf[96];\n"
        "									nsetc++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF setc rgn=%08x\",\n"
        "										 (unsigned)rgn);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetc0;\n"
        "							if (nsetc0 < 8) {\n"
        "								nsetc0++;\n"
        "								nw_boot_log(\"G3: 68k SetClip A879\");\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setc")
    return _insert_after_enter(text, _log_enter(m, "nsetc"), "cpu-pef-setc")


def patch_cpu_pef_fillc(text: str) -> str:
    """KEEP 17261: setc rgn=0 then 68k FillCRgn AA12 at 500541aa.
    Host FillCRgn(rgn, pixPat) Pascal pop 8. Do not skip
    0x5c86c-0x5c8c0. Do not remill leftover:pef-setc.
    Do not skip-68k."""
    m = _MARKERS["pef-fillc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa12u) {\n"
        "						/* leftover:pef-fillc: FillCRgn. Pascal pop 8. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 8u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nfc;\n"
        "							if (nfc < 8) {\n"
        "								nfc++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF fillc AA12\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-fillc")
    return _insert_after_enter(text, _log_enter(m, "nfillc"), "cpu-pef-fillc")


def patch_cpu_pef_layer(text: str) -> str:
    """KEEP 17262: fillc AA12 then 68k LayerDispatch A829 at
    50051fa8. Host like MenuDispatch: selector D0, Pascal pop 4.
    Do not skip 0x5c86c-0x5c8c0. Do not remill leftover:pef-fillc.
    Do not skip-68k."""
    m = _MARKERS["pef-layer"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xaa12u) {\n"
        "						/* leftover:pef-fillc: FillCRgn. Pascal pop 8. */\n"
    )
    new = (
        "					} else if (op68 == 0xa829u) {\n"
        "						/* leftover:pef-layer: LayerDispatch. Selector D0. */\n"
        "						{\n"
        "							const unsigned sel =\n"
        "								(unsigned)(gpr(8) & 0xffffu);\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nly;\n"
        "								if (nly < 8) {\n"
        "									char buf[96];\n"
        "									nly++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF layer A829 sel=%u\",\n"
        "										 sel);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xaa12u) {\n"
        "						/* leftover:pef-fillc: FillCRgn. Pascal pop 8. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-layer")
    return _insert_after_enter(text, _log_enter(m, "nlayer"), "cpu-pef-layer")


def patch_cpu_pef_a8e0(text: str) -> str:
    """Auto mill 68k OffsetRgn 0xA8E0 Pascal pop 8.
    Kind leftover:pef-a8e0 — mill_pef_next leftover:pef-offrgn is splash idx 183.
    """
    m = _MARKERS["pef-a8e0"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8e0u) {\n"
        "						/* leftover:pef-a8e0: OffsetRgn. Pascal pop 8. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 8u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned na8e0;\n"
        "							if (na8e0 < 8) {\n"
        "								na8e0++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF a8e0 0xA8E0\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-a8e0")
    return _insert_after_enter(text, _log_enter(m, "na8e0"), "cpu-pef-a8e0")


def patch_cpu_pef_unionrgn(text: str) -> str:
    """Auto mill 68k UnionRgn 0xA8E5 Pascal pop 12."""
    m = _MARKERS["pef-unionrgn"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8e5u) {\n"
        "						/* leftover:pef-unionrgn: UnionRgn. Pascal pop 12. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 12u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nunionrgn;\n"
        "							if (nunionrgn < 8) {\n"
        "								nunionrgn++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF unionrgn 0xA8E5\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-unionrgn")
    return _insert_after_enter(text, _log_enter(m, "nunionrgn"), "cpu-pef-unionrgn")


def patch_cpu_pef_aa8d8(text: str) -> str:
    """Auto mill 68k NewRgn 0xA8D8 Pascal pop 0."""
    m = _MARKERS["pef-aa8d8"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8d8u) {\n"
        "						/* leftover:pef-aa8d8: NewRgn. Pascal pop 0. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 0u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa8d8;\n"
        "							if (naa8d8 < 8) {\n"
        "								naa8d8++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa8d8 0xA8D8\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa8d8")
    return _insert_after_enter(text, _log_enter(m, "naa8d8"), "cpu-pef-aa8d8")


def patch_cpu_pef_open(text: str) -> str:
    """Auto mill 68k Open 0xA000 Pascal pop 4."""
    m = _MARKERS["pef-open"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa000u) {\n"
        "						/* leftover:pef-open: Open. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nopen;\n"
        "							if (nopen < 8) {\n"
        "								nopen++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF open 0xA000\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-open")
    return _insert_after_enter(text, _log_enter(m, "nopen"), "cpu-pef-open")


def patch_cpu_pef_readxpram(text: str) -> str:
    """Auto mill 68k ReadXPRam 0xA051 Pascal pop 4."""
    m = _MARKERS["pef-readxpram"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa051u) {\n"
        "						/* leftover:pef-readxpram: ReadXPRam. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nreadxpra;\n"
        "							if (nreadxpra < 8) {\n"
        "								nreadxpra++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF readxpram 0xA051\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-readxpram")
    return _insert_after_enter(text, _log_enter(m, "nreadxpra"), "cpu-pef-readxpram")


def patch_cpu_pef_disposeptr(text: str) -> str:
    """Auto mill 68k DisposePtr 0xA01F Pascal pop 4."""
    m = _MARKERS["pef-disposeptr"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa01fu) {\n"
        "						/* leftover:pef-disposeptr: DisposePtr. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndisposep;\n"
        "							if (ndisposep < 8) {\n"
        "								ndisposep++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF disposeptr 0xA01F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-disposeptr")
    return _insert_after_enter(text, _log_enter(m, "ndisposep"), "cpu-pef-disposeptr")


def patch_cpu_pef_debugger(text: str) -> str:
    """Auto mill 68k Debugger 0xA9FF Pascal pop 4."""
    m = _MARKERS["pef-debugger"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9ffu) {\n"
        "						/* leftover:pef-debugger: Debugger. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndebugger;\n"
        "							if (ndebugger < 8) {\n"
        "								ndebugger++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF debugger 0xA9FF\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-debugger")
    return _insert_after_enter(text, _log_enter(m, "ndebugger"), "cpu-pef-debugger")


def patch_cpu_pef_forecolor(text: str) -> str:
    """Auto mill 68k ForeColor 0xA862 Pascal pop 4."""
    m = _MARKERS["pef-forecolor"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa862u) {\n"
        "						/* leftover:pef-forecolor: ForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nforecolo;\n"
        "							if (nforecolo < 8) {\n"
        "								nforecolo++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF forecolor 0xA862\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-forecolor")
    return _insert_after_enter(text, _log_enter(m, "nforecolo"), "cpu-pef-forecolor")


def patch_cpu_pef_backcolor(text: str) -> str:
    """Auto mill 68k BackColor 0xA863 Pascal pop 4."""
    m = _MARKERS["pef-backcolor"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa863u) {\n"
        "						/* leftover:pef-backcolor: BackColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nbackcolo;\n"
        "							if (nbackcolo < 8) {\n"
        "								nbackcolo++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF backcolor 0xA863\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-backcolor")
    return _insert_after_enter(text, _log_enter(m, "nbackcolo"), "cpu-pef-backcolor")


def patch_cpu_pef_setappllim(text: str) -> str:
    """Auto mill 68k SetApplLimit 0xA02D Pascal pop 4."""
    m = _MARKERS["pef-setappllim"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa02du) {\n"
        "						/* leftover:pef-setappllim: SetApplLimit. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetappll;\n"
        "							if (nsetappll < 8) {\n"
        "								nsetappll++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setappllim 0xA02D\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setappllim")
    return _insert_after_enter(text, _log_enter(m, "nsetappll"), "cpu-pef-setappllim")


def patch_cpu_pef_saverestor(text: str) -> str:
    """Auto mill 68k SaveRestoreBits 0xA81E Pascal pop 4."""
    m = _MARKERS["pef-saverestor"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa81eu) {\n"
        "						/* leftover:pef-saverestor: SaveRestoreBits. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsaverest;\n"
        "							if (nsaverest < 8) {\n"
        "								nsaverest++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF saverestor 0xA81E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-saverestor")
    return _insert_after_enter(text, _log_enter(m, "nsaverest"), "cpu-pef-saverestor")


def patch_cpu_pef_aa874(text: str) -> str:
    """Auto mill 68k GetPort 0xA874 Pascal pop 4."""
    m = _MARKERS["pef-aa874"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa874u) {\n"
        "						/* leftover:pef-aa874: GetPort. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa874;\n"
        "							if (naa874 < 8) {\n"
        "								naa874++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa874 0xA874\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa874")
    return _insert_after_enter(text, _log_enter(m, "naa874"), "cpu-pef-aa874")


def patch_cpu_pef_disposehan(text: str) -> str:
    """Auto mill 68k DisposeHandle 0xA023 Pascal pop 4."""
    m = _MARKERS["pef-disposehan"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa023u) {\n"
        "						/* leftover:pef-disposehan: DisposeHandle. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndisposeh;\n"
        "							if (ndisposeh < 8) {\n"
        "								ndisposeh++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF disposehan 0xA023\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-disposehan")
    return _insert_after_enter(text, _log_enter(m, "ndisposeh"), "cpu-pef-disposehan")


def patch_cpu_pef_getzone(text: str) -> str:
    """Auto mill 68k GetZone 0xA11A Pascal pop 4."""
    m = _MARKERS["pef-getzone"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa11au) {\n"
        "						/* leftover:pef-getzone: GetZone. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetzone;\n"
        "							if (ngetzone < 8) {\n"
        "								ngetzone++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getzone 0xA11A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getzone")
    return _insert_after_enter(text, _log_enter(m, "ngetzone"), "cpu-pef-getzone")


def patch_cpu_pef_localtoglo(text: str) -> str:
    """Auto mill 68k LocalToGlobal 0xA870 Pascal pop 4."""
    m = _MARKERS["pef-localtoglo"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa870u) {\n"
        "						/* leftover:pef-localtoglo: LocalToGlobal. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nlocaltog;\n"
        "							if (nlocaltog < 8) {\n"
        "								nlocaltog++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF localtoglo 0xA870\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-localtoglo")
    return _insert_after_enter(text, _log_enter(m, "nlocaltog"), "cpu-pef-localtoglo")


def patch_cpu_pef_setpbits(text: str) -> str:
    """Auto mill 68k SetPBits 0xA875 Pascal pop 4."""
    m = _MARKERS["pef-setpbits"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa875u) {\n"
        "						/* leftover:pef-setpbits: SetPBits. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetpbits;\n"
        "							if (nsetpbits < 8) {\n"
        "								nsetpbits++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setpbits 0xA875\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setpbits")
    return _insert_after_enter(text, _log_enter(m, "nsetpbits"), "cpu-pef-setpbits")


def patch_cpu_pef_aa8ec(text: str) -> str:
    """Auto mill 68k CopyBits 0xA8EC Pascal pop 4."""
    m = _MARKERS["pef-aa8ec"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-aa8ec: CopyBits. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa8ec;\n"
        "							if (naa8ec < 8) {\n"
        "								naa8ec++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa8ec 0xA8EC\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa8ec")
    return _insert_after_enter(text, _log_enter(m, "naa8ec"), "cpu-pef-aa8ec")


def patch_cpu_pef_trapabe9(text: str) -> str:
    """Auto mill 68k TrapABE9 0xABE9 Pascal pop 4."""
    m = _MARKERS["pef-trapabe9"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xabe9u) {\n"
        "						/* leftover:pef-trapabe9: TrapABE9. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapabe9;\n"
        "							if (ntrapabe9 < 8) {\n"
        "								ntrapabe9++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapabe9 0xABE9\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapabe9")
    return _insert_after_enter(text, _log_enter(m, "ntrapabe9"), "cpu-pef-trapabe9")


def patch_cpu_pef_getostrapa(text: str) -> str:
    """Auto mill 68k GetOSTrapAddress 0xA346 Pascal pop 4."""
    m = _MARKERS["pef-getostrapa"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa346u) {\n"
        "						/* leftover:pef-getostrapa: GetOSTrapAddress. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetostra;\n"
        "							if (ngetostra < 8) {\n"
        "								ngetostra++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getostrapa 0xA346\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getostrapa")
    return _insert_after_enter(text, _log_enter(m, "ngetostra"), "cpu-pef-getostrapa")


def patch_cpu_pef_gettooltra(text: str) -> str:
    """Auto mill 68k GetToolTrapAddress 0xA746 Pascal pop 4."""
    m = _MARKERS["pef-gettooltra"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa746u) {\n"
        "						/* leftover:pef-gettooltra: GetToolTrapAddress. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngettoolt;\n"
        "							if (ngettoolt < 8) {\n"
        "								ngettoolt++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF gettooltra 0xA746\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-gettooltra")
    return _insert_after_enter(text, _log_enter(m, "ngettoolt"), "cpu-pef-gettooltra")


def patch_cpu_pef_powerdispa(text: str) -> str:
    """Auto mill 68k PowerDispatch 0xA09F Pascal pop 4."""
    m = _MARKERS["pef-powerdispa"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa09fu) {\n"
        "						/* leftover:pef-powerdispa: PowerDispatch. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npowerdis;\n"
        "							if (npowerdis < 8) {\n"
        "								npowerdis++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF powerdispa 0xA09F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-powerdispa")
    return _insert_after_enter(text, _log_enter(m, "npowerdis"), "cpu-pef-powerdispa")


def patch_cpu_pef_status(text: str) -> str:
    """Auto mill 68k Status 0xA005 Pascal pop 4."""
    m = _MARKERS["pef-status"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa005u) {\n"
        "						/* leftover:pef-status: Status. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nstatus;\n"
        "							if (nstatus < 8) {\n"
        "								nstatus++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF status 0xA005\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-status")
    return _insert_after_enter(text, _log_enter(m, "nstatus"), "cpu-pef-status")


def patch_cpu_pef_moremaster(text: str) -> str:
    """Auto mill 68k MoreMasters 0xA036 Pascal pop 4."""
    m = _MARKERS["pef-moremaster"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa036u) {\n"
        "						/* leftover:pef-moremaster: MoreMasters. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nmoremast;\n"
        "							if (nmoremast < 8) {\n"
        "								nmoremast++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF moremaster 0xA036\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-moremaster")
    return _insert_after_enter(text, _log_enter(m, "nmoremast"), "cpu-pef-moremaster")


def patch_cpu_pef_initutil(text: str) -> str:
    """Auto mill 68k InitUtil 0xA03F Pascal pop 4."""
    m = _MARKERS["pef-initutil"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa03fu) {\n"
        "						/* leftover:pef-initutil: InitUtil. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ninitutil;\n"
        "							if (ninitutil < 8) {\n"
        "								ninitutil++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF initutil 0xA03F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-initutil")
    return _insert_after_enter(text, _log_enter(m, "ninitutil"), "cpu-pef-initutil")


def patch_cpu_pef_blockmoved(text: str) -> str:
    """Auto mill 68k BlockMoveData 0xA22E Pascal pop 12."""
    m = _MARKERS["pef-blockmoved"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa22eu) {\n"
        "						/* leftover:pef-blockmoved: BlockMoveData. Pascal pop 12. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 12u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nblockmov;\n"
        "							if (nblockmov < 8) {\n"
        "								nblockmov++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF blockmoved 0xA22E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-blockmoved")
    return _insert_after_enter(text, _log_enter(m, "nblockmov"), "cpu-pef-blockmoved")


def patch_cpu_pef_sethandles(text: str) -> str:
    """Auto mill 68k SetHandleSize 0xA024 Pascal pop 4."""
    m = _MARKERS["pef-sethandles"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa024u) {\n"
        "						/* leftover:pef-sethandles: SetHandleSize. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsethandl;\n"
        "							if (nsethandl < 8) {\n"
        "								nsethandl++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF sethandles 0xA024\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sethandles")
    return _insert_after_enter(text, _log_enter(m, "nsethandl"), "cpu-pef-sethandles")


def patch_cpu_pef_newhandle(text: str) -> str:
    """Auto mill 68k NewHandle 0xA122 Pascal pop 4."""
    m = _MARKERS["pef-newhandle"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa122u) {\n"
        "						/* leftover:pef-newhandle: NewHandle. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nnewhandl;\n"
        "							if (nnewhandl < 8) {\n"
        "								nnewhandl++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF newhandle 0xA122\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-newhandle")
    return _insert_after_enter(text, _log_enter(m, "nnewhandl"), "cpu-pef-newhandle")


def patch_cpu_pef_control(text: str) -> str:
    """Auto mill 68k Control 0xA004 Pascal pop 4."""
    m = _MARKERS["pef-control"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa004u) {\n"
        "						/* leftover:pef-control: Control. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncontrol;\n"
        "							if (ncontrol < 8) {\n"
        "								ncontrol++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF control 0xA004\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-control")
    return _insert_after_enter(text, _log_enter(m, "ncontrol"), "cpu-pef-control")


def patch_cpu_pef_setptrsize(text: str) -> str:
    """Auto mill 68k SetPtrSize 0xA020 Pascal pop 4."""
    m = _MARKERS["pef-setptrsize"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa020u) {\n"
        "						/* leftover:pef-setptrsize: SetPtrSize. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetptrsi;\n"
        "							if (nsetptrsi < 8) {\n"
        "								nsetptrsi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setptrsize 0xA020\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setptrsize")
    return _insert_after_enter(text, _log_enter(m, "nsetptrsi"), "cpu-pef-setptrsize")


def patch_cpu_pef_memorydisp(text: str) -> str:
    """Auto mill 68k MemoryDispatch 0xA05C Pascal pop 4."""
    m = _MARKERS["pef-memorydisp"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa05cu) {\n"
        "						/* leftover:pef-memorydisp: MemoryDispatch. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nmemorydi;\n"
        "							if (nmemorydi < 8) {\n"
        "								nmemorydi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF memorydisp 0xA05C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-memorydisp")
    return _insert_after_enter(text, _log_enter(m, "nmemorydi"), "cpu-pef-memorydisp")


def patch_cpu_pef_aa15c(text: str) -> str:
    """Auto mill 68k MemoryDispatch 0xA15C Pascal pop 4."""
    m = _MARKERS["pef-aa15c"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa15cu) {\n"
        "						/* leftover:pef-aa15c: MemoryDispatch. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa15c;\n"
        "							if (naa15c < 8) {\n"
        "								naa15c++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa15c 0xA15C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa15c")
    return _insert_after_enter(text, _log_enter(m, "naa15c"), "cpu-pef-aa15c")


def patch_cpu_pef_trapa402(text: str) -> str:
    """Auto mill 68k TrapA402 0xA402 Pascal pop 4."""
    m = _MARKERS["pef-trapa402"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa402u) {\n"
        "						/* leftover:pef-trapa402: TrapA402. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa402;\n"
        "							if (ntrapa402 < 8) {\n"
        "								ntrapa402++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa402 0xA402\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa402")
    return _insert_after_enter(text, _log_enter(m, "ntrapa402"), "cpu-pef-trapa402")


def patch_cpu_pef_trapa403(text: str) -> str:
    """Auto mill 68k TrapA403 0xA403 Pascal pop 4."""
    m = _MARKERS["pef-trapa403"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa403u) {\n"
        "						/* leftover:pef-trapa403: TrapA403. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa403;\n"
        "							if (ntrapa403 < 8) {\n"
        "								ntrapa403++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa403 0xA403\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa403")
    return _insert_after_enter(text, _log_enter(m, "ntrapa403"), "cpu-pef-trapa403")


def patch_cpu_pef_syserror(text: str) -> str:
    """Auto mill 68k SysError 0xA9C9 Pascal pop 2."""
    m = _MARKERS["pef-syserror"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9c9u) {\n"
        "						/* leftover:pef-syserror: SysError. Pascal pop 2. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 2u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsyserror;\n"
        "							if (nsyserror < 8) {\n"
        "								nsyserror++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF syserror 0xA9C9\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-syserror")
    return _insert_after_enter(text, _log_enter(m, "nsyserror"), "cpu-pef-syserror")


def patch_cpu_pef_blockmove(text: str) -> str:
    """Auto mill 68k BlockMove 0xA02E Pascal pop 12."""
    m = _MARKERS["pef-blockmove"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa02eu) {\n"
        "						/* leftover:pef-blockmove: BlockMove. Pascal pop 12. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 12u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nblockmov;\n"
        "							if (nblockmov < 8) {\n"
        "								nblockmov++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF blockmove 0xA02E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-blockmove")
    return _insert_after_enter(text, _log_enter(m, "nblockmov"), "cpu-pef-blockmove")


def patch_cpu_pef_newgdevice(text: str) -> str:
    """Auto mill 68k NewGDevice 0xAA2F Pascal pop 4."""
    m = _MARKERS["pef-newgdevice"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa2fu) {\n"
        "						/* leftover:pef-newgdevice: NewGDevice. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nnewgdevi;\n"
        "							if (nnewgdevi < 8) {\n"
        "								nnewgdevi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF newgdevice 0xAA2F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-newgdevice")
    return _insert_after_enter(text, _log_enter(m, "nnewgdevi"), "cpu-pef-newgdevice")


def patch_cpu_pef_setdevicea(text: str) -> str:
    """Auto mill 68k SetDeviceAttribute 0xAA2D Pascal pop 4."""
    m = _MARKERS["pef-setdevicea"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa2du) {\n"
        "						/* leftover:pef-setdevicea: SetDeviceAttribute. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetdevic;\n"
        "							if (nsetdevic < 8) {\n"
        "								nsetdevic++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setdevicea 0xAA2D\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setdevicea")
    return _insert_after_enter(text, _log_enter(m, "nsetdevic"), "cpu-pef-setdevicea")


def patch_cpu_pef_getcwmgrpo(text: str) -> str:
    """Auto mill 68k GetCWMgrPort 0xAA48 Pascal pop 4."""
    m = _MARKERS["pef-getcwmgrpo"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa48u) {\n"
        "						/* leftover:pef-getcwmgrpo: GetCWMgrPort. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetcwmgr;\n"
        "							if (ngetcwmgr < 8) {\n"
        "								ngetcwmgr++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getcwmgrpo 0xAA48\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getcwmgrpo")
    return _insert_after_enter(text, _log_enter(m, "ngetcwmgr"), "cpu-pef-getcwmgrpo")


def patch_cpu_pef_aa8dc(text: str) -> str:
    """Auto mill 68k CopyRgn 0xA8DC Pascal pop 8."""
    m = _MARKERS["pef-aa8dc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8dcu) {\n"
        "						/* leftover:pef-aa8dc: CopyRgn. Pascal pop 8. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 8u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa8dc;\n"
        "							if (naa8dc < 8) {\n"
        "								naa8dc++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa8dc 0xA8DC\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa8dc")
    return _insert_after_enter(text, _log_enter(m, "naa8dc"), "cpu-pef-aa8dc")


def patch_cpu_pef_textwidth(text: str) -> str:
    """Auto mill 68k TextWidth 0xA886 Pascal pop 4."""
    m = _MARKERS["pef-textwidth"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa886u) {\n"
        "						/* leftover:pef-textwidth: TextWidth. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntextwidt;\n"
        "							if (ntextwidt < 8) {\n"
        "								ntextwidt++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF textwidth 0xA886\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-textwidth")
    return _insert_after_enter(text, _log_enter(m, "ntextwidt"), "cpu-pef-textwidth")


def patch_cpu_pef_aa893(text: str) -> str:
    """Auto mill 68k MoveTo 0xA893 Pascal pop 4."""
    m = _MARKERS["pef-aa893"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa893u) {\n"
        "						/* leftover:pef-aa893: MoveTo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa893;\n"
        "							if (naa893 < 8) {\n"
        "								naa893++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa893 0xA893\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa893")
    return _insert_after_enter(text, _log_enter(m, "naa893"), "cpu-pef-aa893")


def patch_cpu_pef_uprstring(text: str) -> str:
    """Auto mill 68k UprString 0xA054 Pascal pop 4."""
    m = _MARKERS["pef-uprstring"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa054u) {\n"
        "						/* leftover:pef-uprstring: UprString. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nuprstrin;\n"
        "							if (nuprstrin < 8) {\n"
        "								nuprstrin++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF uprstring 0xA054\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-uprstring")
    return _insert_after_enter(text, _log_enter(m, "nuprstrin"), "cpu-pef-uprstring")


def patch_cpu_pef_hfsdispatc(text: str) -> str:
    """Auto mill 68k HFSDispatch 0xA260 Pascal pop 4."""
    m = _MARKERS["pef-hfsdispatc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa260u) {\n"
        "						/* leftover:pef-hfsdispatc: HFSDispatch. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhfsdispa;\n"
        "							if (nhfsdispa < 8) {\n"
        "								nhfsdispa++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hfsdispatc 0xA260\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hfsdispatc")
    return _insert_after_enter(text, _log_enter(m, "nhfsdispa"), "cpu-pef-hfsdispatc")


def patch_cpu_pef_hopenrf(text: str) -> str:
    """Auto mill 68k HOpenRF 0xA20A Pascal pop 4."""
    m = _MARKERS["pef-hopenrf"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa20au) {\n"
        "						/* leftover:pef-hopenrf: HOpenRF. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhopenrf;\n"
        "							if (nhopenrf < 8) {\n"
        "								nhopenrf++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hopenrf 0xA20A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hopenrf")
    return _insert_after_enter(text, _log_enter(m, "nhopenrf"), "cpu-pef-hopenrf")


def patch_cpu_pef_hcreate(text: str) -> str:
    """Auto mill 68k HCreate 0xA208 Pascal pop 4."""
    m = _MARKERS["pef-hcreate"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa208u) {\n"
        "						/* leftover:pef-hcreate: HCreate. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhcreate;\n"
        "							if (nhcreate < 8) {\n"
        "								nhcreate++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hcreate 0xA208\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hcreate")
    return _insert_after_enter(text, _log_enter(m, "nhcreate"), "cpu-pef-hcreate")


def patch_cpu_pef_hdelete(text: str) -> str:
    """Auto mill 68k HDelete 0xA209 Pascal pop 4."""
    m = _MARKERS["pef-hdelete"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa209u) {\n"
        "						/* leftover:pef-hdelete: HDelete. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhdelete;\n"
        "							if (nhdelete < 8) {\n"
        "								nhdelete++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hdelete 0xA209\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hdelete")
    return _insert_after_enter(text, _log_enter(m, "nhdelete"), "cpu-pef-hdelete")


def patch_cpu_pef_hgetfilein(text: str) -> str:
    """Auto mill 68k HGetFileInfo 0xA20C Pascal pop 4."""
    m = _MARKERS["pef-hgetfilein"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa20cu) {\n"
        "						/* leftover:pef-hgetfilein: HGetFileInfo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhgetfile;\n"
        "							if (nhgetfile < 8) {\n"
        "								nhgetfile++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hgetfilein 0xA20C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hgetfilein")
    return _insert_after_enter(text, _log_enter(m, "nhgetfile"), "cpu-pef-hgetfilein")


def patch_cpu_pef_hsetflock(text: str) -> str:
    """Auto mill 68k HSetFLock 0xA241 Pascal pop 4."""
    m = _MARKERS["pef-hsetflock"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa241u) {\n"
        "						/* leftover:pef-hsetflock: HSetFLock. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhsetfloc;\n"
        "							if (nhsetfloc < 8) {\n"
        "								nhsetfloc++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hsetflock 0xA241\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hsetflock")
    return _insert_after_enter(text, _log_enter(m, "nhsetfloc"), "cpu-pef-hsetflock")


def patch_cpu_pef_hrstflock(text: str) -> str:
    """Auto mill 68k HRstFLock 0xA242 Pascal pop 4."""
    m = _MARKERS["pef-hrstflock"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa242u) {\n"
        "						/* leftover:pef-hrstflock: HRstFLock. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhrstfloc;\n"
        "							if (nhrstfloc < 8) {\n"
        "								nhrstfloc++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hrstflock 0xA242\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hrstflock")
    return _insert_after_enter(text, _log_enter(m, "nhrstfloc"), "cpu-pef-hrstflock")


def patch_cpu_pef_hrename(text: str) -> str:
    """Auto mill 68k HRename 0xA20B Pascal pop 4."""
    m = _MARKERS["pef-hrename"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa20bu) {\n"
        "						/* leftover:pef-hrename: HRename. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhrename;\n"
        "							if (nhrename < 8) {\n"
        "								nhrename++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hrename 0xA20B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hrename")
    return _insert_after_enter(text, _log_enter(m, "nhrename"), "cpu-pef-hrename")


def patch_cpu_pef_hopenresfi(text: str) -> str:
    """Auto mill 68k HOpenResFile 0xA81A Pascal pop 4."""
    m = _MARKERS["pef-hopenresfi"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa81au) {\n"
        "						/* leftover:pef-hopenresfi: HOpenResFile. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhopenres;\n"
        "							if (nhopenres < 8) {\n"
        "								nhopenres++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hopenresfi 0xA81A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hopenresfi")
    return _insert_after_enter(text, _log_enter(m, "nhopenres"), "cpu-pef-hopenresfi")


def patch_cpu_pef_hcreateres(text: str) -> str:
    """Auto mill 68k HCreateResFile 0xA81B Pascal pop 4."""
    m = _MARKERS["pef-hcreateres"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa81bu) {\n"
        "						/* leftover:pef-hcreateres: HCreateResFile. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhcreater;\n"
        "							if (nhcreater < 8) {\n"
        "								nhcreater++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hcreateres 0xA81B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hcreateres")
    return _insert_after_enter(text, _log_enter(m, "nhcreater"), "cpu-pef-hcreateres")


def patch_cpu_pef_getvolinfo(text: str) -> str:
    """Auto mill 68k GetVolInfo 0xA007 Pascal pop 4."""
    m = _MARKERS["pef-getvolinfo"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa007u) {\n"
        "						/* leftover:pef-getvolinfo: GetVolInfo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetvolin;\n"
        "							if (ngetvolin < 8) {\n"
        "								ngetvolin++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getvolinfo 0xA007\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getvolinfo")
    return _insert_after_enter(text, _log_enter(m, "ngetvolin"), "cpu-pef-getvolinfo")


def patch_cpu_pef_hopen(text: str) -> str:
    """Auto mill 68k HOpen 0xA200 Pascal pop 4."""
    m = _MARKERS["pef-hopen"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa200u) {\n"
        "						/* leftover:pef-hopen: HOpen. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhopen;\n"
        "							if (nhopen < 8) {\n"
        "								nhopen++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hopen 0xA200\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hopen")
    return _insert_after_enter(text, _log_enter(m, "nhopen"), "cpu-pef-hopen")


def patch_cpu_pef_trapa600(text: str) -> str:
    """Auto mill 68k TrapA600 0xA600 Pascal pop 4."""
    m = _MARKERS["pef-trapa600"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa600u) {\n"
        "						/* leftover:pef-trapa600: TrapA600. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa600;\n"
        "							if (ntrapa600 < 8) {\n"
        "								ntrapa600++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa600 0xA600\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa600")
    return _insert_after_enter(text, _log_enter(m, "ntrapa600"), "cpu-pef-trapa600")


def patch_cpu_pef_trapa609(text: str) -> str:
    """Auto mill 68k TrapA609 0xA609 Pascal pop 4."""
    m = _MARKERS["pef-trapa609"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa609u) {\n"
        "						/* leftover:pef-trapa609: TrapA609. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa609;\n"
        "							if (ntrapa609 < 8) {\n"
        "								ntrapa609++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa609 0xA609\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa609")
    return _insert_after_enter(text, _log_enter(m, "ntrapa609"), "cpu-pef-trapa609")


def patch_cpu_pef_trapa401(text: str) -> str:
    """Auto mill 68k TrapA401 0xA401 Pascal pop 4."""
    m = _MARKERS["pef-trapa401"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa401u) {\n"
        "						/* leftover:pef-trapa401: TrapA401. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa401;\n"
        "							if (ntrapa401 < 8) {\n"
        "								ntrapa401++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa401 0xA401\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa401")
    return _insert_after_enter(text, _log_enter(m, "ntrapa401"), "cpu-pef-trapa401")


def patch_cpu_pef_trapa660(text: str) -> str:
    """Auto mill 68k TrapA660 0xA660 Pascal pop 4."""
    m = _MARKERS["pef-trapa660"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa660u) {\n"
        "						/* leftover:pef-trapa660: TrapA660. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa660;\n"
        "							if (ntrapa660 < 8) {\n"
        "								ntrapa660++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa660 0xA660\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa660")
    return _insert_after_enter(text, _log_enter(m, "ntrapa660"), "cpu-pef-trapa660")


def patch_cpu_pef_hgetvinfo(text: str) -> str:
    """Auto mill 68k HGetVInfo 0xA207 Pascal pop 4."""
    m = _MARKERS["pef-hgetvinfo"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa207u) {\n"
        "						/* leftover:pef-hgetvinfo: HGetVInfo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhgetvinf;\n"
        "							if (nhgetvinf < 8) {\n"
        "								nhgetvinf++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hgetvinfo 0xA207\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hgetvinfo")
    return _insert_after_enter(text, _log_enter(m, "nhgetvinf"), "cpu-pef-hgetvinfo")


def patch_cpu_pef_trapa607(text: str) -> str:
    """Auto mill 68k TrapA607 0xA607 Pascal pop 4."""
    m = _MARKERS["pef-trapa607"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa607u) {\n"
        "						/* leftover:pef-trapa607: TrapA607. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa607;\n"
        "							if (ntrapa607 < 8) {\n"
        "								ntrapa607++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa607 0xA607\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa607")
    return _insert_after_enter(text, _log_enter(m, "ntrapa607"), "cpu-pef-trapa607")


def patch_cpu_pef_trapa60c(text: str) -> str:
    """Auto mill 68k TrapA60C 0xA60C Pascal pop 4."""
    m = _MARKERS["pef-trapa60c"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa60cu) {\n"
        "						/* leftover:pef-trapa60c: TrapA60C. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa60c;\n"
        "							if (ntrapa60c < 8) {\n"
        "								ntrapa60c++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa60c 0xA60C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa60c")
    return _insert_after_enter(text, _log_enter(m, "ntrapa60c"), "cpu-pef-trapa60c")


def patch_cpu_pef_hsetfilein(text: str) -> str:
    """Auto mill 68k HSetFileInfo 0xA20D Pascal pop 4."""
    m = _MARKERS["pef-hsetfilein"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa20du) {\n"
        "						/* leftover:pef-hsetfilein: HSetFileInfo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nhsetfile;\n"
        "							if (nhsetfile < 8) {\n"
        "								nhsetfile++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF hsetfilein 0xA20D\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-hsetfilein")
    return _insert_after_enter(text, _log_enter(m, "nhsetfile"), "cpu-pef-hsetfilein")


def patch_cpu_pef_trapa60d(text: str) -> str:
    """Auto mill 68k TrapA60D 0xA60D Pascal pop 4."""
    m = _MARKERS["pef-trapa60d"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa60du) {\n"
        "						/* leftover:pef-trapa60d: TrapA60D. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa60d;\n"
        "							if (ntrapa60d < 8) {\n"
        "								ntrapa60d++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa60d 0xA60D\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa60d")
    return _insert_after_enter(text, _log_enter(m, "ntrapa60d"), "cpu-pef-trapa60d")


def patch_cpu_pef_trapa608(text: str) -> str:
    """Auto mill 68k TrapA608 0xA608 Pascal pop 4."""
    m = _MARKERS["pef-trapa608"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa608u) {\n"
        "						/* leftover:pef-trapa608: TrapA608. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa608;\n"
        "							if (ntrapa608 < 8) {\n"
        "								ntrapa608++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa608 0xA608\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa608")
    return _insert_after_enter(text, _log_enter(m, "ntrapa608"), "cpu-pef-trapa608")


def patch_cpu_pef_trapa410(text: str) -> str:
    """Auto mill 68k TrapA410 0xA410 Pascal pop 4."""
    m = _MARKERS["pef-trapa410"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa410u) {\n"
        "						/* leftover:pef-trapa410: TrapA410. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa410;\n"
        "							if (ntrapa410 < 8) {\n"
        "								ntrapa410++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa410 0xA410\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa410")
    return _insert_after_enter(text, _log_enter(m, "ntrapa410"), "cpu-pef-trapa410")


def patch_cpu_pef_trapa412(text: str) -> str:
    """Auto mill 68k TrapA412 0xA412 Pascal pop 4."""
    m = _MARKERS["pef-trapa412"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa412u) {\n"
        "						/* leftover:pef-trapa412: TrapA412. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa412;\n"
        "							if (ntrapa412 < 8) {\n"
        "								ntrapa412++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa412 0xA412\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa412")
    return _insert_after_enter(text, _log_enter(m, "ntrapa412"), "cpu-pef-trapa412")


def patch_cpu_pef_trapa411(text: str) -> str:
    """Auto mill 68k TrapA411 0xA411 Pascal pop 4."""
    m = _MARKERS["pef-trapa411"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa411u) {\n"
        "						/* leftover:pef-trapa411: TrapA411. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa411;\n"
        "							if (ntrapa411 < 8) {\n"
        "								ntrapa411++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa411 0xA411\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa411")
    return _insert_after_enter(text, _log_enter(m, "ntrapa411"), "cpu-pef-trapa411")


def patch_cpu_pef_aa013(text: str) -> str:
    """Auto mill 68k FlushVol 0xA013 Pascal pop 4."""
    m = _MARKERS["pef-aa013"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa013u) {\n"
        "						/* leftover:pef-aa013: FlushVol. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa013;\n"
        "							if (naa013 < 8) {\n"
        "								naa013++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa013 0xA013\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa013")
    return _insert_after_enter(text, _log_enter(m, "naa013"), "cpu-pef-aa013")


def patch_cpu_pef_trapa413(text: str) -> str:
    """Auto mill 68k TrapA413 0xA413 Pascal pop 4."""
    m = _MARKERS["pef-trapa413"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa413u) {\n"
        "						/* leftover:pef-trapa413: TrapA413. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa413;\n"
        "							if (ntrapa413 < 8) {\n"
        "								ntrapa413++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa413 0xA413\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa413")
    return _insert_after_enter(text, _log_enter(m, "ntrapa413"), "cpu-pef-trapa413")


def patch_cpu_pef_trapa23c(text: str) -> str:
    """Auto mill 68k TrapA23C 0xA23C Pascal pop 4."""
    m = _MARKERS["pef-trapa23c"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa23cu) {\n"
        "						/* leftover:pef-trapa23c: TrapA23C. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa23c;\n"
        "							if (ntrapa23c < 8) {\n"
        "								ntrapa23c++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa23c 0xA23C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa23c")
    return _insert_after_enter(text, _log_enter(m, "ntrapa23c"), "cpu-pef-trapa23c")


def patch_cpu_pef_trapa250(text: str) -> str:
    """Auto mill 68k TrapA250 0xA250 Pascal pop 4."""
    m = _MARKERS["pef-trapa250"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa250u) {\n"
        "						/* leftover:pef-trapa250: TrapA250. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa250;\n"
        "							if (ntrapa250 < 8) {\n"
        "								ntrapa250++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa250 0xA250\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa250")
    return _insert_after_enter(text, _log_enter(m, "ntrapa250"), "cpu-pef-trapa250")


def patch_cpu_pef_decstr68k(text: str) -> str:
    """Auto mill 68k DECSTR68K 0xA9EE Pascal pop 4."""
    m = _MARKERS["pef-decstr68k"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9eeu) {\n"
        "						/* leftover:pef-decstr68k: DECSTR68K. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndecstr68;\n"
        "							if (ndecstr68 < 8) {\n"
        "								ndecstr68++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF decstr68k 0xA9EE\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-decstr68k")
    return _insert_after_enter(text, _log_enter(m, "ndecstr68"), "cpu-pef-decstr68k")


def patch_cpu_pef_trapaa7f(text: str) -> str:
    """Auto mill 68k TrapAA7F 0xAA7F Pascal pop 4."""
    m = _MARKERS["pef-trapaa7f"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa7fu) {\n"
        "						/* leftover:pef-trapaa7f: TrapAA7F. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapaa7f;\n"
        "							if (ntrapaa7f < 8) {\n"
        "								ntrapaa7f++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapaa7f 0xAA7F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapaa7f")
    return _insert_after_enter(text, _log_enter(m, "ntrapaa7f"), "cpu-pef-trapaa7f")


def patch_cpu_pef_ptrtohand(text: str) -> str:
    """Auto mill 68k PtrToHand 0xA9E3 Pascal pop 12."""
    m = _MARKERS["pef-ptrtohand"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9e3u) {\n"
        "						/* leftover:pef-ptrtohand: PtrToHand. Pascal pop 12. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 12u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nptrtohan;\n"
        "							if (nptrtohan < 8) {\n"
        "								nptrtohan++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF ptrtohand 0xA9E3\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-ptrtohand")
    return _insert_after_enter(text, _log_enter(m, "nptrtohan"), "cpu-pef-ptrtohand")


def patch_cpu_pef_getscrap(text: str) -> str:
    """Auto mill 68k GetScrap 0xA9FD Pascal pop 8."""
    m = _MARKERS["pef-getscrap"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9fdu) {\n"
        "						/* leftover:pef-getscrap: GetScrap. Pascal pop 8. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 8u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetscrap;\n"
        "							if (ngetscrap < 8) {\n"
        "								ngetscrap++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getscrap 0xA9FD\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getscrap")
    return _insert_after_enter(text, _log_enter(m, "ngetscrap"), "cpu-pef-getscrap")


def patch_cpu_pef_putscrap(text: str) -> str:
    """Auto mill 68k PutScrap 0xA9FE Pascal pop 8."""
    m = _MARKERS["pef-putscrap"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9feu) {\n"
        "						/* leftover:pef-putscrap: PutScrap. Pascal pop 8. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 8u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nputscrap;\n"
        "							if (nputscrap < 8) {\n"
        "								nputscrap++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF putscrap 0xA9FE\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-putscrap")
    return _insert_after_enter(text, _log_enter(m, "nputscrap"), "cpu-pef-putscrap")


def patch_cpu_pef_invalrgn(text: str) -> str:
    """Auto mill 68k InvalRgn 0xA927 Pascal pop 4."""
    m = _MARKERS["pef-invalrgn"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa927u) {\n"
        "						/* leftover:pef-invalrgn: InvalRgn. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ninvalrgn;\n"
        "							if (ninvalrgn < 8) {\n"
        "								ninvalrgn++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF invalrgn 0xA927\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-invalrgn")
    return _insert_after_enter(text, _log_enter(m, "ninvalrgn"), "cpu-pef-invalrgn")


def patch_cpu_pef_paintone(text: str) -> str:
    """Auto mill 68k PaintOne 0xA90C Pascal pop 4."""
    m = _MARKERS["pef-paintone"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa90cu) {\n"
        "						/* leftover:pef-paintone: PaintOne. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npaintone;\n"
        "							if (npaintone < 8) {\n"
        "								npaintone++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF paintone 0xA90C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-paintone")
    return _insert_after_enter(text, _log_enter(m, "npaintone"), "cpu-pef-paintone")


def patch_cpu_pef_getpenstat(text: str) -> str:
    """Auto mill 68k GetPenState 0xA898 Pascal pop 4."""
    m = _MARKERS["pef-getpenstat"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa898u) {\n"
        "						/* leftover:pef-getpenstat: GetPenState. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetpenst;\n"
        "							if (ngetpenst < 8) {\n"
        "								ngetpenst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getpenstat 0xA898\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getpenstat")
    return _insert_after_enter(text, _log_enter(m, "ngetpenst"), "cpu-pef-getpenstat")


def patch_cpu_pef_testdevice(text: str) -> str:
    """Auto mill 68k TestDeviceAttribute 0xAA2C Pascal pop 4."""
    m = _MARKERS["pef-testdevice"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa2cu) {\n"
        "						/* leftover:pef-testdevice: TestDeviceAttribute. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntestdevi;\n"
        "							if (ntestdevi < 8) {\n"
        "								ntestdevi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF testdevice 0xAA2C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-testdevice")
    return _insert_after_enter(text, _log_enter(m, "ntestdevi"), "cpu-pef-testdevice")


def patch_cpu_pef_setpenstat(text: str) -> str:
    """Auto mill 68k SetPenState 0xA899 Pascal pop 4."""
    m = _MARKERS["pef-setpenstat"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa899u) {\n"
        "						/* leftover:pef-setpenstat: SetPenState. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetpenst;\n"
        "							if (nsetpenst < 8) {\n"
        "								nsetpenst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setpenstat 0xA899\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setpenstat")
    return _insert_after_enter(text, _log_enter(m, "nsetpenst"), "cpu-pef-setpenstat")


def patch_cpu_pef_getwvarian(text: str) -> str:
    """Auto mill 68k GetWVariant 0xA80A Pascal pop 4."""
    m = _MARKERS["pef-getwvarian"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa80au) {\n"
        "						/* leftover:pef-getwvarian: GetWVariant. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetwvari;\n"
        "							if (ngetwvari < 8) {\n"
        "								ngetwvari++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getwvarian 0xA80A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getwvarian")
    return _insert_after_enter(text, _log_enter(m, "ngetwvari"), "cpu-pef-getwvarian")


def patch_cpu_pef_emptyrgn(text: str) -> str:
    """Auto mill 68k EmptyRgn 0xA8E2 Pascal pop 4."""
    m = _MARKERS["pef-emptyrgn"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8e2u) {\n"
        "						/* leftover:pef-emptyrgn: EmptyRgn. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nemptyrgn;\n"
        "							if (nemptyrgn < 8) {\n"
        "								nemptyrgn++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF emptyrgn 0xA8E2\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-emptyrgn")
    return _insert_after_enter(text, _log_enter(m, "nemptyrgn"), "cpu-pef-emptyrgn")


def patch_cpu_pef_setemptyrg(text: str) -> str:
    """Auto mill 68k SetEmptyRgn 0xA8DD Pascal pop 4."""
    m = _MARKERS["pef-setemptyrg"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8ddu) {\n"
        "						/* leftover:pef-setemptyrg: SetEmptyRgn. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetempty;\n"
        "							if (nsetempty < 8) {\n"
        "								nsetempty++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setemptyrg 0xA8DD\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setemptyrg")
    return _insert_after_enter(text, _log_enter(m, "nsetempty"), "cpu-pef-setemptyrg")


def patch_cpu_pef_drawmenuba(text: str) -> str:
    """Auto mill 68k DrawMenuBar 0xA937 Pascal pop 4."""
    m = _MARKERS["pef-drawmenuba"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa937u) {\n"
        "						/* leftover:pef-drawmenuba: DrawMenuBar. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndrawmenu;\n"
        "							if (ndrawmenu < 8) {\n"
        "								ndrawmenu++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF drawmenuba 0xA937\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-drawmenuba")
    return _insert_after_enter(text, _log_enter(m, "ndrawmenu"), "cpu-pef-drawmenuba")


def patch_cpu_pef_tickcount(text: str) -> str:
    """Auto mill 68k TickCount 0xA975 Pascal pop 4."""
    m = _MARKERS["pef-tickcount"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa975u) {\n"
        "						/* leftover:pef-tickcount: TickCount. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntickcoun;\n"
        "							if (ntickcoun < 8) {\n"
        "								ntickcoun++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF tickcount 0xA975\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-tickcount")
    return _insert_after_enter(text, _log_enter(m, "ntickcoun"), "cpu-pef-tickcount")


def patch_cpu_pef_devicemgr(text: str) -> str:
    """Auto mill 68k DeviceMgr 0xAA6A Pascal pop 4."""
    m = _MARKERS["pef-devicemgr"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa6au) {\n"
        "						/* leftover:pef-devicemgr: DeviceMgr. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndevicemg;\n"
        "							if (ndevicemg < 8) {\n"
        "								ndevicemg++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF devicemgr 0xAA6A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-devicemgr")
    return _insert_after_enter(text, _log_enter(m, "ndevicemg"), "cpu-pef-devicemgr")


def patch_cpu_pef_atamgr(text: str) -> str:
    """Auto mill 68k ATAMgr 0xAAF1 Pascal pop 4."""
    m = _MARKERS["pef-atamgr"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaaf1u) {\n"
        "						/* leftover:pef-atamgr: ATAMgr. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned natamgr;\n"
        "							if (natamgr < 8) {\n"
        "								natamgr++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF atamgr 0xAAF1\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-atamgr")
    return _insert_after_enter(text, _log_enter(m, "natamgr"), "cpu-pef-atamgr")


def patch_cpu_pef_detachreso(text: str) -> str:
    """Auto mill 68k DetachResource 0xA992 Pascal pop 4."""
    m = _MARKERS["pef-detachreso"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa992u) {\n"
        "						/* leftover:pef-detachreso: DetachResource. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndetachre;\n"
        "							if (ndetachre < 8) {\n"
        "								ndetachre++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF detachreso 0xA992\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-detachreso")
    return _insert_after_enter(text, _log_enter(m, "ndetachre"), "cpu-pef-detachreso")


def patch_cpu_pef_drvrinstal(text: str) -> str:
    """Auto mill 68k DrvrInstall 0xA03D Pascal pop 4."""
    m = _MARKERS["pef-drvrinstal"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa03du) {\n"
        "						/* leftover:pef-drvrinstal: DrvrInstall. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndrvrinst;\n"
        "							if (ndrvrinst < 8) {\n"
        "								ndrvrinst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF drvrinstal 0xA03D\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-drvrinstal")
    return _insert_after_enter(text, _log_enter(m, "ndrvrinst"), "cpu-pef-drvrinstal")


def patch_cpu_pef_comparestr(text: str) -> str:
    """Auto mill 68k CompareString 0xA050 Pascal pop 4."""
    m = _MARKERS["pef-comparestr"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa050u) {\n"
        "						/* leftover:pef-comparestr: CompareString. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncompares;\n"
        "							if (ncompares < 8) {\n"
        "								ncompares++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF comparestr 0xA050\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-comparestr")
    return _insert_after_enter(text, _log_enter(m, "ncompares"), "cpu-pef-comparestr")


def patch_cpu_pef_curresfile(text: str) -> str:
    """Auto mill 68k CurResFile 0xA994 Pascal pop 4."""
    m = _MARKERS["pef-curresfile"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa994u) {\n"
        "						/* leftover:pef-curresfile: CurResFile. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ncurresfi;\n"
        "							if (ncurresfi < 8) {\n"
        "								ncurresfi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF curresfile 0xA994\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-curresfile")
    return _insert_after_enter(text, _log_enter(m, "ncurresfi"), "cpu-pef-curresfile")


def patch_cpu_pef_useresfile(text: str) -> str:
    """Auto mill 68k UseResFile 0xA998 Pascal pop 4."""
    m = _MARKERS["pef-useresfile"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa998u) {\n"
        "						/* leftover:pef-useresfile: UseResFile. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nuseresfi;\n"
        "							if (nuseresfi < 8) {\n"
        "								nuseresfi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF useresfile 0xA998\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-useresfile")
    return _insert_after_enter(text, _log_enter(m, "nuseresfi"), "cpu-pef-useresfile")


def patch_cpu_pef_get1resour(text: str) -> str:
    """Auto mill 68k Get1Resource 0xA81F Pascal pop 4."""
    m = _MARKERS["pef-get1resour"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa81fu) {\n"
        "						/* leftover:pef-get1resour: Get1Resource. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nget1reso;\n"
        "							if (nget1reso < 8) {\n"
        "								nget1reso++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF get1resour 0xA81F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-get1resour")
    return _insert_after_enter(text, _log_enter(m, "nget1reso"), "cpu-pef-get1resour")


def patch_cpu_pef_loadresour(text: str) -> str:
    """Auto mill 68k LoadResource 0xA9A2 Pascal pop 4."""
    m = _MARKERS["pef-loadresour"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9a2u) {\n"
        "						/* leftover:pef-loadresour: LoadResource. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nloadreso;\n"
        "							if (nloadreso < 8) {\n"
        "								nloadreso++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF loadresour 0xA9A2\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-loadresour")
    return _insert_after_enter(text, _log_enter(m, "nloadreso"), "cpu-pef-loadresour")


def patch_cpu_pef_releaseres(text: str) -> str:
    """Auto mill 68k ReleaseResource 0xA9A3 Pascal pop 4."""
    m = _MARKERS["pef-releaseres"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9a3u) {\n"
        "						/* leftover:pef-releaseres: ReleaseResource. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nreleaser;\n"
        "							if (nreleaser < 8) {\n"
        "								nreleaser++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF releaseres 0xA9A3\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-releaseres")
    return _insert_after_enter(text, _log_enter(m, "nreleaser"), "cpu-pef-releaseres")


def patch_cpu_pef_writeparam(text: str) -> str:
    """Auto mill 68k WriteParam 0xA038 Pascal pop 4."""
    m = _MARKERS["pef-writeparam"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa038u) {\n"
        "						/* leftover:pef-writeparam: WriteParam. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nwritepar;\n"
        "							if (nwritepar < 8) {\n"
        "								nwritepar++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF writeparam 0xA038\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-writeparam")
    return _insert_after_enter(text, _log_enter(m, "nwritepar"), "cpu-pef-writeparam")


def patch_cpu_pef_nminstall(text: str) -> str:
    """Auto mill 68k NMInstall 0xA05E Pascal pop 4."""
    m = _MARKERS["pef-nminstall"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa05eu) {\n"
        "						/* leftover:pef-nminstall: NMInstall. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nnminstal;\n"
        "							if (nnminstal < 8) {\n"
        "								nnminstal++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF nminstall 0xA05E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-nminstall")
    return _insert_after_enter(text, _log_enter(m, "nnminstal"), "cpu-pef-nminstall")


def patch_cpu_pef_nmremove(text: str) -> str:
    """Auto mill 68k NMRemove 0xA05F Pascal pop 4."""
    m = _MARKERS["pef-nmremove"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa05fu) {\n"
        "						/* leftover:pef-nmremove: NMRemove. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nnmremove;\n"
        "							if (nnmremove < 8) {\n"
        "								nnmremove++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF nmremove 0xA05F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-nmremove")
    return _insert_after_enter(text, _log_enter(m, "nnmremove"), "cpu-pef-nmremove")


def patch_cpu_pef_internalwa(text: str) -> str:
    """Auto mill 68k InternalWait 0xA07F Pascal pop 4."""
    m = _MARKERS["pef-internalwa"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa07fu) {\n"
        "						/* leftover:pef-internalwa: InternalWait. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ninternal;\n"
        "							if (ninternal < 8) {\n"
        "								ninternal++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF internalwa 0xA07F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-internalwa")
    return _insert_after_enter(text, _log_enter(m, "ninternal"), "cpu-pef-internalwa")


def patch_cpu_pef_writexpram(text: str) -> str:
    """Auto mill 68k WriteXPRam 0xA052 Pascal pop 4."""
    m = _MARKERS["pef-writexpram"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa052u) {\n"
        "						/* leftover:pef-writexpram: WriteXPRam. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nwritexpr;\n"
        "							if (nwritexpr < 8) {\n"
        "								nwritexpr++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF writexpram 0xA052\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-writexpram")
    return _insert_after_enter(text, _log_enter(m, "nwritexpr"), "cpu-pef-writexpram")


def patch_cpu_pef_setadbinfo(text: str) -> str:
    """Auto mill 68k SetADBInfo 0xA07A Pascal pop 4."""
    m = _MARKERS["pef-setadbinfo"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa07au) {\n"
        "						/* leftover:pef-setadbinfo: SetADBInfo. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetadbin;\n"
        "							if (nsetadbin < 8) {\n"
        "								nsetadbin++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setadbinfo 0xA07A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setadbinfo")
    return _insert_after_enter(text, _log_enter(m, "nsetadbin"), "cpu-pef-setadbinfo")


def patch_cpu_pef_primetime(text: str) -> str:
    """Auto mill 68k PrimeTime 0xA05A Pascal pop 4."""
    m = _MARKERS["pef-primetime"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa05au) {\n"
        "						/* leftover:pef-primetime: PrimeTime. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nprimetim;\n"
        "							if (nprimetim < 8) {\n"
        "								nprimetim++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF primetime 0xA05A\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-primetime")
    return _insert_after_enter(text, _log_enter(m, "nprimetim"), "cpu-pef-primetime")


def patch_cpu_pef_rmvtime(text: str) -> str:
    """Auto mill 68k RmvTime 0xA059 Pascal pop 4."""
    m = _MARKERS["pef-rmvtime"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa059u) {\n"
        "						/* leftover:pef-rmvtime: RmvTime. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrmvtime;\n"
        "							if (nrmvtime < 8) {\n"
        "								nrmvtime++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF rmvtime 0xA059\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-rmvtime")
    return _insert_after_enter(text, _log_enter(m, "nrmvtime"), "cpu-pef-rmvtime")


def patch_cpu_pef_trapa190(text: str) -> str:
    """Auto mill 68k TrapA190 0xA190 Pascal pop 4."""
    m = _MARKERS["pef-trapa190"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa190u) {\n"
        "						/* leftover:pef-trapa190: TrapA190. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa190;\n"
        "							if (ntrapa190 < 8) {\n"
        "								ntrapa190++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa190 0xA190\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa190")
    return _insert_after_enter(text, _log_enter(m, "ntrapa190"), "cpu-pef-trapa190")


def patch_cpu_pef_openrgn(text: str) -> str:
    """Auto mill 68k OpenRgn 0xA8DA Pascal pop 4."""
    m = _MARKERS["pef-openrgn"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8dau) {\n"
        "						/* leftover:pef-openrgn: OpenRgn. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nopenrgn;\n"
        "							if (nopenrgn < 8) {\n"
        "								nopenrgn++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF openrgn 0xA8DA\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-openrgn")
    return _insert_after_enter(text, _log_enter(m, "nopenrgn"), "cpu-pef-openrgn")


def patch_cpu_pef_frameround(text: str) -> str:
    """Auto mill 68k FrameRoundRect 0xA8B0 Pascal pop 4."""
    m = _MARKERS["pef-frameround"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8b0u) {\n"
        "						/* leftover:pef-frameround: FrameRoundRect. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nframerou;\n"
        "							if (nframerou < 8) {\n"
        "								nframerou++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF frameround 0xA8B0\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-frameround")
    return _insert_after_enter(text, _log_enter(m, "nframerou"), "cpu-pef-frameround")


def patch_cpu_pef_closergn(text: str) -> str:
    """Auto mill 68k CloseRgn 0xA8DB Pascal pop 4."""
    m = _MARKERS["pef-closergn"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8dbu) {\n"
        "						/* leftover:pef-closergn: CloseRgn. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nclosergn;\n"
        "							if (nclosergn < 8) {\n"
        "								nclosergn++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF closergn 0xA8DB\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-closergn")
    return _insert_after_enter(text, _log_enter(m, "nclosergn"), "cpu-pef-closergn")


def patch_cpu_pef_poweroff(text: str) -> str:
    """Auto mill 68k PowerOff 0xA05B Pascal pop 4."""
    m = _MARKERS["pef-poweroff"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa05bu) {\n"
        "						/* leftover:pef-poweroff: PowerOff. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npoweroff;\n"
        "							if (npoweroff < 8) {\n"
        "								npoweroff++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF poweroff 0xA05B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-poweroff")
    return _insert_after_enter(text, _log_enter(m, "npoweroff"), "cpu-pef-poweroff")


def patch_cpu_pef_rgbforecol(text: str) -> str:
    """Auto mill 68k RGBForeColor 0xAA14 Pascal pop 4."""
    m = _MARKERS["pef-rgbforecol"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa14u) {\n"
        "						/* leftover:pef-rgbforecol: RGBForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrgbforec;\n"
        "							if (nrgbforec < 8) {\n"
        "								nrgbforec++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF rgbforecol 0xAA14\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-rgbforecol")
    return _insert_after_enter(text, _log_enter(m, "nrgbforec"), "cpu-pef-rgbforecol")


def patch_cpu_pef_getindadb(text: str) -> str:
    """Auto mill 68k GetIndADB 0xA078 Pascal pop 4."""
    m = _MARKERS["pef-getindadb"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa078u) {\n"
        "						/* leftover:pef-getindadb: GetIndADB. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetindad;\n"
        "							if (ngetindad < 8) {\n"
        "								ngetindad++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getindadb 0xA078\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getindadb")
    return _insert_after_enter(text, _log_enter(m, "ngetindad"), "cpu-pef-getindadb")


def patch_cpu_pef_insxtime(text: str) -> str:
    """Auto mill 68k InsXTime 0xA458 Pascal pop 4."""
    m = _MARKERS["pef-insxtime"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa458u) {\n"
        "						/* leftover:pef-insxtime: InsXTime. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ninsxtime;\n"
        "							if (ninsxtime < 8) {\n"
        "								ninsxtime++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF insxtime 0xA458\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-insxtime")
    return _insert_after_enter(text, _log_enter(m, "ninsxtime"), "cpu-pef-insxtime")


def patch_cpu_pef_getgdevice(text: str) -> str:
    """Auto mill 68k GetGDevice 0xAA32 Pascal pop 4."""
    m = _MARKERS["pef-getgdevice"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaa32u) {\n"
        "						/* leftover:pef-getgdevice: GetGDevice. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ngetgdevi;\n"
        "							if (ngetgdevi < 8) {\n"
        "								ngetgdevi++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF getgdevice 0xAA32\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-getgdevice")
    return _insert_after_enter(text, _log_enter(m, "ngetgdevi"), "cpu-pef-getgdevice")


def patch_cpu_pef_welqd(text: str) -> str:
    """Color Welcome QD: GetGDevice returns planted 32-bit GDHandle,
    CopyBits is ROM (un-stub leftover:pef-aa8ec pop-4), GetWMgrPort is
    InitGraf CGrafPort at RAMBase+0xa100 not Welcome 0x4e000.
    Image #1 is 1-bit STR#/DITL; Image #2 is platinum color window.
    Do not remill leftover:pef-getgdevice leftover:pef-aa8ec leftover:pef-wmgp
    leftover:pef-gdzero leftover:pef-cupgmd leftover:pef-getport.
    Do not skip 0x5c86c-0x5c8c0. Do not skip-68k."""
    m = _MARKERS["pef-welqd"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "static uint32 g3_did_welpef;\n"
        "static uint32 g3_pef_load_welcome(void)\n",
        "static uint32 g3_did_welpef;\n"
        "static uint32 g3_wel_gdh;\n"
        "static uint32 g3_pef_load_welcome(void)\n",
        "cpu-pef-welqd-gdh",
    )
    text = _replace_once(
        text,
        "	} else if (idx == 43u) {\n"
        "		static uint32 g3_wel_gdh;\n"
        "		if (g3_did_welpef && !g3_wel_gdh) {\n",
        "	} else if (idx == 43u) {\n"
        "		if (g3_did_welpef && !g3_wel_gdh) {\n",
        "cpu-pef-welqd-idx43",
    )
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa32u) {\n"
        "						/* leftover:pef-getgdevice: GetGDevice. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n",
        "					} else if (op68 == 0xaa32u) {\n"
        "						/* leftover:pef-welqd: GetGDevice return 32-bit GDHandle. */\n"
        "						{\n"
        "							extern uint32 RAMBase;\n"
        "							uint32 h = g3_wel_gdh;\n"
        "							if (!h)\n"
        "								h = RAMBase + 0xd000u;\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								vm_write_memory_4(gpr(1), h);\n"
        "							gpr(8) = 0;\n"
        "						}\n",
        "cpu-pef-welqd-gdev",
    )
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-aa8ec: CopyBits. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa8ec;\n"
        "							if (naa8ec < 8) {\n"
        "								naa8ec++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa8ec 0xA8EC\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xabe9u) {\n",
        "					} else if (op68 == 0xabe9u) {\n",
        "cpu-pef-welqd-copybits",
    )
    text = _replace_once(
        text,
        "								vm_write_memory_4(dst,\n"
        "										 RAMBase + 0x4e000u);\n",
        "								vm_write_memory_4(dst,\n"
        "										 RAMBase + 0xa100u);\n",
        "cpu-pef-welqd-wmgp",
    )
    return _insert_after_enter(text, _log_enter(m, "nwelqd"), "cpu-pef-welqd")


def patch_cpu_pef_cblit(text: str) -> str:
    """Let ROM CopyBits blit: stop bound-cap BLE exit at 0x20cc4
    and do not skip 0x20cc6-0x20ffe (the blit). leftover:pef-welqd
    KEEP with CopyBits BLE exit so FB stayed black.
    Do not skip 0x5c86c-0x5c8c0. Do not remill leftover:pef-welqd.
    Do not skip-68k."""
    m = _MARKERS["pef-cblit"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "		/* CopyBits tail + dest-RTS 0x20f36 + 2f38 $0744/$0748\n"
        "		 * JMP blit. Keep inner 0x20ca4-0x20cc4 AND skip-start\n"
        "		 * 0x20cc4. Dest 0x20ffe 2d1f. Not 0x8e770. */\n"
        "		if (off >= 0x20cc6u && off < 0x20ffeu)\n"
        "			return 0;\n",
        "		/* leftover:pef-cblit: run CopyBits blit 0x20cc6-0x20ffe. */\n",
        "cpu-pef-cblit-skip",
    )
    text = _replace_once(
        text,
        "						if (cmp_opc == ROMBase + 0x20cb0u ||\n"
        "						    cmp_opc == ROMBase + 0x20cbeu ||\n"
        "						    cmp_opc == ROMBase + 0x1faf6u ||\n"
        "						    cmp_opc == ROMBase + 0x1fb04u) {\n"
        "							gpr(8 + dn) = m;\n"
        "							d = m;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned ncbnd;\n"
        "								if (ncbnd < 8) {\n"
        "									ncbnd++;\n"
        "									nw_boot_log(\"G3: 68k CopyBits bound cap\");\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n",
        "						if (0 && (cmp_opc == ROMBase + 0x20cb0u ||\n"
        "						    cmp_opc == ROMBase + 0x20cbeu ||\n"
        "						    cmp_opc == ROMBase + 0x1faf6u ||\n"
        "						    cmp_opc == ROMBase + 0x1fb04u)) {\n"
        "							gpr(8 + dn) = m;\n"
        "							d = m;\n"
        "						}\n",
        "cpu-pef-cblit-cap",
    )
    text = _replace_once(
        text,
        "						if (bcc_opc == ROMBase + 0x20cb4u ||\n"
        "						    bcc_opc == ROMBase + 0x1fafau) {\n"
        "							take = 1;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nblex;\n"
        "								if (nblex < 8) {\n"
        "									nblex++;\n"
        "									nw_boot_log(\"G3: 68k CopyBits BLE exit 0x20cc4\");\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						} else if (bcc_opc == ROMBase + 0x20cc2u ||\n"
        "							   bcc_opc == ROMBase + 0x1fb08u)\n"
        "							take = 0;\n",
        "						if (0 && (bcc_opc == ROMBase + 0x20cb4u ||\n"
        "						    bcc_opc == ROMBase + 0x1fafau))\n"
        "							take = 1;\n"
        "						else if (bcc_opc == ROMBase + 0x20cc2u ||\n"
        "							 bcc_opc == ROMBase + 0x1fb08u)\n"
        "							take = 0;\n",
        "cpu-pef-cblit-ble",
    )
    return _insert_after_enter(text, _log_enter(m, "ncblit"), "cpu-pef-cblit")


def patch_cpu_pef_cbhost(text: str) -> str:
    """Host CopyBits A8EC: blit src pixmap/bitmap into guest FB.
    leftover:pef-cblit REVERT (ROM blit hang 04cecd36/G2).
    leftover:pef-welqd removed leftover:pef-aa8ec stub. Pascal pop 30.
    Do not remill leftover:pef-cblit leftover:pef-aa8ec leftover:pef-welqd.
    Do not skip 0x5c86c-0x5c8c0. Do not skip-68k."""
    m = _MARKERS["pef-cbhost"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xabe9u) {\n"
        "						/* leftover:pef-trapabe9: TrapABE9. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-cbhost: CopyBits to guest FB. Pascal pop 30. */\n"
        "						{\n"
        "							uint32 sp = gpr(1);\n"
        "							uint32 src_bm = 0, dst_bm = 0;\n"
        "							int16 sr_t = 0, sr_l = 0, sr_b = 0, sr_r = 0;\n"
        "							int16 dr_t = 0, dr_l = 0, dr_b = 0, dr_r = 0;\n"
        "							if (g3_ea_data(sp + 29u)) {\n"
        "								dr_t = (int16)vm_read_memory_2(sp + 6u);\n"
        "								dr_l = (int16)vm_read_memory_2(sp + 8u);\n"
        "								dr_b = (int16)vm_read_memory_2(sp + 10u);\n"
        "								dr_r = (int16)vm_read_memory_2(sp + 12u);\n"
        "								sr_t = (int16)vm_read_memory_2(sp + 14u);\n"
        "								sr_l = (int16)vm_read_memory_2(sp + 16u);\n"
        "								sr_b = (int16)vm_read_memory_2(sp + 18u);\n"
        "								sr_r = (int16)vm_read_memory_2(sp + 20u);\n"
        "								dst_bm = vm_read_memory_4(sp + 22u);\n"
        "								src_bm = vm_read_memory_4(sp + 26u);\n"
        "							}\n"
        "							if (src_bm && dst_bm &&\n"
        "							    g3_ea_data(src_bm + 5u) &&\n"
        "							    g3_ea_data(dst_bm + 5u)) {\n"
        "								const uint32 fb = g3_qd_fb();\n"
        "								uint32 sbase = vm_read_memory_4(src_bm);\n"
        "								uint32 dbase = vm_read_memory_4(dst_bm);\n"
        "								uint32 srow = (uint32)(vm_read_memory_2(src_bm + 4u) & 0x3fffu);\n"
        "								uint32 drow = (uint32)(vm_read_memory_2(dst_bm + 4u) & 0x3fffu);\n"
        "								int w = (int)dr_r - (int)dr_l;\n"
        "								int h = (int)dr_b - (int)dr_t;\n"
        "								int sw = (int)sr_r - (int)sr_l;\n"
        "								int sh = (int)sr_b - (int)sr_t;\n"
        "								if (sw > 0 && sw < w) w = sw;\n"
        "								if (sh > 0 && sh < h) h = sh;\n"
        "								if (w > 640) w = 640;\n"
        "								if (h > 480) h = 480;\n"
        "								if (w > 0 && h > 0 && srow && drow &&\n"
        "								    dbase && sbase &&\n"
        "								    (dbase == fb || drow >= 2560u)) {\n"
        "									unsigned y, x;\n"
        "									for (y = 0; y < (unsigned)h; y++) {\n"
        "										if (srow >= 2560u && drow >= 2560u) {\n"
        "											uint32 s = sbase + (uint32)((int)sr_t + (int)y) * srow + (uint32)sr_l * 4u;\n"
        "											uint32 d = dbase + (uint32)((int)dr_t + (int)y) * drow + (uint32)dr_l * 4u;\n"
        "											for (x = 0; x < (unsigned)w; x++) {\n"
        "												if (g3_ea_data(s + 3u) && g3_ea_data(d + 3u))\n"
        "													vm_write_memory_4(d, vm_read_memory_4(s));\n"
        "												s += 4u;\n"
        "												d += 4u;\n"
        "											}\n"
        "										} else if (drow >= 2560u) {\n"
        "											uint32 s = sbase + (uint32)((int)sr_t + (int)y) * srow + (uint32)((int)sr_l / 8);\n"
        "											uint32 d = dbase + (uint32)((int)dr_t + (int)y) * drow + (uint32)dr_l * 4u;\n"
        "											for (x = 0; x < (unsigned)w; x++) {\n"
        "												uint32 bitoff = (uint32)sr_l + x;\n"
        "												uint8 by = 0;\n"
        "												if (g3_ea_data(sbase + (uint32)((int)sr_t + (int)y) * srow + bitoff / 8u))\n"
        "													by = vm_read_memory_1(sbase + (uint32)((int)sr_t + (int)y) * srow + bitoff / 8u);\n"
        "												uint32 pix = (by & (uint8)(0x80u >> (bitoff & 7u))) ? 0x00ffffffu : 0;\n"
        "												if (g3_ea_data(d + 3u))\n"
        "													vm_write_memory_4(d, pix);\n"
        "												d += 4u;\n"
        "											}\n"
        "										}\n"
        "									}\n"
        "								}\n"
        "							}\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 30u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned ncbhost;\n"
        "								if (ncbhost < 8) {\n"
        "									char buf[128];\n"
        "									ncbhost++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF cbhost s=%08x d=%08x\",\n"
        "										 (unsigned)src_bm, (unsigned)dst_bm);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xabe9u) {\n"
        "						/* leftover:pef-trapabe9: TrapABE9. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cbhost")
    return _insert_after_enter(text, _log_enter(m, "ncbhost"), "cpu-pef-cbhost")


def patch_cpu_pef_powermgrdi(text: str) -> str:
    """Auto mill 68k PowerMgrDispatch 0xA09E Pascal pop 4."""
    m = _MARKERS["pef-powermgrdi"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa09eu) {\n"
        "						/* leftover:pef-powermgrdi: PowerMgrDispatch. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npowermgr;\n"
        "							if (npowermgr < 8) {\n"
        "								npowermgr++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF powermgrdi 0xA09E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-powermgrdi")
    return _insert_after_enter(text, _log_enter(m, "npowermgr"), "cpu-pef-powermgrdi")


def patch_cpu_pef_newgestalt(text: str) -> str:
    """Auto mill 68k NewGestalt 0xA3AD Pascal pop 4."""
    m = _MARKERS["pef-newgestalt"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa3adu) {\n"
        "						/* leftover:pef-newgestalt: NewGestalt. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nnewgesta;\n"
        "							if (nnewgesta < 8) {\n"
        "								nnewgesta++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF newgestalt 0xA3AD\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-newgestalt")
    return _insert_after_enter(text, _log_enter(m, "nnewgesta"), "cpu-pef-newgestalt")


def patch_cpu_pef_sizersrc(text: str) -> str:
    """Auto mill 68k SizeRsrc 0xA9A5 Pascal pop 4."""
    m = _MARKERS["pef-sizersrc"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9a5u) {\n"
        "						/* leftover:pef-sizersrc: SizeRsrc. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsizersrc;\n"
        "							if (nsizersrc < 8) {\n"
        "								nsizersrc++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF sizersrc 0xA9A5\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-sizersrc")
    return _insert_after_enter(text, _log_enter(m, "nsizersrc"), "cpu-pef-sizersrc")


def patch_cpu_pef_trapa098(text: str) -> str:
    """Auto mill 68k TrapA098 0xA098 Pascal pop 4."""
    m = _MARKERS["pef-trapa098"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa098u) {\n"
        "						/* leftover:pef-trapa098: TrapA098. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntrapa098;\n"
        "							if (ntrapa098 < 8) {\n"
        "								ntrapa098++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF trapa098 0xA098\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-trapa098")
    return _insert_after_enter(text, _log_enter(m, "ntrapa098"), "cpu-pef-trapa098")


def patch_cpu_pef_setgrowzon(text: str) -> str:
    """Auto mill 68k SetGrowZone 0xA04B Pascal pop 4."""
    m = _MARKERS["pef-setgrowzon"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa04bu) {\n"
        "						/* leftover:pef-setgrowzon: SetGrowZone. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetgrowz;\n"
        "							if (nsetgrowz < 8) {\n"
        "								nsetgrowz++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setgrowzon 0xA04B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-setgrowzon")
    return _insert_after_enter(text, _log_enter(m, "nsetgrowz"), "cpu-pef-setgrowzon")


def patch_cpu_pef_loadscrap(text: str) -> str:
    """Auto mill 68k LoadScrap 0xA9FB Pascal pop 4."""
    m = _MARKERS["pef-loadscrap"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa9fbu) {\n"
        "						/* leftover:pef-loadscrap: LoadScrap. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nloadscra;\n"
        "							if (nloadscra < 8) {\n"
        "								nloadscra++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF loadscrap 0xA9FB\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-loadscrap")
    return _insert_after_enter(text, _log_enter(m, "nloadscra"), "cpu-pef-loadscrap")


def patch_cpu_pef_aa015(text: str) -> str:
    """Auto mill 68k SetVol 0xA015 Pascal pop 4."""
    m = _MARKERS["pef-aa015"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa015u) {\n"
        "						/* leftover:pef-aa015: SetVol. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa015;\n"
        "							if (naa015 < 8) {\n"
        "								naa015++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa015 0xA015\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa015")
    return _insert_after_enter(text, _log_enter(m, "naa015"), "cpu-pef-aa015")


def patch_cpu_pef_aa03b(text: str) -> str:
    """Auto mill 68k Delay 0xA03B Pascal pop 4."""
    m = _MARKERS["pef-aa03b"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa03bu) {\n"
        "						/* leftover:pef-aa03b: Delay. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naa03b;\n"
        "							if (naa03b < 8) {\n"
        "								naa03b++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF aa03b 0xA03B\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-aa03b")
    return _insert_after_enter(text, _log_enter(m, "naa03b"), "cpu-pef-aa03b")


def patch_cpu_pef_mixedmodem(text: str) -> str:
    """Auto mill 68k MixedModeMagic 0xAAFE Pascal pop 4."""
    m = _MARKERS["pef-mixedmodem"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xaafeu) {\n"
        "						/* leftover:pef-mixedmodem: MixedModeMagic. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nmixedmod;\n"
        "							if (nmixedmod < 8) {\n"
        "								nmixedmod++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF mixedmodem 0xAAFE\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-mixedmodem")
    return _insert_after_enter(text, _log_enter(m, "nmixedmod"), "cpu-pef-mixedmodem")


def patch_cpu_pef_a8ecp(text: str) -> str:
    """leftover:pef-mixedmodem KEEP rec: CopyBits A8EC unhosted
    (leftover:pef-welqd removed leftover:pef-aa8ec). leftover:pef-cblit
    leftover:pef-cbhost REVERT. Pascal pop 30, no ROM blit, no host blit.
    Do not remill leftover:pef-cblit leftover:pef-cbhost leftover:pef-aa8ec
    leftover:pef-welqd leftover:pef-mixedmodem leftover:pef-powermgrdi.
    Do not skip 0x5c86c-0x5c8c0. Do not skip-68k."""
    m = _MARKERS["pef-a8ecp"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xabe9u) {\n"
        "						/* leftover:pef-trapabe9: TrapABE9. Pascal pop 4. */\n"
    )
    new = (
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-a8ecp: CopyBits. Pascal pop 30. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 30u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned na8ecp;\n"
        "							if (na8ecp < 8) {\n"
        "								na8ecp++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF a8ecp 0xA8EC\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xabe9u) {\n"
        "						/* leftover:pef-trapabe9: TrapABE9. Pascal pop 4. */\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-a8ecp")
    return _insert_after_enter(text, _log_enter(m, "na8ecp"), "cpu-pef-a8ecp")


def patch_cpu_pef_paintrm(text: str) -> str:
    """leftover:pef-a8ecp leftover:pef-welqd KEEP rec: CopyBits A-trap
    hosted; un-stub leftover:pef-paintone so ROM PaintOne A90C paints
    window chrome. Do not remill leftover:pef-paintone leftover:pef-a8ecp
    leftover:pef-cblit leftover:pef-cbhost leftover:pef-aa8ec leftover:pef-welqd.
    Do not skip 0x5c86c-0x5c8c0. Do not skip-68k."""
    m = _MARKERS["pef-paintrm"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa90cu) {\n"
        "						/* leftover:pef-paintone: PaintOne. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npaintone;\n"
        "							if (npaintone < 8) {\n"
        "								npaintone++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF paintone 0xA90C\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa898u) {\n",
        "					} else if (op68 == 0xa898u) {\n",
        "cpu-pef-paintrm",
    )
    return _insert_after_enter(text, _log_enter(m, "npaintrm"), "cpu-pef-paintrm")


def patch_cpu_pef_setprm(text: str) -> str:
    """Un-stub leftover:pef-setpbits A875 so ROM SetPortBits runs.
    Do not remill leftover:pef-setpbits. Do not skip 0x5c86c-0x5c8c0."""
    m = _MARKERS["pef-setprm"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa875u) {\n"
        "						/* leftover:pef-setpbits: SetPBits. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetpbits;\n"
        "							if (nsetpbits < 8) {\n"
        "								nsetpbits++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setpbits 0xA875\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa8ecu) {\n",
        "					} else if (op68 == 0xa8ecu) {\n",
        "cpu-pef-setprm",
    )
    return _insert_after_enter(text, _log_enter(m, "nsetprm"), "cpu-pef-setprm")


def patch_cpu_pef_cb32(text: str) -> str:
    """Host CopyBits A8EC: 32-bit src to 32-bit dst into g3_qd_fb() only.
    Replaces leftover:pef-a8ecp pop-30 stub. leftover:pef-cbhost REVERT
    (msr-collapse). Pascal pop 30, one rect, max 640x480, per-row bounds
    only — no 1-bit expand, no per-pixel g3_ea_data storm.
    Do not remill leftover:pef-a8ecp leftover:pef-cbhost leftover:pef-cblit
    leftover:pef-aa8ec leftover:pef-welqd. Do not skip 0x5c86c-0x5c8c0."""
    m = _MARKERS["pef-cb32"]
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-a8ecp: CopyBits. Pascal pop 30. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 30u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned na8ecp;\n"
        "							if (na8ecp < 8) {\n"
        "								na8ecp++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF a8ecp 0xA8EC\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xabe9u) {\n"
    )
    new = (
        "					} else if (op68 == 0xa8ecu) {\n"
        "						/* leftover:pef-cb32: CopyBits 32->32 to guest FB. Pascal pop 30. */\n"
        "						{\n"
        "							uint32 sp = gpr(1);\n"
        "							uint32 src_bm = 0, dst_bm = 0;\n"
        "							int16 sr_t = 0, sr_l = 0, sr_b = 0, sr_r = 0;\n"
        "							int16 dr_t = 0, dr_l = 0, dr_b = 0, dr_r = 0;\n"
        "							int cb_w = 0, cb_h = 0;\n"
        "							if (g3_ea_data(sp + 29u)) {\n"
        "								dr_t = (int16)vm_read_memory_2(sp + 6u);\n"
        "								dr_l = (int16)vm_read_memory_2(sp + 8u);\n"
        "								dr_b = (int16)vm_read_memory_2(sp + 10u);\n"
        "								dr_r = (int16)vm_read_memory_2(sp + 12u);\n"
        "								sr_t = (int16)vm_read_memory_2(sp + 14u);\n"
        "								sr_l = (int16)vm_read_memory_2(sp + 16u);\n"
        "								sr_b = (int16)vm_read_memory_2(sp + 18u);\n"
        "								sr_r = (int16)vm_read_memory_2(sp + 20u);\n"
        "								dst_bm = vm_read_memory_4(sp + 22u);\n"
        "								src_bm = vm_read_memory_4(sp + 26u);\n"
        "							}\n"
        "							if (src_bm && dst_bm &&\n"
        "							    g3_ea_data(src_bm + 5u) &&\n"
        "							    g3_ea_data(dst_bm + 5u)) {\n"
        "								const uint32 fb = g3_qd_fb();\n"
        "								uint32 sbase = vm_read_memory_4(src_bm);\n"
        "								uint32 dbase = vm_read_memory_4(dst_bm);\n"
        "								uint32 srow = (uint32)(vm_read_memory_2(src_bm + 4u) & 0x3fffu);\n"
        "								uint32 drow = (uint32)(vm_read_memory_2(dst_bm + 4u) & 0x3fffu);\n"
        "								cb_w = (int)dr_r - (int)dr_l;\n"
        "								cb_h = (int)dr_b - (int)dr_t;\n"
        "								int sw = (int)sr_r - (int)sr_l;\n"
        "								int sh = (int)sr_b - (int)sr_t;\n"
        "								if (sw > 0 && sw < cb_w) cb_w = sw;\n"
        "								if (sh > 0 && sh < cb_h) cb_h = sh;\n"
        "								if (cb_w > 640) cb_w = 640;\n"
        "								if (cb_h > 480) cb_h = 480;\n"
        "								if (cb_w > 0 && cb_h > 0 && srow >= 2560u &&\n"
        "								    drow >= 2560u && sbase && dbase &&\n"
        "								    (dbase == fb || drow >= 2560u)) {\n"
        "									unsigned y, x;\n"
        "									for (y = 0; y < (unsigned)cb_h; y++) {\n"
        "										uint32 s = sbase + (uint32)((int)sr_t + (int)y) * srow + (uint32)sr_l * 4u;\n"
        "										uint32 d = dbase + (uint32)((int)dr_t + (int)y) * drow + (uint32)dr_l * 4u;\n"
        "										if (!g3_ea_data(s + (uint32)cb_w * 4u - 1u) ||\n"
        "										    !g3_ea_data(d + (uint32)cb_w * 4u - 1u))\n"
        "											continue;\n"
        "										for (x = 0; x < (unsigned)cb_w; x++) {\n"
        "											vm_write_memory_4(d, vm_read_memory_4(s));\n"
        "											s += 4u;\n"
        "											d += 4u;\n"
        "										}\n"
        "									}\n"
        "								}\n"
        "							}\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 30u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned ncb32;\n"
        "								if (ncb32 < 8) {\n"
        "									char buf[160];\n"
        "									ncb32++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF cb32 s=%08x d=%08x w=%d h=%d\",\n"
        "										 (unsigned)src_bm, (unsigned)dst_bm,\n"
        "										 cb_w, cb_h);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "					} else if (op68 == 0xabe9u) {\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cb32")
    return _insert_after_enter(text, _log_enter(m, "ncb32"), "cpu-pef-cb32")


def patch_cpu_pef_fgcol(text: str) -> str:
    """Un-stub leftover:pef-forecolor A862 so ROM ForeColor runs."""
    m = _MARKERS["pef-fgcol"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa862u) {\n"
        "						/* leftover:pef-forecolor: ForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nforecolo;\n"
        "							if (nforecolo < 8) {\n"
        "								nforecolo++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF forecolor 0xA862\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa863u) {\n",
        "					} else if (op68 == 0xa863u) {\n",
        "cpu-pef-fgcol",
    )
    return _insert_after_enter(text, _log_enter(m, "nfgcol"), "cpu-pef-fgcol")


def patch_cpu_pef_bgcol(text: str) -> str:
    """Un-stub leftover:pef-backcolor A863 so ROM BackColor runs."""
    m = _MARKERS["pef-bgcol"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa863u) {\n"
        "						/* leftover:pef-backcolor: BackColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nbackcolo;\n"
        "							if (nbackcolo < 8) {\n"
        "								nbackcolo++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF backcolor 0xA863\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa02du) {\n",
        "					} else if (op68 == 0xa02du) {\n",
        "cpu-pef-bgcol",
    )
    return _insert_after_enter(text, _log_enter(m, "nbgcol"), "cpu-pef-bgcol")


def patch_cpu_pef_rgbfg(text: str) -> str:
    """Un-stub leftover:pef-rgbforecol AA14 so ROM RGBForeColor runs."""
    m = _MARKERS["pef-rgbfg"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa14u) {\n"
        "						/* leftover:pef-rgbforecol: RGBForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrgbforec;\n"
        "							if (nrgbforec < 8) {\n"
        "								nrgbforec++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF rgbforecol 0xAA14\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa078u) {\n",
        "					} else if (op68 == 0xa078u) {\n",
        "cpu-pef-rgbfg",
    )
    return _insert_after_enter(text, _log_enter(m, "nrgbfg"), "cpu-pef-rgbfg")


def patch_cpu_pef_ltglo(text: str) -> str:
    """Un-stub leftover:pef-localtoglo A870 so ROM LocalToGlobal runs."""
    m = _MARKERS["pef-ltglo"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa870u) {\n"
        "						/* leftover:pef-localtoglo: LocalToGlobal. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nlocaltog;\n"
        "							if (nlocaltog < 8) {\n"
        "								nlocaltog++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF localtoglo 0xA870\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa875u) {\n",
        "					} else if (op68 == 0xa875u) {\n",
        "cpu-pef-ltglo",
    )
    return _insert_after_enter(text, _log_enter(m, "nltglo"), "cpu-pef-ltglo")


def patch_cpu_pef_setcrm(text: str) -> str:
    """Un-stub leftover:pef-setc A879 so ROM SetClip runs."""
    m = _MARKERS["pef-setcrm"]
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xa879u) {\n"
        "						/* leftover:pef-setc: SetClip RgnHandle. Pascal pop 4. */\n"
        "						{\n"
        "							uint32 rgn = 0;\n"
        "							if (g3_ea_data(gpr(1) + 3u))\n"
        "								rgn = vm_read_memory_4(gpr(1));\n"
        "							if (g3_ea_data(gpr(1)))\n"
        "								gpr(1) += 4u;\n"
        "							gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nsetc;\n"
        "								if (nsetc < 8) {\n"
        "									char buf[96];\n"
        "									nsetc++;\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF setc rgn=%08x\",\n"
        "										 (unsigned)rgn);\n"
        "									nw_boot_log(buf);\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						}\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetc0;\n"
        "							if (nsetc0 < 8) {\n"
        "								nsetc0++;\n"
        "								nw_boot_log(\"G3: 68k SetClip A879\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa8a1u) {\n",
        "					} else if (op68 == 0xa8a1u) {\n",
        "cpu-pef-setcrm",
    )
    return _insert_after_enter(text, _log_enter(m, "nsetcrm"), "cpu-pef-setcrm")


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
    "pef-skipditl": patch_cpu_pef_skipditl,
    "pef-wplug802": patch_cpu_pef_wplug802,
    "pef-cupgnd": patch_cpu_pef_cupgnd,
    "pef-ditlh": patch_cpu_pef_ditlh,
    "pef-cupaff": patch_cpu_pef_cupaff,
    "pef-cupabd": patch_cpu_pef_cupabd,
    "pef-cupnctl": patch_cpu_pef_cupnctl,
    "pef-ctlown": patch_cpu_pef_ctlown,
    "pef-ctllst": patch_cpu_pef_ctllst,
    "pef-str1050": patch_cpu_pef_str1050,
    "pef-impcap": patch_cpu_pef_impcap,
    "pef-impwel": patch_cpu_pef_impwel,
    "pef-wrefcon": patch_cpu_pef_wrefcon,
    "pef-swtitle": patch_cpu_pef_swtitle,
    "pef-str520": patch_cpu_pef_str520,
    "pef-s520b": patch_cpu_pef_s520b,
    "pef-sub2050": patch_cpu_pef_sub2050,
    "pef-novid": patch_cpu_pef_novid,
    "pef-npicv": patch_cpu_pef_npicv,
    "pef-sdit2050": patch_cpu_pef_sdit2050,
    "pef-subfit": patch_cpu_pef_subfit,
    "pef-alrt1": patch_cpu_pef_alrt1,
    "pef-g520a3": patch_cpu_pef_g520a3,
    "pef-lowisi": patch_cpu_pef_lowisi,
    "pef-low68k": patch_cpu_pef_low68k,
    "pef-welloop": patch_cpu_pef_welloop,
    "pef-lowblr": patch_cpu_pef_lowblr,
    "pef-lowdata": patch_cpu_pef_lowdata,
    "pef-lowoff": patch_cpu_pef_lowoff,
    "pef-gdblr": patch_cpu_pef_gdblr,
    "pef-gdtbl": patch_cpu_pef_gdtbl,
    "pef-gndalrt": patch_cpu_pef_gndalrt,
    "pef-gdzero": patch_cpu_pef_gdzero,
    "pef-b0host": patch_cpu_pef_b0host,
    "pef-b0ptr": patch_cpu_pef_b0ptr,
    "pef-b0ptxt": patch_cpu_pef_b0ptxt,
    "pef-gnd501": patch_cpu_pef_gnd501,
    "pef-b0alrt": patch_cpu_pef_b0alrt,
    "pef-welmd": patch_cpu_pef_welmd,
    "pef-mdloop": patch_cpu_pef_mdloop,
    "pef-mdret": patch_cpu_pef_mdret,
    "pef-mdtoc": patch_cpu_pef_mdtoc,
    "pef-a991c": patch_cpu_pef_a991c,
    "pef-srl": patch_cpu_pef_srl,
    "pef-hunl": patch_cpu_pef_hunl,
    "pef-sutil": patch_cpu_pef_sutil,
    "pef-cntmi": patch_cpu_pef_cntmi,
    "pef-cntres": patch_cpu_pef_cntres,
    "pef-cmsz": patch_cpu_pef_cmsz,
    "pef-mnud": patch_cpu_pef_mnud,
    "pef-nhc": patch_cpu_pef_nhc,
    "pef-a029": patch_cpu_pef_a029,
    "pef-qdext": patch_cpu_pef_qdext,
    "pef-wmgp": patch_cpu_pef_wmgp,
    "pef-setc": patch_cpu_pef_setc,
    "pef-mixedmodem": patch_cpu_pef_mixedmodem,
    "pef-aa03b": patch_cpu_pef_aa03b,
    "pef-aa015": patch_cpu_pef_aa015,
    "pef-loadscrap": patch_cpu_pef_loadscrap,
    "pef-setgrowzon": patch_cpu_pef_setgrowzon,
    "pef-trapa098": patch_cpu_pef_trapa098,
    "pef-sizersrc": patch_cpu_pef_sizersrc,
    "pef-newgestalt": patch_cpu_pef_newgestalt,
    "pef-powermgrdi": patch_cpu_pef_powermgrdi,
    "pef-getgdevice": patch_cpu_pef_getgdevice,
    "pef-welqd": patch_cpu_pef_welqd,
    "pef-cblit": patch_cpu_pef_cblit,
    "pef-cbhost": patch_cpu_pef_cbhost,
    "pef-a8ecp": patch_cpu_pef_a8ecp,
    "pef-paintrm": patch_cpu_pef_paintrm,
    "pef-setprm": patch_cpu_pef_setprm,
    "pef-cb32": patch_cpu_pef_cb32,
    "pef-fgcol": patch_cpu_pef_fgcol,
    "pef-bgcol": patch_cpu_pef_bgcol,
    "pef-rgbfg": patch_cpu_pef_rgbfg,
    "pef-ltglo": patch_cpu_pef_ltglo,
    "pef-setcrm": patch_cpu_pef_setcrm,
    "pef-insxtime": patch_cpu_pef_insxtime,
    "pef-getindadb": patch_cpu_pef_getindadb,
    "pef-rgbforecol": patch_cpu_pef_rgbforecol,
    "pef-poweroff": patch_cpu_pef_poweroff,
    "pef-closergn": patch_cpu_pef_closergn,
    "pef-frameround": patch_cpu_pef_frameround,
    "pef-openrgn": patch_cpu_pef_openrgn,
    "pef-trapa190": patch_cpu_pef_trapa190,
    "pef-rmvtime": patch_cpu_pef_rmvtime,
    "pef-primetime": patch_cpu_pef_primetime,
    "pef-setadbinfo": patch_cpu_pef_setadbinfo,
    "pef-writexpram": patch_cpu_pef_writexpram,
    "pef-internalwa": patch_cpu_pef_internalwa,
    "pef-nmremove": patch_cpu_pef_nmremove,
    "pef-nminstall": patch_cpu_pef_nminstall,
    "pef-writeparam": patch_cpu_pef_writeparam,
    "pef-releaseres": patch_cpu_pef_releaseres,
    "pef-loadresour": patch_cpu_pef_loadresour,
    "pef-get1resour": patch_cpu_pef_get1resour,
    "pef-useresfile": patch_cpu_pef_useresfile,
    "pef-curresfile": patch_cpu_pef_curresfile,
    "pef-comparestr": patch_cpu_pef_comparestr,
    "pef-drvrinstal": patch_cpu_pef_drvrinstal,
    "pef-detachreso": patch_cpu_pef_detachreso,
    "pef-atamgr": patch_cpu_pef_atamgr,
    "pef-devicemgr": patch_cpu_pef_devicemgr,
    "pef-tickcount": patch_cpu_pef_tickcount,
    "pef-drawmenuba": patch_cpu_pef_drawmenuba,
    "pef-setemptyrg": patch_cpu_pef_setemptyrg,
    "pef-emptyrgn": patch_cpu_pef_emptyrgn,
    "pef-getwvarian": patch_cpu_pef_getwvarian,
    "pef-setpenstat": patch_cpu_pef_setpenstat,
    "pef-testdevice": patch_cpu_pef_testdevice,
    "pef-getpenstat": patch_cpu_pef_getpenstat,
    "pef-paintone": patch_cpu_pef_paintone,
    "pef-invalrgn": patch_cpu_pef_invalrgn,
    "pef-putscrap": patch_cpu_pef_putscrap,
    "pef-getscrap": patch_cpu_pef_getscrap,
    "pef-ptrtohand": patch_cpu_pef_ptrtohand,
    "pef-trapaa7f": patch_cpu_pef_trapaa7f,
    "pef-decstr68k": patch_cpu_pef_decstr68k,
    "pef-trapa250": patch_cpu_pef_trapa250,
    "pef-trapa23c": patch_cpu_pef_trapa23c,
    "pef-trapa413": patch_cpu_pef_trapa413,
    "pef-aa013": patch_cpu_pef_aa013,
    "pef-trapa411": patch_cpu_pef_trapa411,
    "pef-trapa412": patch_cpu_pef_trapa412,
    "pef-trapa410": patch_cpu_pef_trapa410,
    "pef-trapa608": patch_cpu_pef_trapa608,
    "pef-trapa60d": patch_cpu_pef_trapa60d,
    "pef-hsetfilein": patch_cpu_pef_hsetfilein,
    "pef-trapa60c": patch_cpu_pef_trapa60c,
    "pef-trapa607": patch_cpu_pef_trapa607,
    "pef-hgetvinfo": patch_cpu_pef_hgetvinfo,
    "pef-trapa660": patch_cpu_pef_trapa660,
    "pef-trapa401": patch_cpu_pef_trapa401,
    "pef-trapa609": patch_cpu_pef_trapa609,
    "pef-trapa600": patch_cpu_pef_trapa600,
    "pef-hopen": patch_cpu_pef_hopen,
    "pef-getvolinfo": patch_cpu_pef_getvolinfo,
    "pef-hcreateres": patch_cpu_pef_hcreateres,
    "pef-hopenresfi": patch_cpu_pef_hopenresfi,
    "pef-hrename": patch_cpu_pef_hrename,
    "pef-hrstflock": patch_cpu_pef_hrstflock,
    "pef-hsetflock": patch_cpu_pef_hsetflock,
    "pef-hgetfilein": patch_cpu_pef_hgetfilein,
    "pef-hdelete": patch_cpu_pef_hdelete,
    "pef-hcreate": patch_cpu_pef_hcreate,
    "pef-hopenrf": patch_cpu_pef_hopenrf,
    "pef-hfsdispatc": patch_cpu_pef_hfsdispatc,
    "pef-uprstring": patch_cpu_pef_uprstring,
    "pef-aa893": patch_cpu_pef_aa893,
    "pef-textwidth": patch_cpu_pef_textwidth,
    "pef-aa8dc": patch_cpu_pef_aa8dc,
    "pef-getcwmgrpo": patch_cpu_pef_getcwmgrpo,
    "pef-setdevicea": patch_cpu_pef_setdevicea,
    "pef-newgdevice": patch_cpu_pef_newgdevice,
    "pef-blockmove": patch_cpu_pef_blockmove,
    "pef-syserror": patch_cpu_pef_syserror,
    "pef-trapa403": patch_cpu_pef_trapa403,
    "pef-trapa402": patch_cpu_pef_trapa402,
    "pef-aa15c": patch_cpu_pef_aa15c,
    "pef-memorydisp": patch_cpu_pef_memorydisp,
    "pef-setptrsize": patch_cpu_pef_setptrsize,
    "pef-control": patch_cpu_pef_control,
    "pef-newhandle": patch_cpu_pef_newhandle,
    "pef-sethandles": patch_cpu_pef_sethandles,
    "pef-blockmoved": patch_cpu_pef_blockmoved,
    "pef-initutil": patch_cpu_pef_initutil,
    "pef-moremaster": patch_cpu_pef_moremaster,
    "pef-status": patch_cpu_pef_status,
    "pef-powerdispa": patch_cpu_pef_powerdispa,
    "pef-gettooltra": patch_cpu_pef_gettooltra,
    "pef-getostrapa": patch_cpu_pef_getostrapa,
    "pef-trapabe9": patch_cpu_pef_trapabe9,
    "pef-aa8ec": patch_cpu_pef_aa8ec,
    "pef-setpbits": patch_cpu_pef_setpbits,
    "pef-localtoglo": patch_cpu_pef_localtoglo,
    "pef-getzone": patch_cpu_pef_getzone,
    "pef-disposehan": patch_cpu_pef_disposehan,
    "pef-aa874": patch_cpu_pef_aa874,
    "pef-saverestor": patch_cpu_pef_saverestor,
    "pef-setappllim": patch_cpu_pef_setappllim,
    "pef-backcolor": patch_cpu_pef_backcolor,
    "pef-forecolor": patch_cpu_pef_forecolor,
    "pef-debugger": patch_cpu_pef_debugger,
    "pef-disposeptr": patch_cpu_pef_disposeptr,
    "pef-readxpram": patch_cpu_pef_readxpram,
    "pef-open": patch_cpu_pef_open,
    "pef-aa8d8": patch_cpu_pef_aa8d8,
    "pef-unionrgn": patch_cpu_pef_unionrgn,
    "pef-a8e0": patch_cpu_pef_a8e0,
    "pef-fillc": patch_cpu_pef_fillc,
    "pef-layer": patch_cpu_pef_layer,
}

from mill_pef_qdloop import QDLOOP_HOSTS, QDLOOP_PATCH as _QDLOOP_PATCH

_HOSTS.extend(QDLOOP_HOSTS)
assert len(_HOSTS) == 254, len(_HOSTS)
PEF_OPEN_KINDS.extend(k for k, _ in QDLOOP_HOSTS)
for _qk, _qt in QDLOOP_HOSTS:
    _MARKERS[_qk] = "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % _qt
_PATCH.update(_QDLOOP_PATCH)


def apply_pef_openq(kind: str, cpu: Path) -> None:
    fn = _PATCH.get(kind)
    if fn is None:
        raise ValueError("unknown pef open kind %s" % kind)
    cpu.write_text(fn(cpu.read_text()))
