#!/usr/bin/env python3
"""Regenerate ../JIT_OPS.md from ppc-decode.cpp × nw_jit.cpp.

Ports nw_jit_op_supported / nw_jit_op_ends_block, probes representative
encodings, and writes the same 345-name document schema as the previous
checklist (legend, summary, table, wave queues, done list).
"""
from __future__ import annotations

import datetime as _dt
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECODE = ROOT / "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp"
NW_JIT = ROOT / "SheepShaver/src/nw_jit.cpp"
OUT = ROOT / "JIT_OPS.md"

EXCLUDE = {"invalid", "eciwx", "ecowx"}
EXCLUDE_WHY = {
    "invalid": "Not a real insn. Prim=6 skip noise is usually this class.",
    "eciwx": "External control; no device model.",
    "ecowx": "External control; no device model.",
}

NW_PPC_BO_TRUE = 12
NW_PPC_BO_FALSE = 4
NW_PPC_BO_BDNZ = 16
NW_PPC_BO_ALWAYS = 20
NW_PPC_SPR_TBL = 268
NW_PPC_SPR_TBU = 269
NW_PPC_SPR_LR = 8

ALTIVEC_FP = {
    "vaddfp", "vcfsx", "vcfux", "vcmpbfp", "vcmpeqfp", "vcmpgefp", "vcmpgtfp",
    "vctsxs", "vctuxs", "vexptefp", "vlogefp", "vmaddfp", "vmaxfp", "vminfp",
    "vnmsubfp", "vrefp", "vrfim", "vrfin", "vrfip", "vrfiz", "vrsqrtefp", "vsubfp",
}
ALTIVEC_ELEM_MEM = {
    "lvebx", "lvehx", "lvewx", "stvebx", "stvehx", "stvewx",
}
W2_INTEGER = {
    "addme", "dcba", "dcbi", "dcbst", "mcrxr", "mfsr", "orc", "subfme", "tlbia", "tw",
}
W3_MEM = {"lhbrx", "sthbrx", "stwbrx"}
HINT_ALIAS = {"lvx", "lvxl", "stvx", "stvxl"}
RA_UPDATE = {
    "lhzu", "lhzux", "stbux", "sthux", "lbzux", "lwzux", "stwux",
    "lfdu", "lfsu", "stfdu", "stfsu", "lfsux", "lfdux", "stfsux", "stfdux",
}
L_COMPARE = {"cmp", "cmpi", "cmpl", "cmpli"}
W1_BRANCH = {"b", "bclr", "bcctr"}
ENDS_RESTRICT = {"vslo", "vsro"}
NAMED_EMIT_ALIAS = {
    "lvxl": "lvx",
    "stvxl": "stvx",
    "lvsr": "lvsl",
    "vmsumshs": "vmsumshm",
}


def bo_is_cr(bo: int) -> bool:
    b = bo & ~1
    return b == NW_PPC_BO_TRUE or b == NW_PPC_BO_FALSE


def spr_num(op: int) -> int:
    spr = (op >> 11) & 0x3FF
    return ((spr & 0x1F) << 5) | ((spr >> 5) & 0x1F)


def encode_spr_field(spr: int) -> int:
    return ((spr & 0x1F) << 5) | ((spr >> 5) & 0x1F)


