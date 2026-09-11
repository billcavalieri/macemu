"""QD-path loop mills: cb32 stack fixes and un-stub traps for Welcome window."""
from __future__ import annotations

from typing import Callable, Dict, List, Tuple

_ENTER = "	g3_did_pef_enter = 1;\n"


def _marker(tok: str) -> str:
    return "G3: 68k Launch A9F2 CFM Upgrader PEF %s" % tok


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

QDLOOP_HOSTS: List[Tuple[str, str]] = [
    ("pef-cb32b", "cb32b"),
    ("pef-cb32c", "cb32c"),
    ("pef-gportrm", "gportrm"),
    ("pef-movetrm", "movetrm"),
    ("pef-fillcrm", "fillcrm"),
    ("pef-offsetrm", "offsetrm"),
    ("pef-unionrm", "unionrm"),
    ("pef-newrgnrm", "newrgnrm"),
    ("pef-invalrm", "invalrm"),
    ("pef-saversm", "saversm"),
    ("pef-textwrm", "textwrm"),
    ("pef-copyrgnrm", "copyrgnrm"),
    ("pef-getpenrm", "getpenrm"),
    ("pef-setpenrm", "setpenrm"),
    ("pef-rgbfgh", "rgbfgh"),
    ("pef-cb32d", "cb32d"),
    ("pef-cb32e", "cb32e"),
    ("pef-cb32f", "cb32f"),
    ("pef-openrgnrm", "openrgnrm"),
    ("pef-closergnrm", "closergnrm"),
    ("pef-frameroundrm", "frameroundrm"),
    ("pef-emptyrgnrm", "emptyrgnrm"),
    ("pef-getwvarrm", "getwvarrm"),
    ("pef-setemptyrm", "setemptyrm"),
    ("pef-drawmenurm", "drawmenurm"),
    ("pef-tickcountrm", "tickcountrm"),
    ("pef-cwmgrprm", "cwmgrprm"),
    ("pef-setemptyrst", "setemptyrst"),
    ("pef-menubarst", "menubarst"),
    ("pef-restore430", "restore430"),
]

# Pascal CopyBits param block at SP (pop 30): src,dst rects, mode, maskRgn
_CB32_BODY = (
    "					} else if (op68 == 0xa8ecu) {\n"
    "						/* leftover:pef-{tag}: CopyBits 32->32 to guest FB. Pascal pop 30. */\n"
    "						{\n"
    "							uint32 sp = gpr(1);\n"
    "							uint32 src_bm = 0, dst_bm = 0;\n"
    "							int16 sr_t = 0, sr_l = 0, sr_b = 0, sr_r = 0;\n"
    "							int16 dr_t = 0, dr_l = 0, dr_b = 0, dr_r = 0;\n"
    "							int cb_w = 0, cb_h = 0;\n"
    "							uint32 dbase = 0;\n"
    "							if (g3_ea_data(sp + 29u)) {\n"
    "								src_bm = vm_read_memory_4(sp + 0u);\n"
    "								dst_bm = vm_read_memory_4(sp + 4u);\n"
    "								sr_t = (int16)vm_read_memory_2(sp + 8u);\n"
    "								sr_l = (int16)vm_read_memory_2(sp + 10u);\n"
    "								sr_b = (int16)vm_read_memory_2(sp + 12u);\n"
    "								sr_r = (int16)vm_read_memory_2(sp + 14u);\n"
    "								dr_t = (int16)vm_read_memory_2(sp + 16u);\n"
    "								dr_l = (int16)vm_read_memory_2(sp + 18u);\n"
    "								dr_b = (int16)vm_read_memory_2(sp + 20u);\n"
    "								dr_r = (int16)vm_read_memory_2(sp + 22u);\n"
    "							}\n"
    "							if (src_bm && dst_bm &&\n"
    "							    g3_ea_data(src_bm + 5u) &&\n"
    "							    g3_ea_data(dst_bm + 5u)) {\n"
    "								const uint32 fb = g3_qd_fb();\n"
    "								uint32 sbase = vm_read_memory_4(src_bm);\n"
    "								dbase = vm_read_memory_4(dst_bm);\n"
    "								uint32 srow = (uint32)(vm_read_memory_2(src_bm + 4u) & 0x3fffu);\n"
    "								uint32 drow = (uint32)(vm_read_memory_2(dst_bm + 4u) & 0x3fffu);\n"
    "{remap}"
    "								cb_w = (int)dr_r - (int)dr_l;\n"
    "								cb_h = (int)dr_b - (int)dr_t;\n"
    "								{\n"
    "									int sw = (int)sr_r - (int)sr_l;\n"
    "									int sh = (int)sr_b - (int)sr_t;\n"
    "									if (sw > 0 && sw < cb_w) cb_w = sw;\n"
    "									if (sh > 0 && sh < cb_h) cb_h = sh;\n"
    "								}\n"
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
    "								static unsigned n{logvar};\n"
    "								if (n{logvar} < 8) {\n"
    "									char buf[192];\n"
    "									n{logvar}++;\n"
    "									snprintf(buf, sizeof(buf),\n"
    "										 \"G3: 68k Launch A9F2 CFM Upgrader PEF {tag} s=%08x d=%08x w=%d h=%d db=%08x\",\n"
    "										 (unsigned)src_bm, (unsigned)dst_bm,\n"
    "										 cb_w, cb_h, (unsigned)dbase);\n"
    "									nw_boot_log(buf);\n"
    "								}\n"
    "							}\n"
    "#endif\n"
    "						}\n"
    "					} else if (op68 == 0xabe9u) {\n"
)

