#!/usr/bin/env python3
"""Disassemble the local OS 9.2.1 New World ROM for G3 mill targeting.

Reads the ROM path from SheepShaver prefs. Never copies ROM/disk into git.
Does not launch SheepShaver.

  python3 research-score/g3_driver/rom_disasm.py
  python3 research-score/g3_driver/g3_driver.py rom
  python3 research-score/g3_driver/rom_disasm.py --off 0x5c86e --count 16
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from debug_run import prefs_path
from mill_apply import (
    A190_DATA_HI,
    A190_DATA_LO,
    HARD_SKIP_OFFS,
    LOOK_AGAIN_SKIP_68K,
    NO_SKIP_68K_OPS,
    OFF_68K,
    UI_SKIP_68K_HI,
    UI_SKIP_68K_LO,
    leftover_map_remaining,
    next_skip_68k_off,
    skip_68k_loop_op,
    skip_68k_millable,
    skip_68k_ui_op,
)

ROM_SIZE = 0x400000
ROM_BASE = 0x50000000
NK_OFF = 0x310000
NEWWORLD_OFF = 0x30D064
FOURCC_PRCL = 0x7072636C
FOURCC_ROM = 0x726F6D20

# 68k emulator + jump table: not a guest 68k PC (g3_r24_ok).
JT_RANGES = (
    (0x350000, 0x400000, "68k-emulator-JT"),
    (0x400000, 0x500000, "rom-extra-copy"),
    (0x00A9E0, 0x00AA7E, "overlay-pad"),
    (0x00A942, 0x00AF3E, "SANE-disp"),
    (0x00BF62, 0x00D5F4, "SCSI/VIA"),
    (0x025FC0, 0x026006, "field-thunks"),
)

# Mill-seen + installer-adjacent A-lines. Names follow KEEP mill logs.
A_LINE = {
    0xA000: "Open",
    0xA001: "Close",
    0xA002: "Read",
    0xA003: "Write",
    0xA009: "GetFileInfo",
    0xA00A: "SetFileInfo",
    0xA013: "GetEOF",
    0xA014: "SetEOF",
    0xA01F: "GetEOF",
    0xA023: "GetFPos",
    0xA044: "SetFPos",
    0xA050: "FlushVol",
    0xA051: "ReadDateTime",
    0xA054: "AddDrive",
    0xA060: "HFSDispatch",
    0xA031: "GetOSEvent",
    0xA198: "HOpen",
    0xA200: "HOpen",
    0xA207: "HGetFileInfo",
    0xA20A: "HSetFPos",
    0xA260: "HFSDispatch",
    0xA450: "Read",
    0xA873: "SetPort",
    0xA88F: "InitCursor",
    0xA8D9: "CloseRgn",
    0xA8A7: "SetRect",
    0xA8FE: "InitGraf",
    0xA8FF: "OpenPort",
    0xA86D: "OpenPort",
    0xA86E: "InitPort",
    0xA8A2: "NewWindow",
    0xA8A3: "DisposeWindow",
    0xA914: "GetNewWindow",
    0xA91F: "GetNewControl",
    0xA97B: "GetCursor",
    0xA97C: "GetCCursor",
    0xA983: "DisposeDialog",
    0xA985: "NewDialog",
    0xA97D: "GetNewDialog",
    0xAA68: "DialogDispatch",
    0xAA5A: "CodeFragmentDispatch",
    0xABE8: "InitCPort",
    0xABE9: "InitCPort",
    0xA22E: "BlockMove",
    0xA02E: "BlockMove",
    0xA146: "GetTrapAddress",
    0xA247: "GetOSTrapAddress",
    0xA346: "GetToolTrapAddress",
    0xA08E: "BTreeDispatch",
    0xA96F: "Enqueue",
    0xA01B: "GetVolInfo",
    0xA71E: "GetFCB",
    0xA06E: "OpenResFile",
    0xA9A0: "GetResource",
    0xA9A2: "LoadResource",
    0xA9C9: "GetResource",
}

STRING_NEEDLES = (
    b"NewWorld",
    b"Mac OS",
    b"Finder",
    b"Installer",
    b"SANE",
    b"CFM",
    b"CodeFragment",
    b"Dialog",
    b"QuickDraw",
    b"InitCPort",
    b"ATA",
    b"ndrv",
    b"OpenFirmware",
    b"MacRISC",
)


def _be16(b: bytes, off: int) -> Optional[int]:
    if off < 0 or off + 2 > len(b):
        return None
    return (b[off] << 8) | b[off + 1]


def _be32(b: bytes, off: int) -> Optional[int]:
    if off < 0 or off + 4 > len(b):
        return None
    return (
        (b[off] << 24)
        | (b[off + 1] << 16)
        | (b[off + 2] << 8)
        | b[off + 3]
    )


def _s8(n: int) -> int:
    n &= 0xFF
    return n - 0x100 if n & 0x80 else n


def _s16(n: int) -> int:
    n &= 0xFFFF
    return n - 0x10000 if n & 0x8000 else n


def rom_path_from_prefs(prefs: Optional[Path] = None) -> Optional[Path]:
    p = Path(prefs) if prefs else prefs_path()
    if not p.is_file():
        return None
    try:
        for line in p.read_text(errors="replace").splitlines():
            if line.startswith("rom "):
                rom = Path(line.split(None, 1)[1].strip())
                if rom.is_file():
                    return rom
    except OSError:
        return None
    return None


def _lzss(src: bytes, dest_len: int = ROM_SIZE) -> bytes:
    dest = bytearray()
    dictionary = bytearray(0x1000)
    run_mask = 0
    dict_idx = 0xFEE
    i = 0
    size = len(src)
    while True:
        if run_mask < 0x100:
            size -= 1
            if size < 0 or i >= len(src):
                break
            run_mask = src[i] | 0xFF00
            i += 1
        bit = run_mask & 1
        run_mask >>= 1
        if bit:
            size -= 1
            if size < 0 or i >= len(src):
                break
            c = src[i]
            i += 1
            dictionary[dict_idx] = c
            dict_idx = (dict_idx + 1) & 0xFFF
            if len(dest) < dest_len:
                dest.append(c)
        else:
            size -= 1
            if size < 0 or i + 1 >= len(src):
                break
            idx = src[i]
            i += 1
            size -= 1
            cnt = src[i]
            i += 1
            idx |= (cnt << 4) & 0xF00
            cnt = (cnt & 0x0F) + 3
            while cnt:
                cnt -= 1
                c = dictionary[idx & 0xFFF]
                idx = (idx + 1) & 0xFFF
                dictionary[dict_idx] = c
                dict_idx = (dict_idx + 1) & 0xFFF
                if len(dest) < dest_len:
                    dest.append(c)
        if len(dest) >= dest_len:
            break
    if len(dest) < dest_len:
        dest.extend(b"\x00" * (dest_len - len(dest)))
    return bytes(dest[:dest_len])


def _chrp_hex(src: bytes, name: str) -> Optional[int]:
    needle = ("constant " + name).encode("ascii")
    i = src.find(needle)
    if i < 7:
        return None
    chunk = src[i - 7 : i]
    try:
        return int(chunk.decode("ascii", "ignore")[:6], 16)
    except ValueError:
        return None


def _parcels(src: bytes, dest_len: int = ROM_SIZE) -> Optional[bytes]:
    dest = bytearray(dest_len)
    parcel_offset = 0x14
    decoded = False
    while parcel_offset != 0 and parcel_offset + 12 <= len(src):
        nxt = _be32(src, parcel_offset)
        ptype = _be32(src, parcel_offset + 4)
        if nxt is None or ptype is None:
            break
        if ptype == FOURCC_ROM:
            lzss_off = _be32(src, parcel_offset + 8)
            if lzss_off is None:
                return None
            parcel_end = nxt if nxt else len(src)
            if parcel_end <= parcel_offset + lzss_off:
                return None
            blob = src[parcel_offset + lzss_off : parcel_end]
            out = _lzss(blob, dest_len)
            dest[:] = out
            decoded = True
        if nxt == 0 or nxt <= parcel_offset:
            break
        parcel_offset = nxt
    return bytes(dest) if decoded else None


def decode_rom_image(src: bytes) -> Optional[bytes]:
    """Same G0 cases as nw_decode_rom_image: 4 MiB plain, CHRP lzss, parcels."""
    if len(src) == ROM_SIZE:
        return src
    if len(src) < 11 or src[:11] != b"<CHRP-BOOT>":
        if len(src) >= ROM_SIZE:
            return src[:ROM_SIZE]
        return None
    off = _chrp_hex(src, "lzss-offset")
    size = _chrp_hex(src, "lzss-size")
    if off is None or size is None:
        off = _chrp_hex(src, "parcels-offset")
        size = _chrp_hex(src, "parcels-size")
    if off is None or size is None or size == 0:
        return None
    if off + size > len(src):
        return None
    blob = src[off : off + size]
    sig = _be32(blob, 0)
    if sig == FOURCC_PRCL:
        return _parcels(blob)
    return _lzss(blob)


def load_rom(path: Optional[Path] = None) -> Tuple[bytes, Path]:
    rom_path = path or rom_path_from_prefs()
    if rom_path is None or not rom_path.is_file():
        raise FileNotFoundError("ROM not found (prefs rom line). Do not commit ROM.")
    raw = rom_path.read_bytes()
    decoded = decode_rom_image(raw)
    if decoded is None or len(decoded) < ROM_SIZE:
        raise ValueError("DecodeROM failed (need 4 MiB New World or CHRP lzss/parcels)")
    return decoded[:ROM_SIZE], rom_path


def aline_name(op: int) -> str:
    op &= 0xFFFF
    if op in A_LINE:
        return A_LINE[op]
    if 0xA000 <= op <= 0xAFFF:
        return "A-line"
    return ""


def disasm_68k_one(rom: bytes, off: int) -> Dict[str, Any]:
    op = _be16(rom, off)
    if op is None:
        return {
            "off": off,
            "size": 0,
            "op": None,
            "text": "(end)",
            "kind": "end",
            "dest": None,
        }
    size = 2
    dest: Optional[int] = None
    kind = "op"
    text = "dc.w $%04x" % op
    if 0xA000 <= op <= 0xAFFF:
        kind = "aline"
        name = aline_name(op)
        text = "_%s $%04x" % (name, op) if name != "A-line" else "A-line $%04x" % op
    elif op == 0x4E75:
        kind = "rts"
        text = "RTS"
    elif op == 0x4E71:
        text = "NOP"
    elif op == 0x4E73:
        text = "RTE"
    elif op == 0x4E56:
        d = _be16(rom, off + 2)
        size = 4
        text = "LINK A6,#%d" % (_s16(d or 0))
        kind = "link"
    elif op == 0x4E5E:
        text = "UNLK A6"
        kind = "unlk"
    elif op == 0x4EBA:
        kind = "jsr_pc"
        d = _be16(rom, off + 2)
        size = 4
        if d is not None:
            dest = (off + 2 + _s16(d)) & 0xFFFFFFFF
            text = "JSR %d(PC) ; dest=0x%x" % (_s16(d), dest)
    elif op == 0x4EB9:
        kind = "jsr_abs"
        a = _be32(rom, off + 2)
        size = 6
        dest = a
        text = "JSR $%08x" % (a or 0)
    elif (op & 0xF1FF) == 0x203C:
        imm = _be32(rom, off + 2)
        size = 6
        text = "MOVE.L #$%08x,D%d" % (imm or 0, (op >> 9) & 7)
        kind = "move"
    elif (op & 0xF000) == 0x7000:
        text = "MOVEQ #%d,D%d" % (_s8(op & 0xFF), (op >> 9) & 7)
        kind = "move"
    elif op == 0x4EFA:
        kind = "jmp_pc"
        d = _be16(rom, off + 2)
        size = 4
        if d is not None:
            dest = (off + 2 + _s16(d)) & 0xFFFFFFFF
            text = "JMP %d(PC) ; dest=0x%x" % (_s16(d), dest)
    elif op == 0x4EF9:
        kind = "jmp_abs"
        a = _be32(rom, off + 2)
        size = 6
        dest = a
        text = "JMP $%08x" % (a or 0)
    elif op == 0x4EFB:
        kind = "jmp_pc"
        d = _be16(rom, off + 2)
        size = 4
        text = "JMP %d(PC,Xn)" % (_s16(d) if d is not None else 0)
    elif (op & 0xFF00) == 0x6000:
        disp8 = op & 0xFF
        cc = (op >> 8) & 0xF
        names = {
            0: "BRA",
            1: "BSR",
            2: "BHI",
            3: "BLS",
            4: "BCC",
            5: "BCS",
            6: "BNE",
            7: "BEQ",
            8: "BVC",
            9: "BVS",
            10: "BPL",
            11: "BMI",
            12: "BGE",
            13: "BLT",
            14: "BGT",
            15: "BLE",
        }
        name = names.get(cc, "Bcc")
        if disp8 == 0:
            d = _be16(rom, off + 2)
            size = 4
            rel = _s16(d or 0)
            dest = (off + 2 + rel) & 0xFFFFFFFF
            text = "%s.W $%x" % (name, dest)
        elif disp8 == 0xFF:
            d = _be16(rom, off + 2)
            size = 4
            rel = _s16(d or 0)
            dest = (off + 2 + rel) & 0xFFFFFFFF
            kind = "bra_w"
            text = "%s.W $%x" % (name, dest)
        else:
            rel = _s8(disp8)
            dest = (off + 2 + rel) & 0xFFFFFFFF
            kind = "bra_s"
            text = "%s.S $%x" % (name, dest)
            if dest == off:
                kind = "bra_star"
                text += " ; *"
    elif (op & 0xFFF0) == 0x4E40:
        text = "TRAP #$%x" % (op & 0xF)
        kind = "trap"
    elif op == 0x4E74:
        d = _be16(rom, off + 2)
        size = 4
        text = "RTD #%d" % (_s16(d or 0))
        kind = "rts"
    else:
        text = "dc.w $%04x" % op
        kind = "raw"
    if skip_68k_loop_op(op) and kind not in ("rts", "aline", "link", "move"):
        kind = "loop"
    return {
        "off": off,
        "size": size,
        "op": op,
        "text": text,
        "kind": kind,
        "dest": dest,
    }


def disasm_68k(rom: bytes, off: int, count: int = 12) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    cur = int(off)
    for _ in range(max(1, count)):
        ins = disasm_68k_one(rom, cur)
        out.append(ins)
        if ins["size"] <= 0:
            break
        cur += int(ins["size"])
        if ins["kind"] in ("rts", "jmp_abs"):
            break
    return out


def ppc_one(w: int) -> str:
    prim = (w >> 26) & 0x3F
    if prim == 18:
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        aa = (w >> 1) & 1
        lk = w & 1
        return "b%s%s %+d" % ("l" if lk else "", "a" if aa else "", li)
    if prim == 16:
        bd = (w >> 2) & 0x3FFF
        if bd & 0x2000:
            bd -= 0x4000
        return "bc %+d" % (bd << 2)
    if prim == 19:
        xo = (w >> 1) & 0x3FF
        if xo == 16:
            return "bclr"
        if xo == 528:
            return "bcctr"
        return "op19 xo=%d" % xo
    if prim == 31:
        xo = (w >> 1) & 0x3FF
        names = {0: "cmp", 32: "cmpl", 339: "mfspr", 467: "mtspr", 595: "mfsr", 659: "mfsrin"}
        return names.get(xo, "op31 xo=%d" % xo)
    if prim == 14:
        return "addi"
    if prim == 15:
        return "addis"
    if prim == 11:
        return "cmpi"
    if prim == 10:
        return "cmpli"
    if prim == 36:
        return "stw"
    if prim == 32:
        return "lwz"
    if prim == 24:
        return "ori"
    if prim == 21:
        return "rlwinm"
    return "op prim=%d" % prim


def disasm_ppc(rom: bytes, off: int, count: int = 8) -> List[str]:
    lines = []
    cur = int(off) & ~3
    for _ in range(max(1, count)):
        w = _be32(rom, cur)
        if w is None:
            break
        lines.append("  ppc 0x%x: %08x  %s" % (cur, w, ppc_one(w)))
        cur += 4
    return lines


def region_tag(off: int) -> Optional[str]:
    o = int(off)
    for a, b, name in JT_RANGES:
        if a <= o < b:
            return name
    if UI_SKIP_68K_LO <= o < UI_SKIP_68K_HI:
        return "ui-dialog-path"
    if A190_DATA_LO <= o < A190_DATA_HI:
        return "a190-data-table"
    if o in LOOK_AGAIN_SKIP_68K:
        return "look-again-keep"
    if 0x326000 <= o < 0x327000:
        return "nk-50326-wait"
    if o == OFF_68K:
        return "68k-interp"
    if NK_OFF <= o < 0x360000:
        return "nanokernel"
    return None


def classify_off(rom: bytes, off: int) -> Dict[str, Any]:
    ins = disasm_68k_one(rom, off)
    tag = region_tag(off)
    hard = int(off) in HARD_SKIP_OFFS
    millable = skip_68k_millable(off) and not hard
    loop = skip_68k_loop_op(ins.get("op")) or ins.get("kind") in (
        "loop",
        "bra_star",
        "rts",
    )
    note = []
    if hard:
        note.append("HARD-do-not-skip")
    if tag:
        note.append(tag)
    if not millable:
        note.append("not-skip-68k-millable")
    if loop:
        note.append("loop/return")
    if skip_68k_ui_op(ins.get("op")):
        note.append("no-skip-68k-ui-fs")
    if tag == "ui-dialog-path":
        note.append("do-not-skip-68k")
    if tag == "a190-data-table":
        note.append("data-not-code")
    if tag == "look-again-keep":
        note.append("undo-skip-later")
    if ins.get("kind") == "aline":
        note.append("toolbox-trap")
    if tag == "68k-emulator-JT":
        note.append("do-not-r24-divert")
    return {
        "off": off,
        "pc": ROM_BASE + off,
        "ins": ins,
        "tag": tag,
        "hard": hard,
        "millable": millable,
        "loop": loop,
        "note": ",".join(note) or "code",
    }


def g0_ok(rom: bytes) -> bool:
    return rom[NEWWORLD_OFF : NEWWORLD_OFF + 8] == b"NewWorld"


def interesting_strings(rom: bytes, limit: int = 24) -> List[str]:
    hits: List[str] = []
    region = rom[:0x360000]
    for needle in STRING_NEEDLES:
        start = 0
        found = 0
        while found < 3 and len(hits) < limit:
            i = region.find(needle, start)
            if i < 0:
                break
            lo = max(0, i - 16)
            hi = min(len(region), i + len(needle) + 24)
            chunk = region[lo:hi]
            printable = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
            hits.append("  +0x%x  %s" % (i, printable))
            start = i + 1
            found += 1
    return hits[:limit]


def _load_state() -> Dict[str, Any]:
    p = HERE / "state.json"
    if not p.is_file():
        return {}
    try:
        return json.loads(p.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def mill_targets(st: Dict[str, Any], n: int = 12) -> List[int]:
    mill = (st.get("mill") or {}) if st else {}
    remain, _pref, _n = leftover_map_remaining(
        mill, mill.get("tested"), mill.get("reverted_kinds"), limit=n
    )
    out: List[int] = []
    seen = set()
    nxt = next_skip_68k_off(mill, mill.get("tested"), mill.get("reverted_kinds"))
    if nxt is not None:
        out.append(int(nxt))
        seen.add(int(nxt))
    for o in remain:
        if int(o) in seen:
            continue
        seen.add(int(o))
        out.append(int(o))
        if len(out) >= n:
            break
    return out


def format_report(
    rom: bytes,
    offs: List[int],
    count: int = 12,
    nk: bool = True,
) -> str:
    lines: List[str] = []
    a = lines.append
    a("# G3 ROM disasm (local prefs ROM; not written to git)")
    a("")
    a("- size: 4 MiB decoded")
    a("- G0 NewWorld +0x%x: %s" % (NEWWORLD_OFF, "yes" if g0_ok(rom) else "NO"))
    a("- NK +0x%x first word: %s" % (NK_OFF, "%08x" % (_be32(rom, NK_OFF) or 0)))
    a("- 68k interp hang PPC +0x%x: %s" % (OFF_68K, ppc_one(_be32(rom, OFF_68K) or 0)))
    a("")
    a("## Strings (mill-relevant, capped)")
    a("")
    ss = interesting_strings(rom)
    if ss:
        lines.extend(ss)
    else:
        a("- (none)")
    a("")
    if nk:
        a("## HARD / NK 50326 (PPC, do not skip-list)")
        a("")
        for off in sorted(HARD_SKIP_OFFS | {0x3265A4, 0x326510, 0x326458}):
            a("- off=0x%x pc=5%07x %s" % (off, off, region_tag(off) or ""))
            lines.extend(disasm_ppc(rom, off, 6))
        a("")
    a("## Do not skip-68k (ROM findings)")
    a("")
    a(
        "- UI path 0x%x-0x%x: GetCCursor/DialogDispatch/SetPort/DisposeDialog (WINDOW)"
        % (UI_SKIP_68K_LO, UI_SKIP_68K_HI)
    )
    a(
        "- $a190 data 0x%x-0x%x: repeating table, not code"
        % (A190_DATA_LO, A190_DATA_HI)
    )
    a(
        "- no-skip A-lines: "
        + ", ".join("$%04x" % o for o in sorted(NO_SKIP_68K_OPS))
    )
    a("- look-again KEEP (undo skip later; do not remill skip-68k):")
    for off in sorted(LOOK_AGAIN_SKIP_68K):
        c = classify_off(rom, off)
        ins = c["ins"]
        a(
            "  - 0x%x pc=%08x %s [%s]"
            % (
                off,
                c["pc"],
                ins["text"],
                c["note"],
            )
        )
    a("")
    a("## 68k sites")
    a("")
    if not offs:
        a("- (none)")
    for off in offs:
        c = classify_off(rom, off)
        ins = c["ins"]
        a(
            "### 0x%x pc=%08x op=%s  %s  [%s]"
            % (
                off,
                c["pc"],
                ("%04x" % ins["op"]) if ins.get("op") is not None else "-",
                ins["text"],
                c["note"],
            )
        )
        a("- millable=%s loop=%s hard=%s tag=%s" % (c["millable"], c["loop"], c["hard"], c["tag"] or "-"))
        for ins2 in disasm_68k(rom, off, count):
            dest = ""
            if ins2.get("dest") is not None:
                dest = " -> 0x%x" % ins2["dest"]
            a(
                "    +0x%x  %s%s"
                % (ins2["off"], ins2["text"], dest)
            )
        a("")
    a("Do not commit ROM/disk. Do not skip HARD offs. Do not r24-divert CODE 0 JT.")
    a("")
    return "\n".join(lines)


def parse_off(s: str) -> int:
    s = s.strip().lower()
    if s.startswith("0x"):
        return int(s, 16)
    if re.fullmatch(r"[0-9a-f]+", s) and any(c in s for c in "abcdef"):
        return int(s, 16)
    return int(s, 0) if s.startswith("0") else int(s, 10)


def cmd_rom(args: argparse.Namespace) -> int:
    try:
        rom, path = load_rom(Path(args.rom) if getattr(args, "rom", None) else None)
    except (FileNotFoundError, ValueError, OSError) as e:
        print("rom-disasm fail: %s" % e)
        return 2
    print("rom-disasm loaded %d bytes from prefs (path not copied into git)" % len(rom))
    print("rom file: %s" % path)
    offs: List[int] = []
    if getattr(args, "off", None):
        for raw in args.off:
            offs.append(parse_off(raw))
    else:
        st = _load_state()
        offs = mill_targets(st, n=int(getattr(args, "next", 12) or 12))
        mill = st.get("mill") or {}
        keep = mill.get("keep_log")
        if keep:
            print("keep_log: %s keep_pc=%s" % (keep, "%x" % (mill.get("keep_pc") or 0)))
    count = int(getattr(args, "count", 12) or 12)
    sys.stdout.write(format_report(rom, offs, count=count, nk=not getattr(args, "no_nk", False)))
    return 0


def build_argparser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default=None, help="ROM file (default: prefs rom line)")
    ap.add_argument("--off", action="append", default=[], help="ROM offset (repeatable)")
    ap.add_argument("--next", type=int, default=12, help="next skip-68k offs from state")
    ap.add_argument("--count", type=int, default=12, help="68k insns per site")
    ap.add_argument("--no-nk", action="store_true")
    return ap


def main() -> int:
    return cmd_rom(build_argparser().parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