def nw_jit_op_supported(op: int) -> int:
    prim = (op >> 26) & 0x3F
    rd = (op >> 21) & 0x1F
    xo = (op >> 1) & 0x3FF
    if prim in (14, 12, 13):
        return 1
    if prim == 15:
        return 1
    if prim == 7:
        return 1
    if prim == 31 and xo in (10, 522):
        return 1
    if prim == 31 and xo in (138, 650):
        return 1
    if prim == 31 and xo == 520:
        return 1
    if prim == 31 and xo == 8:
        return 1
    if prim == 31 and xo == 136:
        return 1
    if prim == 31 and xo == 648:
        return 1
    if prim == 31 and xo == 40:
        return 1
    if prim == 31 and xo == 552:
        return 1
    if prim == 28:
        return 1
    if prim == 10:
        return int((rd & 3) == 0)
    if prim == 31 and xo == 144:
        return 1
    if prim == 31 and xo == 19:
        return 1
    if prim == 31 and xo == 922:
        return 1
    if prim == 31 and xo == 954:
        return 1
    if prim == 31 and xo == 24:
        return 1
    if prim == 31 and xo == 536:
        return 1
    if prim == 31 and xo == 792:
        return 1
    if prim == 31 and xo == 824:
        return 1
    if prim == 31 and xo == 598:
        return 1
    if prim == 31 and xo == 566:
        return 1
    if prim == 31 and xo == 822:
        return 1
    if prim == 31 and xo in (342, 374):
        return 1
    if prim == 31 and xo in (278, 246, 86, 54, 470, 758):
        return 1
    if prim == 31 and xo == 854:
        return 1
    if prim == 31 and xo == 1014:
        return 1
    if prim == 31 and xo == 210:
        return 1
    if prim == 3:
        return 1
    if prim == 31 and xo == 146:
        return 1
    if prim == 19 and xo == 50:
        return 1
    if prim == 19 and xo == 150:
        return 1
    if prim == 11:
        return int((rd & 3) == 0)
    if prim in (20, 21):
        return 1
    if prim == 23:
        return 1
    if prim in (46, 47):
        return 1
    if prim == 16:
        return 1
    if prim == 18:
        return 1
    if prim == 19 and xo == 0:
        return 1
    if prim == 19 and xo == 33:
        return 1
    if prim == 19 and xo == 193:
        return 1
    if prim == 19 and xo == 289:
        return 1
    if prim == 19 and xo == 449:
        return 1
    if prim == 19 and xo == 417:
        return 1
    if prim == 19 and xo == 257:
        return 1
    if prim == 19 and xo == 129:
        return 1
    if prim == 19 and xo == 225:
        return 1
    if prim == 19 and xo in (16, 528):
        return 1
    if prim == 31 and xo in (266, 778):
        return 1
    if prim == 31 and xo == 444:
        return 1
    if prim == 31 and xo == 316:
        return 1
    if prim == 31 and xo == 284:
        return 1
    if prim == 31 and xo == 476:
        return 1
    if prim == 31 and xo == 124:
        return 1
    if prim == 31 and xo == 28:
        return 1
    if prim == 31 and xo == 26:
        return 1
    if prim == 31 and xo == 104:
        return 1
    if prim == 24:
        return 1
    if prim == 25:
        return 1
    if prim == 31 and xo == 0:
        return int((rd & 3) == 0)
    if prim == 31 and xo == 32:
        return int((rd & 3) == 0)
    if prim == 31 and xo == 339:
        return 1
    if prim == 31 and xo == 467:
        return 1
    if prim in (32, 33, 36, 37):
        return 1
    if prim == 31 and xo in (20, 150):
        return 1
    if prim == 31 and xo in (533, 661):
        return 1
    if prim == 31 and xo in (597, 725):
        return 1
    if prim == 31 and xo == 982:
        return 1
    if prim == 31 and xo == 306:
        return 1
    if prim == 31 and xo == 202:
        return 1
    if prim == 31 and xo == 200:
        return 1
    if prim == 31 and xo == 234:
        return 1
    if prim == 31 and xo == 232:
        return 1
    if prim == 31 and xo == 412:
        return 1
    if prim == 31 and xo == 512:
        return 1
    if prim == 31 and xo == 595:
        return 1
    if prim == 31 and xo == 4:
        return 1
    if prim == 31 and xo == 370:
        return 1
    if prim in (34, 35, 38):
        return 1
    if prim == 39:
        return 1
    if prim == 31 and xo == 87:
        return 1
    if prim == 31 and xo == 215:
        return 1
    if prim == 31 and xo == 247:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo in (103, 359):
        return 1
    if prim == 31 and xo in (231, 487):
        return 1
    if prim == 31 and xo == 23:
        return 1
    if prim == 31 and xo == 151:
        return 1
    if prim == 31 and xo == 183:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo == 55:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo == 119:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo == 407:
        return 1
    if prim == 31 and xo == 439:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo in (343, 375):
        return 1
    if prim in (40, 42, 43, 44):
        return 1
    if prim == 41:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 45:
        return 1
    if prim == 50:
        return 1
    if prim == 54:
        return 1
    if prim == 48:
        return 1
    if prim == 52:
        return 1
    if prim in (49, 51, 53, 55):
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo in (535, 663):
        return 1
    if prim == 31 and xo in (599, 727):
        return 1
    if prim == 31 and xo in (567, 631, 695, 759):
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 59:
        axo = (op >> 1) & 0x1F
        if axo in (18, 20, 21, 24, 25, 28, 29, 30, 31):
            return 1
    if prim == 63 and xo == 40:
        return 1
    if prim == 63 and xo == 72:
        return 1
    if prim == 63 and xo == 12:
        return 1
    if prim == 63 and xo == 583:
        return 1
    if prim == 63 and xo == 15:
        return 1
    if prim == 63 and xo == 14:
        return 1
    if prim == 63 and xo == 32:
        return 1
    if prim == 63 and xo == 0:
        return 1
    if prim == 63 and xo == 264:
        return 1
    if prim == 63 and xo == 136:
        return 1
    if prim == 63 and xo == 711:
        return 1
    if prim == 63 and xo == 70:
        return 1
    if prim == 63 and xo == 38:
        return 1
    if prim == 63 and xo == 134:
        return 1
    if prim == 63 and xo == 64:
        return 1
    if prim == 63:
        axo = (op >> 1) & 0x1F
        if axo in (18, 20, 21, 23, 25, 26, 28, 29, 30, 31):
            return 1
    if prim == 31 and xo in (6, 38):
        return 1
    if prim == 4:
        return 1
    if prim == 31 and xo == 235:
        return 1
    if prim == 31 and xo == 747:
        return 1
    if prim == 31 and xo == 459:
        return 1
    if prim == 31 and xo == 971:
        return 1
    if prim == 31 and xo == 491:
        return 1
    if prim == 31 and xo == 1003:
        return 1
    if prim == 8:
        return 1
    if prim == 31 and xo == 83:
        return 1
    if prim == 31 and xo == 371:
        tbr = spr_num(op)
        return int(tbr == NW_PPC_SPR_TBL or tbr == NW_PPC_SPR_TBU)
    if prim == 17:
        return 1
    if prim == 31 and xo == 11:
        return 1
    if prim == 31 and xo == 75:
        return 1
    if prim == 31 and xo == 279:
        return 1
    if prim == 31 and xo == 311:
        return int(((op >> 16) & 0x1F) != 0)
    if prim == 31 and xo == 534:
        return 1
    if prim == 31 and xo == 790:
        return 1
    if prim == 31 and xo == 918:
        return 1
    if prim == 31 and xo == 662:
        return 1
    if prim == 31 and xo in (7, 39, 71, 135, 167, 199):
        return 1
    if prim == 31 and xo == 242:
        return 1
    if prim == 31 and xo == 659:
        return 1
    if prim == 31 and xo == 60:
        return 1
    if prim == 26:
        return 1
    if prim == 27:
        return 1
    if prim == 29:
        return 1
    return 0