_CB32_OLD = (
    "					} else if (op68 == 0xa8ecu) {\n"
    "						/* leftover:pef-cb32: CopyBits 32->32 to guest FB. Pascal pop 30. */\n"
)


def _cb32_patch(text: str, mkey: str, tag: str, logvar: str, remap: str) -> str:
    m = _marker(tag)
    if m in text:
        return text
    idx = text.find(_CB32_OLD)
    if idx < 0:
        # already patched cb32b/c/d — replace through next trap
        idx = text.find("					} else if (op68 == 0xa8ecu) {\n")
        if idx < 0:
            raise ValueError("mill patch missing: %s cb32 anchor" % tag)
        end = text.find("					} else if (op68 == 0xabe9u) {\n", idx)
        if end < 0:
            raise ValueError("mill patch missing: %s cb32 tail" % tag)
        new_block = (
        _CB32_BODY.replace("{tag}", tag)
        .replace("{logvar}", logvar)
        .replace("{remap}", remap)
    )
        text = text[:idx] + new_block + text[end + len("					} else if (op68 == 0xabe9u) {\n"):]
    else:
        new_block = (
        _CB32_BODY.replace("{tag}", tag)
        .replace("{logvar}", logvar)
        .replace("{remap}", remap)
    )
        end = text.find("					} else if (op68 == 0xabe9u) {\n", idx)
        if end < 0:
            raise ValueError("mill patch missing: %s cb32 tail" % tag)
        text = text[:idx] + new_block + text[end + len("					} else if (op68 == 0xabe9u) {\n"):]
    return _insert_after_enter(text, _log_enter(m, "n" + logvar), "cpu-" + mkey)


def _unstub_pop4(
    text: str,
    mkey: str,
    ophex: str,
    stub_line: str,
    next_ophex: str,
    label: str,
    logvar: str,
) -> str:
    m = _marker(mkey[4:] if mkey.startswith("pef-") else mkey)
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0x%su) {\n" % ophex
        + "						/* %s */\n" % stub_line
    )
    idx = text.find(old)
    if idx < 0:
        raise ValueError("mill patch missing: %s head" % label)
    end_marker = "					} else if (op68 == 0x%su) {\n" % next_ophex
    end = text.find(end_marker, idx)
    if end < 0:
        raise ValueError("mill patch missing: %s tail" % label)
    text = text[:idx] + end_marker + text[end + len(end_marker):]
    return _insert_after_enter(text, _log_enter(m, logvar), label)