def nw_jit_op_ends_block(op: int) -> bool:
    prim = (op >> 26) & 0x3F
    xo = (op >> 1) & 0x3FF
    vxo = op & 0x7FF
    if prim in (16, 18, 17):
        return True
    if prim == 19 and xo in (16, 528, 150, 50):
        return True
    if prim == 31 and xo in (146, 982, 306, 370, 210, 242):
        return True
    if prim == 31 and xo == 339:
        spr = spr_num(op)
        user = spr in (22, 8, 9, 1)  # DEC, LR, CTR, XER
        ext = spr in (268, 269, 287, 256) or (272 <= spr <= 275)
        return not user and not ext
    if prim == 4 and vxo in (1036, 1100):
        return True
    return False


def parse_decode_table(text: str) -> list[dict]:
    start = text.find("powerpc_ii_table[]")
    if start < 0:
        raise SystemExit("powerpc_ii_table not found")
    brace = text.find("{", start)
    i = brace + 1
    entries: list[dict] = []
    n = len(text)
    while i < n:
        while i < n and text[i] in " \t\n\r":
            i += 1
        if i < n and text[i] == "}":
            break
        if text.startswith("{", i):
            i += 1
            while i < n and text[i] in " \t\n\r":
                i += 1
            if text[i] != '"':
                raise SystemExit(f"expected name at {i}")
            j = text.find('"', i + 1)
            name = text[i + 1 : j]
            i = j + 1
            while i < n and text[i] in " \t\n\r,":
                i += 1
            if not text.startswith("EXECUTE", i):
                raise SystemExit(f"expected EXECUTE for {name}")
            exec_start = i
            # skip identifier
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            if text[i] != "(":
                raise SystemExit(f"EXECUTE missing ( for {name}")
            depth = 0
            while i < n:
                if text[i] == "(":
                    depth += 1
                elif text[i] == ")":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
            execute = " ".join(text[exec_start:i].split())
            while i < n and text[i] in " \t\n\r,":
                i += 1
            if not text.startswith("PPC_I(", i):
                raise SystemExit(f"expected PPC_I for {name}")
            i = text.find(")", i) + 1
            while i < n and text[i] in " \t\n\r,":
                i += 1
            m = re.match(r"(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(CFLOW_\w+)", text[i:])
            if not m:
                raise SystemExit(f"bad trailer for {name}: {text[i:i+80]!r}")
            form, prim, xo, cflow = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
            i += m.end()
            while i < n and text[i] not in "}":
                i += 1
            if i < n and text[i] == "}":
                i += 1
            while i < n and text[i] in " \t\n\r,":
                i += 1
            entries.append({
                "name": name,
                "execute": execute,
                "form": form,
                "prim": prim,
                "xo": xo,
                "cflow": cflow,
            })
            continue
        i += 1
    return entries


def load_family_map(md: Path) -> dict[str, str]:
    fam: dict[str, str] = {}
    if not md.exists():
        return fam
    for line in md.read_text().splitlines():
        m = re.match(r"\| \[[x ~\-]\] \| `([^`]+)` \| (\w+) \|", line)
        if m:
            fam[m.group(1)] = m.group(2)
    return fam


def infer_family(e: dict, fam_map: dict[str, str]) -> str:
    name = e["name"]
    if name in fam_map:
        return fam_map[name]
    if name == "invalid":
        return "none"
    if name in ALTIVEC_ELEM_MEM or name.startswith(("lv", "stv")):
        return "altivec"
    if e["form"] in ("VX_form", "VA_form", "VXR_form") or e["prim"] == 4:
        return "altivec"
    if name.startswith("f") or name in {
        "lfd", "lfdu", "lfdux", "lfdx", "lfs", "lfsu", "lfsux", "lfsx",
        "stfd", "stfdu", "stfdux", "stfdx", "stfs", "stfsu", "stfsux", "stfsx",
        "mcrfs", "mffs", "mtfsb0", "mtfsb1", "mtfsf", "mtfsfi",
    }:
        return "fp"
    if name in {
        "b", "bc", "bcctr", "bclr", "crand", "crandc", "creqv", "crnand",
        "crnor", "cror", "crorc", "crxor", "isync", "rfi", "sc",
    }:
        return "control"
    if name in {
        "mcrf", "mcrxr", "mfcr", "mfmsr", "mfspr", "mfsr", "mfsrin", "mftb",
        "mtcrf", "mtmsr", "mtspr", "mtsr", "mtsrin",
    }:
        return "spr"
    if name.startswith(("l", "st")):
        return "mem"
    return "integer"


def scan_nw_jit(text: str) -> dict:
    helpers = set(re.findall(r"nw_jit_helper_([A-Za-z0-9_]+)\s*\(", text))
    encode_fns = set(re.findall(r"nw_ppc_([A-Za-z0-9_]+)\s*\(", text))
    emit_calls = set(re.findall(r"emit_call_([A-Za-z0-9_]+)\s*\(", text))
    # string mentions excluding comments-only is hard; take all word mentions
    names_in_file = set()
    for m in re.finditer(r'"([a-z][a-z0-9.]+)"', text):
        names_in_file.add(m.group(1))
    emit_fn = text[text.find("static int emit_op("):]
    prim4 = ""
    m = re.search(r"if \(prim == 4\) \{", emit_fn)
    if m:
        prim4 = emit_fn[m.start(): m.start() + 8000]
    emit_vxo = {int(x) for x in re.findall(r"vxo == (\d+)", prim4)}
    emit_vaxo = {int(x) for x in re.findall(r"vaxo == (\d+)", prim4)}
    return {
        "helpers": helpers,
        "encode_fns": encode_fns,
        "emit_calls": emit_calls,
        "quoted": names_in_file,
        "emit_vxo": emit_vxo,
        "emit_vaxo": emit_vaxo,
        "text": text,
    }


def encode_op(e: dict, *, rd=4, ra=3, rb=2, rc=0, aa=0, spr=None, l_bit=0, bo=None) -> int:
    form, prim, xo = e["form"], e["prim"], e["xo"]
    op = (prim & 0x3F) << 26
    rd_f = rd
    if bo is not None:
        rd_f = bo
    if l_bit:
        rd_f |= 1
    if form in ("VX_form",):
        op |= (rd_f << 21) | (ra << 16) | (rb << 11) | (xo & 0x7FF)
    elif form == "VXR_form":
        op |= (rd_f << 21) | (ra << 16) | (rb << 11) | (xo & 0x7FF)
        if rc:
            op |= 1 << 10
    elif form == "VA_form":
        op |= (rd_f << 21) | (ra << 16) | (rb << 11) | (xo & 0x3F)
    elif form in ("X_form", "XO_form", "XFX_form", "XFL_form", "XL_form"):
        op |= (rd_f << 21) | (ra << 16) | (rb << 11) | ((xo & 0x3FF) << 1) | (rc & 1)
        if spr is not None:
            op = (op & ~0x1FF800) | (encode_spr_field(spr) << 11)
    elif form in ("D_form", "B_form", "M_form"):
        op |= (rd_f << 21) | (ra << 16)
        op |= rc & 1
        if aa:
            op |= 2
    elif form == "I_form":
        op |= (aa & 1) << 1
        op |= rc & 1
    elif form == "SC_form":
        op |= 2  # LEV unused; SA=1 typical
    elif form == "INVALID_form":
        pass
    else:
        op |= (rd_f << 21) | (ra << 16) | (rb << 11) | ((xo & 0x3FF) << 1) | (rc & 1)
    return op & 0xFFFFFFFF