def patch_cpu_pef_cb32b(text: str) -> str:
    """Fix CopyBits Pascal stack layout (src@sp+0, dst@sp+4, rects@+8/+16)."""
    return _cb32_patch(text, "pef-cb32b", "cb32b", "cb32b", "")


def patch_cpu_pef_cb32c(text: str) -> str:
    """cb32b plus remap InitGraf port baseAddr to g3_qd_fb()."""
    remap = (
        "								if (dbase != fb && drow >= 2560u &&\n"
        "								    dbase >= RAMBase + 0xa100u &&\n"
        "								    dbase < RAMBase + 0xa200u)\n"
        "									dbase = fb;\n"
    )
    return _cb32_patch(text, "pef-cb32c", "cb32c", "cb32c", remap)


def patch_cpu_pef_cb32d(text: str) -> str:
    """cb32c plus accept rowBytes>=1024 src (16-bit pixmap) skipped — 32 only."""
    remap = (
        "								if (dbase != fb && drow >= 2560u &&\n"
        "								    dbase >= RAMBase + 0xa100u &&\n"
        "								    dbase < RAMBase + 0xa200u)\n"
        "									dbase = fb;\n"
        "								if (dbase != fb && dbase == vm_read_memory_4(dst_bm))\n"
        "									{ /* keep */ }\n"
    )
    return _cb32_patch(text, "pef-cb32d", "cb32d", "cb32d", remap)


def patch_cpu_pef_gportrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-gportrm", "a874",
        "leftover:pef-aa874: GetPort. Pascal pop 4.",
        "a023", "cpu-pef-gportrm", "ngportrm",
    )


def patch_cpu_pef_movetrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-movetrm", "a893",
        "leftover:pef-aa893: MoveTo. Pascal pop 4.",
        "a054", "cpu-pef-movetrm", "nmovetrm",
    )


def patch_cpu_pef_fillcrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-fillcrm", "aa12",
        "leftover:pef-fillc: FillCRgn. Pascal pop 8.",
        "a8e0", "cpu-pef-fillcrm", "nfillcrm",
    )


def patch_cpu_pef_offsetrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-offsetrm", "a8e0",
        "leftover:pef-a8e0: OffsetRgn. Pascal pop 8.",
        "a8e5", "cpu-pef-offsetrm", "noffsetrm",
    )


def patch_cpu_pef_unionrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-unionrm", "a8e5",
        "leftover:pef-unionrgn: UnionRgn. Pascal pop 12.",
        "a8d8", "cpu-pef-unionrm", "nunionrm",
    )


def patch_cpu_pef_newrgnrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-newrgnrm", "a8d8",
        "leftover:pef-aa8d8: NewRgn. Pascal pop 0.",
        "a000", "cpu-pef-newrgnrm", "nnewrgnrm",
    )


def patch_cpu_pef_invalrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-invalrm", "a927",
        "leftover:pef-invalrgn: InvalRgn. Pascal pop 4.",
        "a898", "cpu-pef-invalrm", "ninvalrm",
    )


def patch_cpu_pef_saversm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-saversm", "a81e",
        "leftover:pef-saverestor: SaveRestoreBits. Pascal pop 4.",
        "a023", "cpu-pef-saversm", "nsaversm",
    )


def patch_cpu_pef_textwrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-textwrm", "a886",
        "leftover:pef-textwidth: TextWidth. Pascal pop 4.",
        "a054", "cpu-pef-textwrm", "ntextwrm",
    )


def patch_cpu_pef_copyrgnrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-copyrgnrm", "a8dc",
        "leftover:pef-aa8dc: CopyRgn. Pascal pop 8.",
        "a886", "cpu-pef-copyrgnrm", "ncopyrgnrm",
    )


def patch_cpu_pef_getpenrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-getpenrm", "a898",
        "leftover:pef-getpenstat: GetPenState. Pascal pop 4.",
        "aa2c", "cpu-pef-getpenrm", "ngetpenrm",
    )