def has_named_emit(name: str, scan: dict, e: dict) -> bool:
    key = name.replace(".", "")
    if key in scan["helpers"] or key in scan["emit_calls"] or key in scan["encode_fns"]:
        return True
    alias = NAMED_EMIT_ALIAS.get(name)
    if alias and (alias in scan["helpers"] or alias in scan["emit_calls"]):
        return True
    if e["form"] in ("VX_form", "VXR_form") and e["xo"] in scan["emit_vxo"]:
        return True
    if e["form"] == "VA_form" and e["xo"] in scan["emit_vaxo"]:
        return True
    if e["prim"] == 4 and "nw_jit_helper_vmx" in scan["text"][scan["text"].find("static int emit_op("):]:
        return True
    # allowlisted scalar: standing rule is allowlist ⇒ emit
    return False


def name_mentioned(name: str, scan: dict) -> bool:
    if f'"{name}"' in scan["text"]:
        return True
    key = name.replace(".", "")
    return bool(re.search(rf"\b{re.escape(key)}\b", scan["text"]))


def classify(e: dict, scan: dict) -> dict:
    name = e["name"]
    caveats: list[str] = []
    gates_ok: list[str] = []
    gates_fail: list[str] = []
    mill_hole = False

    if name in EXCLUDE:
        return {
            "mark": "[-]",
            "status": "exclude",
            "wave": "exclude",
            "in_allowlist": False,
            "has_emit": False,
            "ends": False,
            "caveats": caveats,
            "ok": gates_ok,
            "fail": gates_fail,
            "mill_hole": False,
            "notes_extra": [EXCLUDE_WHY[name], "not in `nw_jit_op_supported`"],
        }

    reps: list[tuple[str, int]] = []
    base = encode_op(e)
    reps.append(("base", base))
    if e["form"] in ("X_form", "XO_form", "A_form", "XFL_form", "VX_form", "VXR_form", "VA_form"):
        reps.append(("Rc=1", encode_op(e, rc=1)))
    if name in RA_UPDATE or "true, true" in e["execute"] or ", true, false)" in e["execute"] and "RA," in e["execute"]:
        reps.append(("RA≠0", encode_op(e, ra=1)))
        reps.append(("RA=0", encode_op(e, ra=0)))
    if name in L_COMPARE:
        reps.append(("L=0", encode_op(e, rd=4, l_bit=0)))  # crfD=1, L=0 → rd bits 25:21 = 0b00100
        # crfD=0 L=0: rd=0; L=1: rd=1
        reps.append(("L=0 cr0", encode_op(e, rd=0, l_bit=0)))
        reps.append(("L=1", encode_op(e, rd=1, l_bit=1)))
    if name == "b":
        reps.append(("AA=0", encode_op(e, aa=0)))
        reps.append(("AA=1", encode_op(e, aa=1)))
    if name in ("bclr", "bcctr"):
        reps.append(("BO=20", encode_op(e, bo=NW_PPC_BO_ALWAYS)))
        reps.append(("BO=CR", encode_op(e, bo=NW_PPC_BO_TRUE)))
        reps.append(("BO=CTR", encode_op(e, bo=NW_PPC_BO_BDNZ)))
    if name == "mftb":
        reps.append(("TBL", encode_op(e, spr=NW_PPC_SPR_TBL)))
        reps.append(("TBU", encode_op(e, spr=NW_PPC_SPR_TBU)))
        reps.append(("otherSPR", encode_op(e, spr=1)))
    if name == "mfspr":
        reps.append(("LR", encode_op(e, spr=NW_PPC_SPR_LR)))

    results = {tag: bool(nw_jit_op_supported(op)) for tag, op in reps}
    any_ok = any(results.values())
    ends = False
    for tag, op in reps:
        if results[tag] and nw_jit_op_ends_block(op):
            ends = True
            break
    if not ends:
        ends = nw_jit_op_ends_block(base)

    emit = has_named_emit(name, scan, e)
    if any_ok and not emit:
        # family XO covered?
        if e["prim"] == 4:
            emit = (e["xo"] in scan["emit_vxo"]) or (e["xo"] in scan["emit_vaxo"])
        elif any_ok:
            # allowlist without helper name still counts as emit if C will compile
            emit = True

    ra0 = results.get("RA=0")
    ra1 = results.get("RA≠0")
    if "RA=0" in results and "RA≠0" in results:
        if ra1 and not ra0:
            gates_ok.append("RA≠0")
            gates_fail.append("RA=0")
            caveats.append("RA≠0 required.")
        elif ra1:
            pass

    if name in L_COMPARE:
        if results.get("L=0 cr0") and not results.get("L=1"):
            caveats.append("L=0 only.")
            gates_ok.append("L=0")
            gates_fail.append("L=1")

    if name == "b":
        if results.get("AA=0") and not results.get("AA=1"):
            caveats.append("AA=0 only (`(op & 2) == 0`). Absolute `b` falls back.")
            gates_ok.append("AA=0")
            gates_fail.append("AA=1")
            mill_hole = True

    if name in ("bclr", "bcctr"):
        if results.get("BO=20") or results.get("BO=CR"):
            gates_ok.append("BO=20")
            gates_ok.append("BO=CR")
        if not results.get("BO=CTR"):
            gates_fail.append("BO=CTR")
            if name == "bclr":
                caveats.append("Only BO=always (blr) or CR-true/false. CTR/decrement BO → skip_unsup.")
            else:
                caveats.append("Same BO gate as bclr.")
            mill_hole = True

    rc0 = results.get("base")
    rc1 = results.get("Rc=1")
    # FP Rc gate from C: most FP require !(op & 1), except fcmpo/fcmpu
    if name not in ("fcmpo", "fcmpu") and (e["prim"] in (59, 63) or name.startswith("f") or name in {"mffs", "mtfsf"}):
        if rc0 and rc1 is False:
            caveats.append("Rc=0 forms only in allowlist (`!(op & 1)`), except fcmpo/fcmpu.")
            gates_ok.append("Rc=0")
            gates_fail.append("Rc=1")
            mill_hole = True

    if name == "mftb":
        if results.get("TBL") and not results.get("otherSPR"):
            caveats.append("TBL/TBU only.")

    if name in HINT_ALIAS:
        if name in ("lvx",):
            caveats.append("hint ignored (lvxl same path).")
        else:
            caveats.append("hint ignored.")

    if name in ENDS_RESTRICT:
        caveats.append("Allowlisted but **ends_block** (one-op; Starting Up lock).")
        mill_hole = True

    if name == "mfspr":
        caveats.append("Supported; non-user SPR ends block via helper.")
    if name == "mtspr":
        caveats.append("Supported; guest helper for non-user.")
    if name in ("mtmsr", "mtsr", "mtsrin", "tlbie", "tlbia", "icbi"):
        caveats.append("ends_block.")
    if name == "rfi":
        caveats.append("Block-end helper + ret.")

    named = name_mentioned(name, scan)
    extra = []
    if not any_ok:
        extra.append("not in `nw_jit_op_supported`")
        if named and name not in scan["helpers"]:
            extra.append("name appears in nw_jit.cpp (maybe hist/skip label only)")
        mill_hole = True
    else:
        extra.append("in allowlist")

    # kpx nop note for cache/stream ops that are allowlisted
    if "EXECUTE_0(nop)" in e["execute"] and any_ok and name not in EXCLUDE:
        if name in {"dcbf", "dcbt", "dcbtst", "dss", "dst", "dstst", "dcba", "dcbi", "dcbst"}:
            extra.insert(0, "kpx nop.")

    # status mark
    if not any_ok:
        mark, status = "[ ]", "todo"
    else:
        form_caveat = bool(
            gates_fail
            or name in ENDS_RESTRICT
            or (name in HINT_ALIAS)
            or (name == "mftb")
            or (name == "mfspr")
        )
        # RA=0 and L=1 are form caveats per legend
        if any(x in gates_fail for x in ("RA=0", "L=1", "Rc=1", "AA=1", "BO=CTR")):
            form_caveat = True
        if name in ENDS_RESTRICT or name in ("rfi", "mtmsr", "mtsr", "mtsrin", "tlbie", "tlbia", "icbi"):
            form_caveat = True
        if form_caveat:
            mark, status = "[~]", "partial"
        else:
            mark, status = "[x]", "done"

    return {
        "mark": mark,
        "status": status,
        "in_allowlist": any_ok,
        "has_emit": emit,
        "ends": ends,
        "caveats": caveats,
        "ok": gates_ok,
        "fail": gates_fail,
        "mill_hole": mill_hole and name not in EXCLUDE,
        "notes_extra": extra,
        "results": results,
    }


def assign_wave(e: dict, c: dict) -> str:
    name = e["name"]
    if name in EXCLUDE:
        return "exclude"
    if c["status"] == "done":
        return "—"
    # remaining mill holes
    if name in W1_BRANCH and c["mill_hole"]:
        return "W1"
    if name in ENDS_RESTRICT:
        return "W6"
    if name == "vmsumshs":
        return "W6"
    if name in HINT_ALIAS:
        return "W6"
    # form-reject only (L, RA=0) and intentional ends_block: not a mill wave
    form_only = set(c["fail"]) <= {"RA=0", "L=1"} and not (
        set(c["fail"]) & {"Rc=1", "AA=1", "BO=CTR"}
    )
    if c["status"] == "partial" and form_only and name not in W1_BRANCH:
        if name in L_COMPARE or name in RA_UPDATE:
            return "—"
    if name in ("mftb", "mfspr", "mtspr") and c["status"] == "partial":
        return "—"
    if name in ("rfi", "mtmsr", "mtsr", "mtsrin", "tlbie", "tlbia", "icbi") and c["in_allowlist"]:
        return "—"
    if c["status"] == "todo":
        if name in W2_INTEGER or name in {"tlbsync"}:
            return "W2"
        if name in W3_MEM or (e.get("family") == "mem" and name.endswith("x") and "br" in name):
            return "W3"
        if e.get("family") == "fp" or name in RA_UPDATE and e.get("family") == "fp":
            return "W4"
        if name in ALTIVEC_ELEM_MEM:
            return "W5"
        if name in ALTIVEC_FP:
            return "W7"
        if e.get("family") == "altivec":
            return "W6"
        if e.get("family") == "mem":
            return "W3"
        if e.get("family") in ("integer", "spr"):
            return "W2"
        return "W8"
    if c["status"] == "partial":
        if "Rc=1" in c["fail"]:
            return "W4"
        if e.get("family") == "fp" and form_only:
            return "—"
        if e.get("family") == "fp":
            return "W4"
        if e.get("family") == "mem" and name in RA_UPDATE:
            return "—"
        if e.get("family") == "altivec":
            return "W6"
        return "W8"
    return "—"