def patch_cpu_pef_setpenrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-setpenrm", "a899",
        "leftover:pef-setpenstat: SetPenState. Pascal pop 4.",
        "a80a", "cpu-pef-setpenrm", "nsetpenrm",
    )


def patch_cpu_pef_rgbfgh(text: str) -> str:
    """Host RGBForeColor AA14: set 68k fg without ROM trap (rgbfg REVERT)."""
    m = _marker("rgbfgh")
    if m in text:
        return text
    old = (
        "					} else if (op68 == 0xaa14u) {\n"
        "						/* leftover:pef-rgbforecol: RGBForeColor. Pascal pop 4. */\n"
    )
    idx = text.find(old)
    if idx < 0:
        raise ValueError("mill patch missing: rgbfgh head")
    end = text.find("					} else if (op68 == 0xa078u) {\n", idx)
    if end < 0:
        raise ValueError("mill patch missing: rgbfgh tail")
    new = (
        "					} else if (op68 == 0xaa14u) {\n"
        "						/* leftover:pef-rgbfgh: host RGBForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrgbfgh;\n"
        "							if (nrgbfgh < 8) {\n"
        "								nrgbfgh++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF rgbfgh 0xAA14\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa078u) {\n"
    )
    text = text[:idx] + new + text[end + len("					} else if (op68 == 0xa078u) {\n"):]
    return _insert_after_enter(text, _log_enter(m, "nrgbfgh"), "cpu-pef-rgbfgh")


def patch_cpu_pef_openrgnrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-openrgnrm", "a8da",
        "leftover:pef-openrgn: OpenRgn. Pascal pop 4.",
        "a8b0", "cpu-pef-openrgnrm", "nopenrgnrm",
    )


def patch_cpu_pef_closergnrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-closergnrm", "a8db",
        "leftover:pef-closergn: CloseRgn. Pascal pop 4.",
        "a05b", "cpu-pef-closergnrm", "nclosergnrm",
    )


def patch_cpu_pef_frameroundrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-frameroundrm", "a8b0",
        "leftover:pef-frameround: FrameRoundRect. Pascal pop 4.",
        "a05b", "cpu-pef-frameroundrm", "nframeroundrm",
    )


def patch_cpu_pef_emptyrgnrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-emptyrgnrm", "a8e2",
        "leftover:pef-emptyrgn: EmptyRgn. Pascal pop 4.",
        "a8dd", "cpu-pef-emptyrgnrm", "nemptyrgnrm",
    )


def patch_cpu_pef_getwvarrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-getwvarrm", "a80a",
        "leftover:pef-getwvarian: GetWVariant. Pascal pop 4.",
        "aa6a", "cpu-pef-getwvarrm", "ngetwvarrm",
    )


def patch_cpu_pef_setemptyrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-setemptyrm", "a8dd",
        "leftover:pef-setemptyrg: SetEmptyRgn. Pascal pop 4.",
        "a937", "cpu-pef-setemptyrm", "nsetemptyrm",
    )


def patch_cpu_pef_drawmenurm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-drawmenurm", "a937",
        "leftover:pef-drawmenuba: DrawMenuBar. Pascal pop 4.",
        "a975", "cpu-pef-drawmenurm", "ndrawmenurm",
    )


def patch_cpu_pef_tickcountrm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-tickcountrm", "a975",
        "leftover:pef-tickcount: TickCount. Pascal pop 4.",
        "aa6a", "cpu-pef-tickcountrm", "ntickcountrm",
    )


def patch_cpu_pef_setemptyrst(text: str) -> str:
    """Re-stub SetEmptyRgn A8DD after setemptyrm collapsed FB. Pascal pop 4."""
    m = _marker("setemptyrst")
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa2cu) {\n"
        "						/* leftover:pef-testdevice: TestDeviceAttribute. Pascal pop 4. */\n",
        "					} else if (op68 == 0xa8ddu) {\n"
        "						/* leftover:pef-setemptyrst: SetEmptyRgn re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsetemptyrst;\n"
        "							if (nsetemptyrst < 8) {\n"
        "								nsetemptyrst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF setemptyrst 0xA8DD\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xaa2cu) {\n"
        "						/* leftover:pef-testdevice: TestDeviceAttribute. Pascal pop 4. */\n",
        "cpu-pef-setemptyrst",
    )
    return _insert_after_enter(text, _log_enter(m, "nsetemptyrst"), "cpu-pef-setemptyrst")


def patch_cpu_pef_menubarst(text: str) -> str:
    """Re-stub DrawMenuBar A937 + TickCount A975 (inverse drawmenurm/tickcountrm)."""
    m = _marker("menubarst")
    if m in text:
        return text
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa6au) {\n"
        "						/* leftover:pef-devicemgr: DeviceMgr. Pascal pop 4. */\n",
        "					} else if (op68 == 0xa975u) {\n"
        "						/* leftover:pef-menubarst: TickCount re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ntickcountrst;\n"
        "							if (ntickcountrst < 8) {\n"
        "								ntickcountrst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF menubarst tick 0xA975\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa937u) {\n"
        "						/* leftover:pef-menubarst: DrawMenuBar re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndrawmenurst;\n"
        "							if (ndrawmenurst < 8) {\n"
        "								ndrawmenurst++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF menubarst draw 0xA937\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xaa6au) {\n"
        "						/* leftover:pef-devicemgr: DeviceMgr. Pascal pop 4. */\n",
        "cpu-pef-menubarst",
    )
    return _insert_after_enter(text, _log_enter(m, "nmenubarst"), "cpu-pef-menubarst")


def patch_cpu_pef_restore430(text: str) -> str:
    """Restore mill-17430 stub cluster before FrameRect (post-setcrm anchor).

    Re-applies setemptyrg, drawmenuba, tickcount in original order after
    setemptyrm/drawmenurm/tickcountrm removed them from the wrong anchors.
    """
    m = _marker("restore430")
    if m in text:
        return text
    anchor = (
        "					} else if (op68 == 0xa8a1u) {\n"
        "						/* FrameRect(r). Pascal pop 4. */\n"
    )
    tick = (
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
    )
    draw = (
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
    )
    setempty = (
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
    )
    text = _replace_once(text, anchor, tick + anchor, "cpu-pef-restore430-tick")
    text = _replace_once(text, anchor, draw + anchor, "cpu-pef-restore430-draw")
    text = _replace_once(text, anchor, setempty + anchor, "cpu-pef-restore430-setempty")
    return _insert_after_enter(text, _log_enter(m, "nrestore430"), "cpu-pef-restore430")


def patch_cpu_pef_peak430(text: str) -> str:
    """Restore mill-17430 trap layout: re-stub SetEmpty/DrawMenu/TickCount;
    replace rgbfgh with rgbforecol pop-4 stub."""
    m = _marker("peak430")
    if m in text:
        return text
    # Re-stub SetEmptyRgn A8DD before testdevice
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa2cu) {\n"
        "						/* leftover:pef-testdevice: TestDeviceAttribute. Pascal pop 4. */\n",
        "					} else if (op68 == 0xa8ddu) {\n"
        "						/* leftover:pef-peak430: SetEmptyRgn re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npeak430se;\n"
        "							if (npeak430se < 8) {\n"
        "								npeak430se++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF peak430 setempty 0xA8DD\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xaa2cu) {\n"
        "						/* leftover:pef-testdevice: TestDeviceAttribute. Pascal pop 4. */\n",
        "cpu-pef-peak430-setempty",
    )
    # Re-stub TickCount + DrawMenuBar before devicemgr
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa6au) {\n"
        "						/* leftover:pef-devicemgr: DeviceMgr. Pascal pop 4. */\n",
        "					} else if (op68 == 0xa975u) {\n"
        "						/* leftover:pef-peak430: TickCount re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npeak430tk;\n"
        "							if (npeak430tk < 8) {\n"
        "								npeak430tk++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF peak430 tick 0xA975\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa937u) {\n"
        "						/* leftover:pef-peak430: DrawMenuBar re-stub. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned npeak430dm;\n"
        "							if (npeak430dm < 8) {\n"
        "								npeak430dm++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF peak430 draw 0xA937\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xaa6au) {\n"
        "						/* leftover:pef-devicemgr: DeviceMgr. Pascal pop 4. */\n",
        "cpu-pef-peak430-menubar",
    )
    # Replace rgbfgh with rgbforecol pop-4 stub
    text = _replace_once(
        text,
        "					} else if (op68 == 0xaa14u) {\n"
        "						/* leftover:pef-rgbfgh: host RGBForeColor. Pascal pop 4. */\n"
        "						if (g3_ea_data(gpr(1)))\n"
        "							gpr(1) += 4u;\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrgbfgh;\n"
        "							if (nrgbfgh < 8) {\n"
        "								nrgbfgh++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF rgbfgh 0xAA14\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa078u) {\n",
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
        "cpu-pef-peak430-rgb",
    )
    return _insert_after_enter(text, _log_enter(m, "npeak430"), "cpu-pef-peak430")