def encoding_cell(e: dict) -> str:
    return f"`{e['form']}` {e['prim']}/{e['xo']}"


def notes_cell(e: dict, c: dict) -> str:
    parts = [f"kpx: `{e['execute']}`"]
    parts.append(f"{e['form']} prim={e['prim']} xo={e['xo']} {e['cflow']}")
    parts.extend(c["caveats"])
    if c["ends"] and e["name"] in {
        "b", "bc", "bcctr", "bclr", "rfi", "sc", "isync",
        "mtmsr", "mtsr", "mtsrin", "tlbie", "icbi", "vslo", "vsro",
    }:
        if not any(p == "**ends_block**" for p in parts):
            parts.append("**ends_block**")
    if c["ok"] or c["fail"]:
        bits = []
        if c["ok"]:
            bits.append("ok=" + ",".join(c["ok"]))
        if c["fail"]:
            bits.append("fail=" + ",".join(c["fail"]))
        # only emit allowlist ok/fail when there is a selective gate
        if c["fail"]:
            parts.append("allowlist " + "; ".join(bits).replace("; fail=", "; fail="))
            # prefer compact: allowlist ok=X; fail=Y
            compact = "allowlist "
            if c["ok"]:
                compact += "ok=" + ",".join(c["ok"])
            if c["fail"]:
                if c["ok"]:
                    compact += "; "
                compact += "fail=" + ",".join(c["fail"])
            # replace the previous append
            parts[-1] = compact
    if c["notes_extra"]:
        # avoid duplicating allowlist line
        parts.extend(c["notes_extra"])
    # unique preserve order
    out: list[str] = []
    seen = set()
    for p in parts:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return "; ".join(out)


def md_escape_cell(s: str) -> str:
    return s.replace("|", "\\|")