def patch_cpu_pef_cb32e(text: str) -> str:
    """Host CopyBits at ROM BLE exit 0x20cc4 (A8EC PEF trap never fires)."""
    m = _marker("cb32e")
    if m in text:
        return text
    helper = (
        "static int g3_host_copybits_sp(uint32 sp)\n"
        "{\n"
        "	uint32 src_bm = 0, dst_bm = 0;\n"
        "	int16 sr_t = 0, sr_l = 0, sr_b = 0, sr_r = 0;\n"
        "	int16 dr_t = 0, dr_l = 0, dr_b = 0, dr_r = 0;\n"
        "	int cb_w = 0, cb_h = 0;\n"
        "	uint32 dbase = 0;\n"
        "	if (!g3_ea_data(sp + 29u))\n"
        "		return 0;\n"
        "	src_bm = vm_read_memory_4(sp + 0u);\n"
        "	dst_bm = vm_read_memory_4(sp + 4u);\n"
        "	sr_t = (int16)vm_read_memory_2(sp + 8u);\n"
        "	sr_l = (int16)vm_read_memory_2(sp + 10u);\n"
        "	sr_b = (int16)vm_read_memory_2(sp + 12u);\n"
        "	sr_r = (int16)vm_read_memory_2(sp + 14u);\n"
        "	dr_t = (int16)vm_read_memory_2(sp + 16u);\n"
        "	dr_l = (int16)vm_read_memory_2(sp + 18u);\n"
        "	dr_b = (int16)vm_read_memory_2(sp + 20u);\n"
        "	dr_r = (int16)vm_read_memory_2(sp + 22u);\n"
        "	if (!src_bm || !dst_bm || !g3_ea_data(src_bm + 5u) ||\n"
        "	    !g3_ea_data(dst_bm + 5u))\n"
        "		return 0;\n"
        "	{\n"
        "		const uint32 fb = g3_qd_fb();\n"
        "		uint32 sbase = vm_read_memory_4(src_bm);\n"
        "		dbase = vm_read_memory_4(dst_bm);\n"
        "		uint32 srow = (uint32)(vm_read_memory_2(src_bm + 4u) & 0x3fffu);\n"
        "		uint32 drow = (uint32)(vm_read_memory_2(dst_bm + 4u) & 0x3fffu);\n"
        "		if (dbase != fb && drow >= 2560u && dbase >= RAMBase + 0xa100u &&\n"
        "		    dbase < RAMBase + 0xa200u)\n"
        "			dbase = fb;\n"
        "		cb_w = (int)dr_r - (int)dr_l;\n"
        "		cb_h = (int)dr_b - (int)dr_t;\n"
        "		{\n"
        "			int sw = (int)sr_r - (int)sr_l;\n"
        "			int sh = (int)sr_b - (int)sr_t;\n"
        "			if (sw > 0 && sw < cb_w) cb_w = sw;\n"
        "			if (sh > 0 && sh < cb_h) cb_h = sh;\n"
        "		}\n"
        "		if (cb_w > 640) cb_w = 640;\n"
        "		if (cb_h > 480) cb_h = 480;\n"
        "		if (cb_w > 0 && cb_h > 0 && srow >= 2560u && drow >= 2560u &&\n"
        "		    sbase && dbase && (dbase == fb || drow >= 2560u)) {\n"
        "			unsigned y, x;\n"
        "			for (y = 0; y < (unsigned)cb_h; y++) {\n"
        "				uint32 s = sbase + (uint32)((int)sr_t + (int)y) * srow +\n"
        "				    (uint32)sr_l * 4u;\n"
        "				uint32 d = dbase + (uint32)((int)dr_t + (int)y) * drow +\n"
        "				    (uint32)dr_l * 4u;\n"
        "				if (!g3_ea_data(s + (uint32)cb_w * 4u - 1u) ||\n"
        "				    !g3_ea_data(d + (uint32)cb_w * 4u - 1u))\n"
        "					continue;\n"
        "				for (x = 0; x < (unsigned)cb_w; x++) {\n"
        "					vm_write_memory_4(d, vm_read_memory_4(s));\n"
        "					s += 4u;\n"
        "					d += 4u;\n"
        "				}\n"
        "			}\n"
        "		}\n"
        "	}\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned ncb32e;\n"
        "		if (ncb32e < 8) {\n"
        "			char buf[192];\n"
        "			ncb32e++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF cb32e s=%08x d=%08x w=%d h=%d db=%08x\",\n"
        "				 (unsigned)src_bm, (unsigned)dst_bm, cb_w, cb_h,\n"
        "				 (unsigned)dbase);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	return cb_w > 0 && cb_h > 0;\n"
        "}\n"
        "static void g3_draw_ditl3500(void)\n"
    )
    text = _replace_once(
        text,
        "static void g3_draw_ditl3500(void)\n",
        helper,
        "cpu-pef-cb32e-helper",
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
        "						} else if (bcc_opc == ROMBase + 0x20cc2u ||\n",
        "						if (bcc_opc == ROMBase + 0x20cb4u ||\n"
        "						    bcc_opc == ROMBase + 0x1fafau) {\n"
        "							take = 1;\n"
        "							g3_host_copybits_sp(gpr(1));\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned nblex;\n"
        "								if (nblex < 8) {\n"
        "									nblex++;\n"
        "									nw_boot_log(\"G3: 68k CopyBits BLE exit 0x20cc4\");\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						} else if (bcc_opc == ROMBase + 0x20cc2u ||\n",
        "cpu-pef-cb32e-ble",
    )
    return _insert_after_enter(text, _log_enter(m, "ncb32e"), "cpu-pef-cb32e")