def main() -> int:
    decode = DECODE.read_text()
    nw = NW_JIT.read_text()
    entries = parse_decode_table(decode)
    if len(entries) != 345:
        print(f"warning: parsed {len(entries)} entries, expected 345", file=sys.stderr)
    fam_map = load_family_map(OUT)
    scan = scan_nw_jit(nw)
    rows = []
    for e in entries:
        e["family"] = infer_family(e, fam_map)
        c = classify(e, scan)
        e["wave"] = assign_wave(e, c)
        e["class"] = c
        rows.append(e)

    # counts
    status_count = Counter(e["class"]["status"] for e in rows)
    fam_count = Counter(e["family"] for e in rows)
    today = _dt.date.today().isoformat()

    lines: list[str] = []
    lines.append("# SheepShaver NW JIT — opcode checklist")
    lines.append("")
    lines.append(
        f"Generated **{today}** from `powerpc_ii_table` in "
        f"`SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp` ({len(rows)} names) against "
        f"`nw_jit_op_supported` / `nw_jit_op_ends_block` in `SheepShaver/src/nw_jit.cpp`."
    )
    lines.append("")
    lines.append(
        "Regenerate: `python3 tools/gen_jit_ops_md.py` (ports `nw_jit_op_supported` against `powerpc_ii_table`)."
    )
    lines.append("")
    lines.append("## Status legend")
    lines.append("")
    lines.append("| Mark | Meaning |")
    lines.append("|------|---------|")
    lines.append(
        "| `[x]` | **done** — representative encoding(s) pass the allowlist; no known form caveat; AltiVec name referenced in `nw_jit.cpp` |"
    )
    lines.append(
        "| `[~]` | **partial** — allowlisted with a form caveat (Rc, RA≠0, BO, AA, ends_block), or AltiVec XO hit without a named emit reference |"
    )
    lines.append(
        "| `[ ]` | **todo** — not in `nw_jit_op_supported` (falls to interpreter / `skip_unsup`) |"
    )
    lines.append("| `[-]` | **exclude** — do not mill |")
    lines.append("")
    lines.append("**Wave** follows the mill-everything plan (W1 hot partials → W7 AltiVec FP → W8 sweep).")
    lines.append("")
    lines.append("A checked box means the JIT **will attempt** the op. It is not a forever VERIFY sign-off.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Status | Count |")
    lines.append("|--------|------:|")
    for key, label in (("done", "done"), ("partial", "partial"), ("todo", "todo"), ("exclude", "exclude")):
        lines.append(f"| {label} | {status_count[key]} |")
    lines.append(f"| **total** | **{len(rows)}** |")
    lines.append("")
    lines.append("| Family | Count |")
    lines.append("|--------|------:|")
    for fam in ("altivec", "integer", "fp", "mem", "control", "spr", "none"):
        lines.append(f"| {fam} | {fam_count[fam]} |")
    lines.append("")
    lines.append("## Checklist")
    lines.append("")
    lines.append("| | Op | Family | Wave | Encoding | Notes |")
    lines.append("|---|----|--------|------|----------|-------|")

    fam_order = {"altivec": 0, "control": 1, "fp": 2, "integer": 3, "mem": 4, "none": 5, "spr": 6}
    ordered = sorted(rows, key=lambda e: (fam_order.get(e["family"], 9), e["name"]))
    for e in ordered:
        c = e["class"]
        lines.append(
            f"| {c['mark']} | `{e['name']}` | {e['family']} | {e['wave']} | "
            f"{encoding_cell(e)} | {md_escape_cell(notes_cell(e, c))} |"
        )

    lines.append("")
    lines.append("## Todo / partial by wave")
    lines.append("")

    wave_order = ["W1", "W2", "W3", "W4", "W5", "W6", "W7", "W8", "exclude"]
    by_wave: dict[str, list] = defaultdict(list)
    for e in rows:
        if e["class"]["status"] in ("todo", "partial", "exclude"):
            w = e["wave"]
            if w == "—":
                # still list mill-relevant partials under a synthetic bucket? keep them out of hole lists
                continue
            by_wave[w].append(e)

    for w in wave_order:
        items = by_wave.get(w, [])
        if not items:
            continue
        # preserve decode-table-ish but group by name
        items = sorted(items, key=lambda e: (fam_order.get(e["family"], 9), e["name"]))
        title = w if w != "exclude" else "exclude"
        lines.append(f"### {title} ({len(items)})")
        lines.append("")
        for e in items:
            c = e["class"]
            lines.append(f"- {c['mark']} `{e['name']}` — {notes_cell(e, c)}")
        lines.append("")

    done = sorted(e["name"] for e in rows if e["class"]["status"] == "done")
    lines.append("## Already done (checked)")
    lines.append("")
    lines.append("Quick list of `[x]` names for scanning:")
    lines.append("")
    # 10 per line like the original
    chunk = []
    wrapped: list[str] = []
    for i, n in enumerate(done, 1):
        chunk.append(f"`{n}`")
        if i % 10 == 0:
            wrapped.append(", ".join(chunk))
            chunk = []
    if chunk:
        wrapped.append(", ".join(chunk))
    lines.append("\n".join(wrapped))
    lines.append("")
    lines.append("## Notes on reading encodings")
    lines.append("")
    lines.append("- **prim/xo** come from the kpx decode table (`D_form` xo is often 0; real primary opcode is `prim`).")
    lines.append("- **AltiVec VX/VXR**: table `xo` is the 11-bit vector opcode field (`op & 0x7ff`).")
    lines.append("- **AltiVec VA**: table `xo` is the 6-bit VA opcode (`op & 0x3f`); allowlist also has a small VA set (vperm/vsldoi/…).")
    lines.append("- **kpx** line is the interpreter execute template — use it as the semantic oracle when milling.")
    lines.append("- Status is derived from a Python port of `nw_jit_op_supported`; keep the C function as source of truth.")
    lines.append("")

    OUT.write_text("\n".join(lines) if lines[-1] == "" else "\n".join(lines) + "\n")
    print(
        f"wrote {OUT} names={len(rows)} done={status_count['done']} "
        f"partial={status_count['partial']} todo={status_count['todo']} "
        f"exclude={status_count['exclude']}"
    )
    if len(rows) != 345:
        return 1
    if sum(status_count[k] for k in ("done", "partial", "todo", "exclude")) != len(rows):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