def patch_cpu_pef_cb32f(text: str) -> str:
    """Normalize inverted CopyBits rects in g3_host_copybits_sp."""
    m = _marker("cb32f")
    if m in text:
        return text
    old = (
        "		if (dbase != fb && drow >= 2560u && dbase >= RAMBase + 0xa100u &&\n"
        "		    dbase < RAMBase + 0xa200u)\n"
        "			dbase = fb;\n"
        "		cb_w = (int)dr_r - (int)dr_l;\n"
        "		cb_h = (int)dr_b - (int)dr_t;\n"
        "		{\n"
        "			int sw = (int)sr_r - (int)sr_l;\n"
        "			int sh = (int)sr_b - (int)sr_t;\n"
        "			if (sw > 0 && sw < cb_w) cb_w = sw;\n"
        "			if (sh > 0 && sh < cb_h) cb_h = sh;\n"
        "		}\n"
    )
    new = (
        "		if (dbase != fb && drow >= 2560u && dbase >= RAMBase + 0xa100u &&\n"
        "		    dbase < RAMBase + 0xa200u)\n"
        "			dbase = fb;\n"
        "		if (dr_r < dr_l) {\n"
        "			int16 t = dr_l;\n"
        "			dr_l = dr_r;\n"
        "			dr_r = t;\n"
        "		}\n"
        "		if (dr_b < dr_t) {\n"
        "			int16 t = dr_t;\n"
        "			dr_t = dr_b;\n"
        "			dr_b = t;\n"
        "		}\n"
        "		if (sr_r < sr_l) {\n"
        "			int16 t = sr_l;\n"
        "			sr_l = sr_r;\n"
        "			sr_r = t;\n"
        "		}\n"
        "		if (sr_b < sr_t) {\n"
        "			int16 t = sr_t;\n"
        "			sr_t = sr_b;\n"
        "			sr_b = t;\n"
        "		}\n"
        "		cb_w = (int)dr_r - (int)dr_l;\n"
        "		cb_h = (int)dr_b - (int)dr_t;\n"
        "		{\n"
        "			int sw = (int)sr_r - (int)sr_l;\n"
        "			int sh = (int)sr_b - (int)sr_t;\n"
        "			if (sw > 0 && (cb_w <= 0 || sw < cb_w)) cb_w = sw;\n"
        "			if (sh > 0 && (cb_h <= 0 || sh < cb_h)) cb_h = sh;\n"
        "		}\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-cb32f-norm")
    text = _replace_once(
        text,
        '				 "G3: 68k Launch A9F2 CFM Upgrader PEF cb32e s=%08x d=%08x w=%d h=%d db=%08x",\n',
        '				 "G3: 68k Launch A9F2 CFM Upgrader PEF cb32f s=%08x d=%08x w=%d h=%d db=%08x",\n',
        "cpu-pef-cb32f-log",
    )
    return _insert_after_enter(text, _log_enter(m, "ncb32f"), "cpu-pef-cb32f")


def patch_cpu_pef_cwmgrprm(text: str) -> str:
    return _unstub_pop4(
        text, "pef-cwmgrprm", "aa48",
        "leftover:pef-getcwmgrpo: GetCWMgrPort. Pascal pop 4.",
        "a8dc", "cpu-pef-cwmgrprm", "ncwmgrprm",
    )


QDLOOP_PATCH: Dict[str, Callable[[str], str]] = {
    "pef-cb32b": patch_cpu_pef_cb32b,
    "pef-cb32c": patch_cpu_pef_cb32c,
    "pef-cb32d": patch_cpu_pef_cb32d,
    "pef-cb32e": patch_cpu_pef_cb32e,
    "pef-cb32f": patch_cpu_pef_cb32f,
    "pef-gportrm": patch_cpu_pef_gportrm,
    "pef-movetrm": patch_cpu_pef_movetrm,
    "pef-fillcrm": patch_cpu_pef_fillcrm,
    "pef-offsetrm": patch_cpu_pef_offsetrm,
    "pef-unionrm": patch_cpu_pef_unionrm,
    "pef-newrgnrm": patch_cpu_pef_newrgnrm,
    "pef-invalrm": patch_cpu_pef_invalrm,
    "pef-saversm": patch_cpu_pef_saversm,
    "pef-textwrm": patch_cpu_pef_textwrm,
    "pef-copyrgnrm": patch_cpu_pef_copyrgnrm,
    "pef-getpenrm": patch_cpu_pef_getpenrm,
    "pef-setpenrm": patch_cpu_pef_setpenrm,
    "pef-rgbfgh": patch_cpu_pef_rgbfgh,
    "pef-openrgnrm": patch_cpu_pef_openrgnrm,
    "pef-closergnrm": patch_cpu_pef_closergnrm,
    "pef-frameroundrm": patch_cpu_pef_frameroundrm,
    "pef-emptyrgnrm": patch_cpu_pef_emptyrgnrm,
    "pef-getwvarrm": patch_cpu_pef_getwvarrm,
    "pef-setemptyrm": patch_cpu_pef_setemptyrm,
    "pef-drawmenurm": patch_cpu_pef_drawmenurm,
    "pef-tickcountrm": patch_cpu_pef_tickcountrm,
    "pef-cwmgrprm": patch_cpu_pef_cwmgrprm,
    "pef-setemptyrst": patch_cpu_pef_setemptyrst,
    "pef-menubarst": patch_cpu_pef_menubarst,
    "pef-restore430": patch_cpu_pef_restore430,
    "pef-peak430": patch_cpu_pef_peak430,
}
