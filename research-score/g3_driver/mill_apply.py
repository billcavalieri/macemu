#!/usr/bin/env python3
"""Canned class mills. Skip-68k policy from NewWorldView mill-annotations.json. Hang-cap only after apply."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from mill_log import read_log, read_log_tail, resolve_log

HERE = Path(__file__).resolve().parent

MARKER_STW = "G3: DEC leave 50326 stw+mfsr skip"

MILL_FILES = (
    "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp",
    "SheepShaver/src/nw_boot_contract.cpp",
    "SheepShaver/src/include/nw_boot_contract.h",
    "SheepShaver/src/kpx_cpu/tests/mmu_harness.cpp",
)


def repo_root() -> Path:
    return HERE.parents[1]


def cpu_path(root: Optional[Path] = None) -> Path:
    r = Path(root) if root else repo_root()
    return r / "SheepShaver" / "src" / "kpx_cpu" / "src" / "cpu" / "ppc" / "ppc-cpu.cpp"


WAIT0_BODY = (
    "static int nw_dec_leave_50326564_wait(uint32 rom_off)\n"
    "{\n"
    "	(void)rom_off;\n"
    "	return 0;\n"
    "}"
)

MARKER_MFSR = "G3: DEC leave 50326 mfsr skip"
MARKER_POISON = "G3: poison callback skip"
MARKER_HANG_SKIP = "G3: KEEP hang skip"
MARKER_68K_KEEP = "G3: KEEP 68k hang"
MARKER_READ_NOERR = "G3: 68k Read A002/A450 noErr"
MARKER_SETFPOS_NOERR = "G3: 68k SetFPos A044 noErr"
MARKER_SLOT_26E90 = "G3: 68k skip slot helper 0x26e90"
MARKER_SPIN_26E88 = "G3: 68k skip 1adc mill 0x26e88"
MARKER_CFM_AA5A = "G3: 68k CFM AA5A sel=65532 native"
MARKER_TRAP_68K = "G3: 68k A-line default native"
MARKER_REENTER_68K = "G3: 68k reenter from hang"
MARKER_DISPOSEPTR = "G3: 68k DisposePtr A01F"
MARKER_FIXMUL = "G3: 68k FixMul A868"
MARKER_INITFONTS = "G3: 68k InitFonts A8FE"
MARKER_STAY_CODE66 = "G3: 68k stay CODE 66"
MARKER_GETRESOURCE_A9A0 = "G3: 68k GetResource A9A0 toast"
MARKER_GETNEWDIALOG_DLOG = "G3: 68k GetNewDialog A97C toast"
MARKER_CODE66_SYSERR99 = "G3: 68k CODE 66 SysError 99 continue"
MARKER_CODE66_RESUME = "G3: 68k LoadSeg A9F0 CODE 66 resume"
MARKER_CODE66_ALLOW_9440 = "G3: 68k CODE 66 allow 0x9440"
MARKER_LAUNCH_UPGRADER = "G3: 68k Launch A9F2 CFM Upgrader"
MARKER_SPLASH_510 = "G3: 68k GetNewDialog A97C Splash 510"
MARKER_SPLASH_510_EVEN = "G3: 68k GetNewDialog A97C Splash 510 even"
MARKER_PICT_1000 = "G3: 68k DrawPicture A8F6 PICT 1000"
MARKER_PEF_UPGRADER = "G3: 68k Launch A9F2 CFM Upgrader PEF"
MARKER_PEF_ENTER = "G3: 68k Launch A9F2 CFM Upgrader PEF enter"
MARKER_PEF_IMPORTS = "G3: 68k Launch A9F2 CFM Upgrader PEF import"
MARKER_PEF_SYSENV = "G3: 68k Launch A9F2 CFM Upgrader PEF SysEnvirons"
MARKER_PEF_VOL = "G3: 68k Launch A9F2 CFM Upgrader PEF vol"
MARKER_PEF_DCE = "G3: 68k Launch A9F2 CFM Upgrader PEF dce"
MARKER_PEF_WAIT = "G3: 68k Launch A9F2 CFM Upgrader PEF WaitNextEvent"
MARKER_PEF_TE = "G3: 68k Launch A9F2 CFM Upgrader PEF TENew"
MARKER_PEF_TEREC = "G3: 68k Launch A9F2 CFM Upgrader PEF TERec"
MARKER_PEF_SKIPTE = "G3: 68k Launch A9F2 CFM Upgrader PEF skipTE"
MARKER_PEF_SKIPDI = "G3: 68k Launch A9F2 CFM Upgrader PEF skipDI"
MARKER_PEF_IDX = "G3: 68k Launch A9F2 CFM Upgrader PEF idx"
MARKER_PEF_GND = "G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog 510"
MARKER_PEF_GNDID = "G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id="
MARKER_PEF_D519 = "G3: 68k Launch A9F2 CFM Upgrader PEF DLOG 519"
MARKER_PEF_MODAL = "G3: 68k Launch A9F2 CFM Upgrader PEF ModalDialog"
MARKER_PEF_NO519 = "G3: 68k Launch A9F2 CFM Upgrader PEF no519"
MARKER_PEF_SHOW = "G3: 68k Launch A9F2 CFM Upgrader PEF ShowWindow"
MARKER_PEF_FORCESPLASH = "G3: 68k Launch A9F2 CFM Upgrader PEF forceSplash"
MARKER_PEF_SKIPWAIT = "G3: 68k Launch A9F2 CFM Upgrader PEF skipWait"
MARKER_PEF_CALLSPLASH = "G3: 68k Launch A9F2 CFM Upgrader PEF callSplash"
MARKER_PEF_SKIPALERT = "G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert"
MARKER_PEF_NIMP = "G3: 68k Launch A9F2 CFM Upgrader PEF nimp"
MARKER_PEF_JUMPSPLASH = "G3: 68k Launch A9F2 CFM Upgrader PEF jumpSplash"
MARKER_PEF_PLANTSPLASH = "G3: 68k Launch A9F2 CFM Upgrader PEF plantSplash"
MARKER_PEF_FORCEBLIT = "G3: 68k Launch A9F2 CFM Upgrader PEF forceBlit"
MARKER_PEF_BLITOFF = "G3: 68k Launch A9F2 CFM Upgrader PEF blitOff"
MARKER_PEF_CALLGND = "G3: 68k Launch A9F2 CFM Upgrader PEF callGnd"
MARKER_PEF_SKIPAE = "G3: 68k Launch A9F2 CFM Upgrader PEF skipAE"
OFF_3265A4 = 0x3265A4
OFF_326458 = 0x326458

KINDS = {
    "false-stw-spr": ["skip-pair", "execute-pair", "skip-mfsr"],
}

# After class kinds are reverted. Do not remill skip-pair / skip-mfsr.
# skip-hang mills KEEP hang pc (mill-4: 50326510), not 0x326564 / 0x3264fc.
# keep-68k / read-noerr / setfpos-noerr: mill-22 left 50326 for 68k pc=50366084.
LEFTOVER = [
    "poison-skip",
    "unstick-stw",
    "skip-hang",
    "keep-68k",
    "read-noerr",
    "setfpos-noerr",
    "slot-26e90",
    "skip-3265a4",
    "spin-26e88",
    "skip-326458",
    "stay-code66",
    "launch-upgrader",
    "splash-510",
    "splash-510-even",
    "pict-1000",
    "pef-upgrader",
    "pef-enter",
    "pef-imports",
    "pef-sysenv",
    "pef-vol",
    "pef-dce",
    "pef-wait",
    "pef-te",
    "pef-terec",
    "pef-skipte",
    "pef-skipdi",
    "pef-idx",
    "pef-gnd",
    "pef-gndid",
    "pef-d519",
    "pef-modal",
    "pef-no519",
    "pef-show",
    "pef-forcesplash",
    "pef-skipwait",
    "pef-callsplash",
    "pef-skipalert",
    "pef-nimp",
    "pef-jumpsplash",
    "pef-plantsplash",
    "pef-forceblit",
    "pef-blitoff",
    "pef-callgnd",
    "pef-skipae",
    "skip-68k",
    "cfm-aa5a",
    "trap-68k",
    "reenter-68k",
    "fixmul-a868",
    "disposeptr-a01f",
    "initfonts-a8fe",
    "getresource-a9a0",
    "getnewdialog-dlog",
    "code66-syserr99",
    "code66-resume",
    "code66-allow-9440",
    "grok-escalate",
]
KIND_68K = (
    "keep-68k",
    "read-noerr",
    "setfpos-noerr",
    "slot-26e90",
    "skip-3265a4",
    "spin-26e88",
    "skip-326458",
    "stay-code66",
    "launch-upgrader",
    "splash-510",
    "splash-510-even",
    "pict-1000",
    "pef-upgrader",
    "pef-enter",
    "pef-imports",
    "pef-sysenv",
    "pef-vol",
    "pef-dce",
    "pef-wait",
    "pef-te",
    "pef-terec",
    "pef-skipte",
    "pef-skipdi",
    "pef-idx",
    "pef-gnd",
    "pef-gndid",
    "pef-d519",
    "pef-modal",
    "pef-no519",
    "pef-show",
    "pef-forcesplash",
    "pef-skipwait",
    "pef-callsplash",
    "pef-skipalert",
    "pef-nimp",
    "pef-jumpsplash",
    "pef-plantsplash",
    "pef-forceblit",
    "pef-blitoff",
    "pef-callgnd",
    "pef-skipae",
    "skip-68k",
    "cfm-aa5a",
    "trap-68k",
    "reenter-68k",
    "fixmul-a868",
    "disposeptr-a01f",
    "initfonts-a8fe",
    "getresource-a9a0",
    "getnewdialog-dlog",
    "code66-syserr99",
    "code66-resume",
    "code66-allow-9440",
)
OFF_68K = 0x366084
MARKER_68K_R24 = "G3: 68k r24 skip"
RE_SPIN_R24 = re.compile(r"68k spin r24=([0-9a-fA-F]+)(?: op=([0-9a-fA-F]+))?")
RE_MAP_R24 = re.compile(r"68k map r24=([0-9a-fA-F]+) op=([0-9a-fA-F]+)")
RE_TRAP_PC = re.compile(
    r"68k (DialogDispatch|GetCCursor|DisposeDialog|GetNewDialog|"
    r"NewDialog|SysError|NewCWindow|NewWindow|SetPort|CloseRgn|"
    r"InitCPort|GetResource|OpenResFile|InitCursor|GetEOF|GetFPos)"
    r".*\bpc=([0-9a-fA-F]+)"
)
NO_SKIP_68K_TRAP_NAMES = frozenset(
    {
        "GetNewDialog",
        "NewDialog",
        "GetCCursor",
        "DialogDispatch",
        "SetPort",
        "DisposeDialog",
        "CloseRgn",
        "OpenResFile",
        "GetResource",
        "SysError",
        "InitCursor",
        "GetEOF",
        "GetFPos",
        "Read",
        "SetFPos",
    }
)
RE_MILL_STAMP_68K = re.compile(r"G3-MILL-68K-0x([0-9a-fA-F]+)")
RE_SKIP68_DEFAULT = re.compile(
    r"if \(!skip68\)\s*\n\s*skip68 = 0x([0-9a-fA-F]+)u;"
)
KEEP_68K_LOG_NS = (1116, 680, 35, 22)
# ppc-cpu.cpp logs at most this many unique `68k map r24=` lines
# per hang-cap (bitmap still covers the whole 4MiB ROM). Not 4096.
MAP_LOG_CAP = 16384
# BRA.S *, JMP (xxx.W,PC), JMP (d8,PC,Xn), RTS — dispatch/returns, not G3.
# 0x1B8C6 is 4EFB (KEEP mill histogram); skip-68k REVERT. Mill with G3_68K_MILL_LOOPS=1.
LOOP_68K_OPS = frozenset({0x60FF, 0x4EFA, 0x4EFB, 0x4E75})

# HARD 0x3264fc; skip-pair/skip-mfsr PCs already reverted worse.
HARD_SKIP_OFFS = frozenset({0x3264FC, 0x326564, 0x326568})

# GetNewDialog ROM stub at 0x5c86c (trap A97C), then DialogDispatch/SetPort.
# Not GetCCursor (AA1B). KEEP 0x5c86e still hits DialogDispatch.
# REVERT 0x5c89a/0x5c8d4 — do not skip-68k this WINDOW path.
UI_SKIP_68K_LO = 0x5C86C
UI_SKIP_68K_HI = 0x5C8C0  # through RTS 0x5c8be

# ROM helper CODE 66 falls into (ppc-cpu g3_rom_9440). Not skip-68k.
CODE66_HELPER_LO = 0x9440
CODE66_HELPER_HI = 0x94D0  # through JMP (A0) 0x94c2

# A-lines skip-68k mutes without 68k-loss. Do not mill skip of these.
NO_SKIP_68K_OPS = frozenset(
    {
        0xA97C,  # GetNewDialog (not GetCCursor)
        0xA97D,  # NewDialog (do not 10-byte GetNewDialog pop)
        0xAA1B,  # GetCCursor
        0xAA68,  # DialogDispatch
        0xA873,  # SetPort
        0xA983,  # DisposeDialog
        0xA8D9,  # CloseRgn
        0xA06E,  # OpenResFile (KEEP 0x16fc2/0x173f0)
        0xA9C9,  # SysError (KEEP 0x16db8 look-again)
        0xA9A0,  # GetResource
        0xA88F,  # InitCursor (KEEP 0x151d8)
        0xA991,  # ModalDialog (KEEP 6613 dialog loop)
        0xA01F,  # GetEOF
        0xA023,  # GetFPos
        0xA044,  # SetFPos
        0xA002,  # Read
        0xA450,  # Read
    }
)

# KEEP $a190 repeating table — data, not code.
A190_DATA_LO = 0x16DE8
A190_DATA_HI = 0x16E20

# KEEP look-again (undo skip later; do not remill skip-68k): OpenResFile/GetResource/InitCursor/GetFPos.
LOOK_AGAIN_SKIP_68K = frozenset(
    {0x16FC2, 0x173F0, 0x16DB8, 0x151D8, 0x50D28, 0x50D38, 0x8670}
)


def mill_stamp_68k(off: int) -> str:
    return "G3-MILL-68K-0x%x" % int(off)


def mill_68k_loops_ok() -> bool:
    return os.environ.get("G3_68K_MILL_LOOPS", "").strip().lower() in (
        "1",
        "true",
        "yes",
    )


def mill_68k_walk_ok() -> bool:
    """+2 walk after the map is empty. Default off; Grok Build / canned successor next."""
    v = os.environ.get("G3_68K_WALK", "0").strip().lower()
    return v in ("1", "true", "yes")


def skip_68k_loop_op(op: Optional[int]) -> bool:
    if mill_68k_loops_ok() or op is None:
        return False
    o = int(op) & 0xFFFF
    if o in LOOP_68K_OPS:
        return True
    if (o & 0xFF00) == 0x6000 and (o & 0xFF) == 0xFF:
        return True
    return False


def skip_68k_ui_op(op: Optional[int]) -> bool:
    """A-lines that skip-68k mutes WINDOW/FS. Do not mill skip."""
    if op is None:
        return False
    return (int(op) & 0xFFFF) in NO_SKIP_68K_OPS


RE_HANG_OFF_CMP = re.compile(r"if \(hang_off == 0x[0-9a-fA-F]+u\)")
RE_HB_PC = re.compile(r"heartbeat pc=([0-9a-fA-F]+)")


def mill_kinds(live: str) -> List[str]:
    return list(KINDS.get(live) or [])


def tested_keys(tested: Optional[List[str]] = None, live: str = "") -> List[str]:
    out: List[str] = []
    for t in tested or []:
        if t is None:
            continue
        s = str(t)
        if ":" in s:
            out.append(s)
        elif live:
            out.append("%s:skip-pair" % live)
        else:
            out.append("%s:skip-pair" % s)
    return out


def next_kind(live: str, tested: Optional[List[str]] = None) -> Optional[str]:
    keys = set(tested_keys(tested, live))
    for k in mill_kinds(live):
        if "%s:%s" % (live, k) not in keys:
            return k
    return None


def hang_rom_off(pc: Optional[int]) -> Optional[int]:
    if pc is None:
        return None
    pc = int(pc)
    if pc >= 0x50000000:
        return pc - 0x50000000
    return pc


def keep_is_68k(pc: Optional[int]) -> bool:
    return hang_rom_off(pc) == OFF_68K


def infer_saw_68k(mill: Optional[Dict[str, Any]] = None) -> bool:
    mill = mill or {}
    if mill.get("saw_68k") or keep_is_68k(mill.get("keep_pc")):
        return True
    for a in mill.get("attempts") or []:
        if a.get("result") != "KEEP":
            continue
        n = a.get("n")
        if not n:
            continue
        p = _log_for_n_simple(int(n))
        if p is None:
            continue
        tail = read_log_tail(p, 12000)
        if "pc=50366084" in tail:
            return True
    return False


def _log_for_n_simple(n: int) -> Optional[Path]:
    return resolve_log(Path("/tmp/ss-g3-mill-%d.log" % n))


def hang_off_millable(hang_off: Optional[int]) -> bool:
    if hang_off is None:
        return False
    off = int(hang_off)
    if off < 0x326000 or off >= 0x327000:
        return False
    if off in HARD_SKIP_OFFS:
        return False
    return True


def skip_hang_key(hang_off: int) -> str:
    return "leftover:skip-hang:%08x" % int(hang_off)


def leftover_68k_pending(
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
    saw_68k: bool = False,
    mill: Optional[Dict[str, Any]] = None,
) -> bool:
    if saw_68k:
        if next_skip_68k_off(mill, tested, reverted) is not None:
            return True
        keys = set(tested_keys(tested, "leftover"))
        rev = set(reverted or [])
        for k in (
            "stay-code66",
            "launch-upgrader",
            "splash-510",
            "splash-510-even",
            "pict-1000",
            "pef-upgrader",
            "pef-enter",
            "pef-imports",
            "pef-sysenv",
            "pef-vol",
            "pef-dce",
            "pef-wait",
            "pef-te",
            "pef-terec",
            "pef-skipte",
            "pef-skipdi",
            "pef-idx",
            "pef-gnd",
            "pef-gndid",
            "pef-d519",
            "pef-modal",
            "pef-no519",
            "pef-show",
            "pef-forcesplash",
            "pef-skipwait",
            "pef-callsplash",
            "pef-skipalert",
            "pef-nimp",
            "pef-jumpsplash",
            "pef-plantsplash",
            "pef-forceblit",
            "pef-blitoff",
            "pef-callgnd",
            "pef-skipae",
            "cfm-aa5a",
            "trap-68k",
            "reenter-68k",
            "fixmul-a868",
            "disposeptr-a01f",
            "initfonts-a8fe",
            "getresource-a9a0",
            "getnewdialog-dlog",
            "code66-syserr99",
            "code66-resume",
            "code66-allow-9440",
        ):
            key = "leftover:%s" % k
            if key not in keys and key not in rev:
                return True
        return False
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    for k in KIND_68K:
        if k in (
            "skip-68k",
            "cfm-aa5a",
            "trap-68k",
            "reenter-68k",
            "stay-code66",
            "launch-upgrader",
            "splash-510",
            "splash-510-even",
            "pict-1000",
            "pef-upgrader",
            "pef-enter",
            "pef-imports",
            "pef-sysenv",
            "pef-vol",
            "pef-dce",
            "pef-wait",
            "pef-te",
            "pef-terec",
            "pef-skipte",
            "pef-skipdi",
            "pef-idx",
            "pef-gnd",
            "pef-gndid",
            "pef-d519",
            "pef-modal",
            "pef-no519",
            "pef-show",
            "pef-forcesplash",
            "pef-skipwait",
            "pef-callsplash",
            "pef-skipalert",
            "pef-nimp",
            "pef-jumpsplash",
            "pef-plantsplash",
            "pef-forceblit",
            "pef-blitoff",
            "pef-callgnd",
            "pef-skipae",
            "getresource-a9a0",
            "getnewdialog-dlog",
            "code66-syserr99",
            "code66-resume",
            "code66-allow-9440",
        ):
            continue
        key = "leftover:%s" % k
        if key not in keys and key not in rev:
            return True
    return False


def next_skip_hang_off(
    hang_off: Optional[int],
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
) -> Optional[int]:
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])

    def walk(start: Optional[int]) -> Optional[int]:
        if start is None:
            return None
        off = int(start)
        if off < 0x326000 or off >= 0x327000:
            return None
        while off < 0x327000:
            if hang_off_millable(off):
                key = skip_hang_key(off)
                if key not in keys and key not in rev:
                    return off
            off += 4
        return None

    cand = walk(hang_off)
    if cand is not None:
        return cand
    if hang_off is not None and 0x326000 <= int(hang_off) < 0x327000:
        return walk(0x326000)
    return None


def force_skip_hang_off(
    hang_off: Optional[int] = None,
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
    current_off: Optional[int] = None,
) -> int:
    """Always a 50326 skip mill. Never None. Only G3 or Ctrl-C stops work."""
    off = next_skip_hang_off(hang_off, tested, reverted)
    if off is not None:
        return off
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    cur = int(current_off) if current_off is not None else None

    def first(skip_tested: bool) -> Optional[int]:
        o = 0x326000
        while o < 0x327000:
            if hang_off_millable(o):
                key = skip_hang_key(o)
                if key in rev:
                    o += 4
                    continue
                if cur is not None and o == cur:
                    o += 4
                    continue
                if skip_tested and key in keys:
                    o += 4
                    continue
                return o
            o += 4
        return None

    off = first(skip_tested=True)
    if off is not None:
        return off
    off = first(skip_tested=False)
    if off is not None:
        return off
    return 0x326510


def skip_68k_key(off: int) -> str:
    return "leftover:skip-68k:%08x" % int(off)


def skip_68k_millable(off: Optional[int]) -> bool:
    if off is None:
        return False
    o = int(off)
    if o == OFF_68K or o in HARD_SKIP_OFFS:
        return False
    # spin-26e88 REVERT. Do not remill skip-68k 0x26e88.
    if o == 0x26E88:
        return False
    if 0x326000 <= o < 0x327000:
        return False
    if UI_SKIP_68K_LO <= o < UI_SKIP_68K_HI:
        return False
    if CODE66_HELPER_LO <= o < CODE66_HELPER_HI:
        return False
    if A190_DATA_LO <= o < A190_DATA_HI:
        return False
    if o in LOOK_AGAIN_SKIP_68K:
        return False
    if 0x350000 <= o < 0x400000:
        return False
    if o < 0x1000 or o >= 0x400000:
        return False
    return True


def skip_68k_blocked(off: int, op: Optional[int] = None) -> bool:
    """True = do not mill skip-68k this ROM off (HARD/UI/FS/loop/data/NW annotations)."""
    if not skip_68k_millable(off):
        return True
    if skip_68k_loop_op(op) or skip_68k_ui_op(op):
        return True
    try:
        from mill_annotations import active

        ann = active()
        if ann and ann.blocks_skip_68k(off):
            return True
    except ImportError:
        pass
    return False


def _r24_to_off(r24: int) -> int:
    r24 = int(r24)
    return r24 - 0x50000000 if r24 >= 0x50000000 else r24


def _68k_pairs_from_log(path: Optional[Path]) -> List[Tuple[int, Optional[int]]]:
    """(rom_off, op or None) from map lines then spin lines."""
    text = read_log(path)
    if not text:
        return []
    pairs: List[Tuple[int, Optional[int]]] = []
    for m in RE_MAP_R24.finditer(text):
        pairs.append((_r24_to_off(int(m.group(1), 16)), int(m.group(2), 16)))
    for m in RE_SPIN_R24.finditer(text):
        op = int(m.group(2), 16) if m.group(2) else None
        pairs.append((_r24_to_off(int(m.group(1), 16)), op))
    return pairs


def _68k_offs_from_log(path: Optional[Path]) -> List[int]:
    return [p[0] for p in _68k_pairs_from_log(path)]


def _68k_trap_offs_from_log(path: Optional[Path]) -> List[int]:
    text = read_log(path)
    if not text:
        return []
    out: List[int] = []
    seen = set()
    for m in RE_TRAP_PC.finditer(text):
        if m.group(1) in NO_SKIP_68K_TRAP_NAMES:
            continue
        o = _r24_to_off(int(m.group(2), 16))
        if o % 2:
            o += 1
        if o in seen:
            continue
        seen.add(o)
        out.append(o)
    return out


def _68k_trap_offs(mill: Optional[Dict[str, Any]] = None) -> List[int]:
    mill = mill or {}
    offs: List[int] = []
    seen = set()
    paths: List[Optional[Path]] = []
    keep = mill.get("keep_log")
    if keep:
        paths.append(Path(str(keep)))
    if not mill.get("map_keep_log_only"):
        for n in KEEP_68K_LOG_NS:
            paths.append(_log_for_n_simple(n))
    for p in paths:
        for o in _68k_trap_offs_from_log(p):
            if o in seen:
                continue
            seen.add(o)
            offs.append(o)
    return offs


def _68k_map_pairs(mill: Optional[Dict[str, Any]] = None) -> List[Tuple[int, Optional[int]]]:
    mill = mill or {}
    pairs: List[Tuple[int, Optional[int]]] = []
    keep = mill.get("keep_log")
    if keep:
        pairs.extend(_68k_pairs_from_log(Path(str(keep))))
    if mill.get("map_keep_log_only"):
        return pairs
    for n in KEEP_68K_LOG_NS:
        pairs.extend(_68k_pairs_from_log(_log_for_n_simple(n)))
    return pairs


def next_skip_68k_off(
    mill: Optional[Dict[str, Any]] = None,
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
) -> Optional[int]:
    mill = mill or {}
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    if "leftover:spin-26e88" in rev:
        keys.add(skip_68k_key(0x26E88))
        rev.add(skip_68k_key(0x26E88))

    def ok(o: int, op: Optional[int] = None) -> bool:
        if skip_68k_blocked(o, op):
            return False
        key = skip_68k_key(o)
        return key not in keys and key not in rev

    try:
        from mill_annotations import active

        ann = active()
        if ann:
            for o in ann.skip_candidate_offs():
                if ok(o):
                    return o
    except ImportError:
        pass

    try:
        from mill_histogram import active as active_histogram

        hist = active_histogram()
        if hist:
            for o in hist.ranked_offs():
                if ok(o):
                    return o
    except ImportError:
        pass

    seen = set()
    for o in _68k_trap_offs(mill):
        if o in seen:
            continue
        seen.add(o)
        if ok(o):
            return o
    for raw, op in _68k_map_pairs(mill):
        o = int(raw)
        if o % 2:
            o += 1
        if o in seen:
            continue
        seen.add(o)
        if ok(o, op):
            return o
    if not mill_68k_walk_ok():
        return None
    start = 0x26E88
    mill35 = _68k_offs_from_log(_log_for_n_simple(35))
    if mill35:
        start = mill35[-1]
    start = int(start)
    if start % 2:
        start += 1
    o = start
    for _ in range(0x20000):
        if o not in seen and ok(o):
            return o
        o += 2
        if o >= 0x400000:
            o = 0x1000
    return None


def force_skip_68k_off(
    mill: Optional[Dict[str, Any]] = None,
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
) -> Optional[int]:
    """Histogram skip-68k only unless G3_68K_WALK=1. Never invent a +2 off by default."""
    off = next_skip_68k_off(mill, tested, reverted)
    if off is not None:
        return off
    if not mill_68k_walk_ok():
        return None
    o = 0x26E8A
    for _ in range(0x20000):
        if skip_68k_millable(o) and skip_68k_key(o) not in set(reverted or []):
            if o != 0x26E88:
                return o
        o += 2
        if o >= 0x400000:
            o = 0x1000
    return 0x26E90


def leftover_map_remaining(
    mill: Optional[Dict[str, Any]] = None,
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
    limit: int = 20,
) -> Tuple[List[int], Dict[str, int], int]:
    """Untested map/spin ROM offs from KEEP logs (not the whole ROM).

    Hang-cap logs at most MAP_LOG_CAP unique map lines; the in-guest
    bitmap still marks every even ROM halfword. skip-68k mills this
    leftover list, not a 4096 cap and not +2 of the image.
    Returns (next offs up to limit, prefix counts, remaining n).
    """
    mill = mill or {}
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    seen = set()
    remain: List[int] = []
    prefixes: Dict[str, int] = {}
    for raw, op in _68k_map_pairs(mill):
        o = int(raw)
        if o % 2:
            o += 1
        if o in seen:
            continue
        seen.add(o)
        if skip_68k_blocked(o, op):
            continue
        key = skip_68k_key(o)
        if key in keys or key in rev:
            continue
        hx = "%x" % o
        pref = hx[:3] if len(hx) >= 3 else hx
        prefixes[pref] = int(prefixes.get(pref) or 0) + 1
        remain.append(o)
    return remain[: max(0, int(limit))], prefixes, len(remain)


def last_millable_hang_off(
    log_path: Optional[str],
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
) -> Optional[int]:
    if not log_path:
        return None
    text = read_log(log_path)
    if not text:
        return None
    last = None
    for line in text.splitlines():
        m = RE_HB_PC.search(line)
        if not m:
            continue
        off = hang_rom_off(int(m.group(1), 16))
        if hang_off_millable(off):
            last = off
    if last is None:
        return None
    return next_skip_hang_off(last, tested, reverted)


def next_leftover(
    tested: Optional[List[str]] = None,
    reverted: Optional[List[str]] = None,
    hang_off: Optional[int] = None,
    saw_68k: bool = False,
    mill: Optional[Dict[str, Any]] = None,
) -> Optional[str]:
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    if saw_68k:
        key = "leftover:stay-code66"
        if key not in keys and key not in rev:
            return "stay-code66"
        key = "leftover:launch-upgrader"
        if key not in keys and key not in rev:
            return "launch-upgrader"
        key = "leftover:splash-510"
        if key not in keys and key not in rev:
            return "splash-510"
        key = "leftover:splash-510-even"
        if key not in keys and key not in rev:
            return "splash-510-even"
        key = "leftover:pict-1000"
        if key not in keys and key not in rev:
            return "pict-1000"
        key = "leftover:pef-upgrader"
        if key not in keys and key not in rev:
            return "pef-upgrader"
        key = "leftover:pef-enter"
        if key not in keys and key not in rev:
            return "pef-enter"
        key = "leftover:pef-imports"
        if key not in keys and key not in rev:
            return "pef-imports"
        key = "leftover:pef-sysenv"
        if key not in keys and key not in rev:
            return "pef-sysenv"
        key = "leftover:pef-vol"
        if key not in keys and key not in rev:
            return "pef-vol"
        key = "leftover:pef-dce"
        if key not in keys and key not in rev:
            return "pef-dce"
        key = "leftover:pef-wait"
        if key not in keys and key not in rev:
            return "pef-wait"
        key = "leftover:pef-te"
        if key not in keys and key not in rev:
            return "pef-te"
        key = "leftover:pef-terec"
        if key not in keys and key not in rev:
            return "pef-terec"
        key = "leftover:pef-skipte"
        if key not in keys and key not in rev:
            return "pef-skipte"
        key = "leftover:pef-skipdi"
        if key not in keys and key not in rev:
            return "pef-skipdi"
        key = "leftover:pef-idx"
        if key not in keys and key not in rev:
            return "pef-idx"
        key = "leftover:pef-gnd"
        if key not in keys and key not in rev:
            return "pef-gnd"
        key = "leftover:pef-gndid"
        if key not in keys and key not in rev:
            return "pef-gndid"
        key = "leftover:pef-d519"
        if key not in keys and key not in rev:
            return "pef-d519"
        key = "leftover:pef-modal"
        if key not in keys and key not in rev:
            return "pef-modal"
        key = "leftover:pef-no519"
        if key not in keys and key not in rev:
            return "pef-no519"
        key = "leftover:pef-show"
        if key not in keys and key not in rev:
            return "pef-show"
        key = "leftover:pef-forcesplash"
        if key not in keys and key not in rev:
            return "pef-forcesplash"
        key = "leftover:pef-skipwait"
        if key not in keys and key not in rev:
            return "pef-skipwait"
        key = "leftover:pef-callsplash"
        if key not in keys and key not in rev:
            return "pef-callsplash"
        key = "leftover:pef-skipalert"
        if key not in keys and key not in rev:
            return "pef-skipalert"
        key = "leftover:pef-nimp"
        if key not in keys and key not in rev:
            return "pef-nimp"
        key = "leftover:pef-jumpsplash"
        if key not in keys and key not in rev:
            return "pef-jumpsplash"
        key = "leftover:pef-plantsplash"
        if key not in keys and key not in rev:
            return "pef-plantsplash"
        key = "leftover:pef-forceblit"
        if key not in keys and key not in rev:
            return "pef-forceblit"
        key = "leftover:pef-blitoff"
        if key not in keys and key not in rev:
            return "pef-blitoff"
        key = "leftover:pef-callgnd"
        if key not in keys and key not in rev:
            return "pef-callgnd"
        key = "leftover:pef-skipae"
        if key not in keys and key not in rev:
            return "pef-skipae"
    for k in LEFTOVER:
        if k == "skip-hang":
            if saw_68k:
                continue
            if next_skip_hang_off(hang_off, tested, reverted) is None:
                continue
            return k
        if k == "skip-68k":
            if not saw_68k:
                continue
            if mill is not None and next_skip_68k_off(mill, tested, reverted) is None:
                continue
            return k
        if k in (
            "stay-code66",
            "launch-upgrader",
            "splash-510",
            "splash-510-even",
            "pict-1000",
            "pef-upgrader",
            "pef-enter",
            "pef-imports",
            "pef-sysenv",
            "pef-vol",
            "pef-dce",
            "pef-wait",
            "pef-te",
            "pef-terec",
            "pef-skipte",
            "pef-skipdi",
            "pef-idx",
            "pef-gnd",
            "pef-gndid",
            "pef-d519",
            "pef-modal",
            "pef-no519",
            "pef-show",
            "pef-forcesplash",
            "pef-skipwait",
            "pef-callsplash",
            "pef-skipalert",
            "pef-nimp",
            "pef-jumpsplash",
            "pef-plantsplash",
            "pef-forceblit",
            "pef-blitoff",
            "pef-callgnd",
            "pef-skipae",
            "cfm-aa5a",
            "trap-68k",
            "reenter-68k",
            "fixmul-a868",
            "disposeptr-a01f",
            "initfonts-a8fe",
            "getresource-a9a0",
            "getnewdialog-dlog",
            "code66-syserr99",
            "code66-resume",
            "code66-allow-9440",
            "grok-escalate",
        ):
            key = "leftover:%s" % k
            if key in keys or key in rev:
                continue
            if not saw_68k:
                continue
            return k
        if k in ("slot-26e90", "skip-3265a4", "spin-26e88", "skip-326458"):
            key = "leftover:%s" % k
            if key in keys or key in rev:
                continue
            if saw_68k or hang_off == OFF_68K:
                return k
            continue
        if k in ("keep-68k", "read-noerr", "setfpos-noerr"):
            key = "leftover:%s" % k
            if key in keys or key in rev:
                continue
            if hang_off is None or hang_off_millable(hang_off):
                continue
            if hang_off != OFF_68K:
                continue
            return k
        key = "leftover:%s" % k
        if key in keys or key in rev:
            continue
        if k == "unstick-stw" and "false-stw-spr:skip-pair" not in (tested or []):
            continue
        return k
    return None


def mill_kind(live: str, tested: Optional[List[str]] = None) -> str:
    """Refuse-as-wait classes mill skip/execute, not wait-cmp. e298371e is not this mill."""
    if live == "wait-cmp-fwd-bc":
        return "wait-already"
    k = next_kind(live, tested)
    if k:
        return k
    kinds = mill_kinds(live)
    return kinds[0] if kinds else "skip-pair"


def is_applied(
    live: str,
    root: Optional[Path] = None,
    kind: Optional[str] = None,
    hang_off: Optional[int] = None,
) -> bool:
    p = cpu_path(root)
    if not p.is_file():
        return False
    text = p.read_text(errors="replace")
    k = kind or mill_kind(live)
    if live == "leftover" or k in LEFTOVER:
        if k == "poison-skip":
            return MARKER_POISON in text
        if k == "unstick-stw":
            return WAIT0_BODY in text and MARKER_STW not in text and MARKER_MFSR not in text
        if k == "skip-hang":
            if MARKER_HANG_SKIP not in text:
                return False
            if hang_off is None:
                return True
            return ("if (hang_off == 0x%xu)" % int(hang_off)) in text
        if k == "keep-68k":
            return MARKER_68K_KEEP in text
        if k == "read-noerr":
            return MARKER_READ_NOERR in text
        if k == "setfpos-noerr":
            return MARKER_SETFPOS_NOERR in text
        if k == "slot-26e90":
            return MARKER_SLOT_26E90 in text
        if k == "skip-3265a4":
            return ("if (hang_off == 0x%xu)" % OFF_3265A4) in text
        if k == "skip-326458":
            return ("if (hang_off == 0x%xu)" % OFF_326458) in text
        if k == "spin-26e88":
            return MARKER_SPIN_26E88 in text
        if k == "skip-68k":
            if MARKER_68K_R24 not in text and MARKER_SPIN_26E88 not in text:
                return False
            if hang_off is None:
                return True
            if mill_stamp_68k(int(hang_off)) in text:
                return True
            if ("skip68 = 0x%xu;" % int(hang_off)) in text:
                return True
            return ("r24 - 2u == ROMBase + 0x%xu" % int(hang_off)) in text
        if k == "cfm-aa5a":
            return MARKER_CFM_AA5A in text
        if k == "trap-68k":
            return MARKER_TRAP_68K in text
        if k == "reenter-68k":
            return MARKER_REENTER_68K in text
        if k == "fixmul-a868":
            return MARKER_FIXMUL in text
        if k == "disposeptr-a01f":
            return MARKER_DISPOSEPTR in text
        if k == "initfonts-a8fe":
            return MARKER_INITFONTS in text
        if k == "stay-code66":
            return MARKER_STAY_CODE66 in text
        if k == "launch-upgrader":
            return MARKER_LAUNCH_UPGRADER in text
        if k == "splash-510":
            return MARKER_SPLASH_510 in text
        if k == "splash-510-even":
            return MARKER_SPLASH_510_EVEN in text
        if k == "pict-1000":
            return MARKER_PICT_1000 in text
        if k == "pef-upgrader":
            return MARKER_PEF_UPGRADER in text
        if k == "pef-enter":
            return MARKER_PEF_ENTER in text
        if k == "pef-imports":
            return MARKER_PEF_IMPORTS in text
        if k == "pef-sysenv":
            return MARKER_PEF_SYSENV in text
        if k == "pef-vol":
            return MARKER_PEF_VOL in text
        if k == "pef-dce":
            return MARKER_PEF_DCE in text
        if k == "pef-wait":
            return MARKER_PEF_WAIT in text
        if k == "pef-te":
            return MARKER_PEF_TE in text
        if k == "pef-terec":
            return MARKER_PEF_TEREC in text
        if k == "pef-skipte":
            return MARKER_PEF_SKIPTE in text
        if k == "pef-skipdi":
            return MARKER_PEF_SKIPDI in text
        if k == "pef-idx":
            return MARKER_PEF_IDX in text
        if k == "pef-gnd":
            return MARKER_PEF_GND in text
        if k == "pef-gndid":
            return MARKER_PEF_GNDID in text
        if k == "pef-d519":
            return MARKER_PEF_D519 in text
        if k == "pef-modal":
            return MARKER_PEF_MODAL in text
        if k == "pef-no519":
            return MARKER_PEF_NO519 in text
        if k == "pef-show":
            return MARKER_PEF_SHOW in text
        if k == "pef-forcesplash":
            return MARKER_PEF_FORCESPLASH in text
        if k == "pef-skipwait":
            return MARKER_PEF_SKIPWAIT in text
        if k == "pef-callsplash":
            return MARKER_PEF_CALLSPLASH in text
        if k == "pef-skipalert":
            return MARKER_PEF_SKIPALERT in text
        if k == "pef-nimp":
            return MARKER_PEF_NIMP in text
        if k == "pef-jumpsplash":
            return MARKER_PEF_JUMPSPLASH in text
        if k == "pef-plantsplash":
            return MARKER_PEF_PLANTSPLASH in text
        if k == "pef-forceblit":
            return MARKER_PEF_FORCEBLIT in text
        if k == "pef-blitoff":
            return MARKER_PEF_BLITOFF in text
        if k == "pef-callgnd":
            return MARKER_PEF_CALLGND in text
        if k == "pef-skipae":
            return MARKER_PEF_SKIPAE in text
        if k == "getresource-a9a0":
            return MARKER_GETRESOURCE_A9A0 in text
        if k == "getnewdialog-dlog":
            return MARKER_GETNEWDIALOG_DLOG in text
        if k == "code66-syserr99":
            return MARKER_CODE66_SYSERR99 in text
        if k == "code66-resume":
            return MARKER_CODE66_RESUME in text
        if k == "code66-allow-9440":
            return MARKER_CODE66_ALLOW_9440 in text
        if k == "grok-escalate":
            return True
        return False
    if live != "false-stw-spr":
        return False
    if k == "skip-pair":
        return MARKER_STW in text
    if k == "execute-pair":
        return MARKER_STW not in text and MARKER_MFSR not in text and WAIT0_BODY in text
    if k == "skip-mfsr":
        return MARKER_MFSR in text and MARKER_STW not in text
    return False


def millable(live: str) -> bool:
    return live in ("false-stw-spr", "false-cmp-li", "false-back-bc", "NEW")


def _stash_dir() -> Path:
    return Path("/tmp/g3-mill-stash")


def stash_files(root: Optional[Path] = None) -> Path:
    r = Path(root) if root else repo_root()
    dest = _stash_dir()
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for rel in MILL_FILES:
        src = r / rel
        if not src.is_file():
            continue
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)
    (dest / "ok").write_text("1\n")
    return dest


def revert(root: Optional[Path] = None) -> bool:
    r = Path(root) if root else repo_root()
    dest = _stash_dir()
    if (dest / "ok").is_file():
        for rel in MILL_FILES:
            src = dest / rel
            if src.is_file():
                shutil.copy2(src, r / rel)
        return True
    rels = [rel for rel in MILL_FILES if (r / rel).is_file()]
    if not rels:
        return False
    rc = subprocess.call(["git", "checkout", "HEAD", "--"] + rels, cwd=str(r))
    return rc == 0


def mill_worse(
    before: Dict[str, Any],
    after: Dict[str, Any],
    keep_pc: Optional[int] = None,
    saw_68k: bool = False,
    ss_alive_sec: Optional[float] = None,
    window: str = "unknown",
) -> bool:
    parsed = after.get("parsed") or after
    prev = before.get("parsed") or before
    if after.get("hang_04cecd36") or parsed.get("hang_04cecd36"):
        return True
    if after.get("empty_300") or parsed.get("empty_300"):
        return True
    mill_max = parsed.get("mill_max") or 0
    if mill_max:
        return True
    if parsed.get("ee_forced"):
        return True
    if prev.get("g2_live") and not after.get("g2_live"):
        return True
    if parsed.get("msr_collapse"):
        return True
    bhb = before.get("last_hb") or {}
    ahb = after.get("last_hb") or {}
    if keep_is_68k(bhb.get("pc")) and not keep_is_68k(ahb.get("pc")):
        aoff = hang_rom_off(ahb.get("pc"))
        if aoff is not None and 0x325000 <= aoff < 0x327000:
            return True
    reached = bool(after.get("reached_68k") or parsed.get("reached_68k"))
    if keep_is_68k(keep_pc) or saw_68k:
        aoff = hang_rom_off(ahb.get("pc"))
        in_50326 = aoff is not None and 0x325000 <= aoff < 0x327000
        if not reached:
            if ahb.get("pc") is None or in_50326:
                return True
        elif not keep_is_68k(ahb.get("pc")) and in_50326:
            return True
    win = (window or "unknown").lower()
    if (
        ss_alive_sec is not None
        and float(ss_alive_sec) < 8.0
        and win != "yes"
    ):
        # KEEP-stable can stop in <8s at 50366084. That is not
        # hang 04cecd36. Short-run worse only if 68k hang lost.
        if not (
            reached
            and keep_is_68k(ahb.get("pc"))
            and after.get("g2_live")
            and not (after.get("hang_04cecd36") or parsed.get("hang_04cecd36"))
        ):
            return True
    return False


def mill_moved(before: Dict[str, Any], after: Dict[str, Any]) -> bool:
    if (after.get("LIVE_CLASS") or "") != (before.get("LIVE_CLASS") or ""):
        return True
    bhb = (before.get("last_hb") or {}) or {}
    ahb = (after.get("last_hb") or {}) or {}
    if bhb.get("pc") != ahb.get("pc"):
        return True
    return False


def _replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n == 0:
        raise ValueError("mill patch missing: %s" % label)
    if n > 1:
        raise ValueError("mill patch not unique: %s" % label)
    return text.replace(old, new, 1)


def patch_header_text(text: str) -> str:
    if "nw_dec_leave_50326564_stw_mfsr" in text:
        return text
    old = (
        "/* Live 042a7f54 hang pc=50326564 after 50326484 bne +12. */\n"
        "int nw_dec_leave_50326564_off(uint32_t off);\n"
        "/* Heartbeat wait sites after 50326678. Not a skip-list. */\n"
        "int nw_dec_leave_hb_wait_off(uint32_t off);"
    )
    new = (
        "/* Live 042a7f54 hang pc=50326564 after 50326484 bne +12. */\n"
        "int nw_dec_leave_50326564_off(uint32_t off);\n"
        "/*\n"
        " * Live e25a61f1: 50326564 900107d4/7c0604a6 is stw+mfsr, not\n"
        " * cmp+forward-bc. Do not mill it as a wait (e298371e).\n"
        " */\n"
        "int nw_ppc_is_stw(uint32_t op);\n"
        "int nw_ppc_is_mfsr(uint32_t op);\n"
        "int nw_dec_leave_50326564_stw_mfsr(uint32_t off, uint32_t op, uint32_t nxt);\n"
        "/* Heartbeat wait sites after 50326678. Not a skip-list. */\n"
        "int nw_dec_leave_hb_wait_off(uint32_t off);"
    )
    return _replace_once(text, old, new, "header-stw-mfsr")


def patch_contract_text(text: str) -> str:
    if "int nw_dec_leave_50326564_stw_mfsr(" in text:
        return text
    old = (
        "int nw_dec_leave_50326564_off(uint32_t off)\n"
        "{\n"
        "	if (off == 0x326564u)\n"
        "		return 1;\n"
        "	/* Around 50326564: ±4 insns. Not a skip-list. */\n"
        "	if ((off & 3u) == 0 && off >= 0x32655cu && off <= 0x326574u)\n"
        "		return 1;\n"
        "	return 0;\n"
        "}"
    )
    new = (
        "int nw_dec_leave_50326564_off(uint32_t off)\n"
        "{\n"
        "	if (off == 0x326564u)\n"
        "		return 1;\n"
        "	/* Around 50326564: ±4 insns. Not a skip-list. */\n"
        "	if ((off & 3u) == 0 && off >= 0x32655cu && off <= 0x326574u)\n"
        "		return 1;\n"
        "	return 0;\n"
        "}\n"
        "\n"
        "int nw_ppc_is_stw(uint32_t op)\n"
        "{\n"
        "	return (op >> 26) == 36u;\n"
        "}\n"
        "\n"
        "int nw_ppc_is_mfsr(uint32_t op)\n"
        "{\n"
        "	return ((op >> 26) == 31u) && (((op >> 1) & 0x3ffu) == 595u);\n"
        "}\n"
        "\n"
        "int nw_dec_leave_50326564_stw_mfsr(uint32_t off, uint32_t op, uint32_t nxt)\n"
        "{\n"
        "	if (off != 0x326564u && off != 0x326568u)\n"
        "		return 0;\n"
        "	if (op == 0x900107d4u && nxt == 0x7c0604a6u)\n"
        "		return 1;\n"
        "	if (nw_ppc_is_stw(op) && nw_ppc_is_mfsr(nxt))\n"
        "		return 1;\n"
        "	if (off == 0x326568u && nw_ppc_is_mfsr(op))\n"
        "		return 1;\n"
        "	return 0;\n"
        "}"
    )
    return _replace_once(text, old, new, "contract-stw-mfsr")


def patch_cpu_text(text: str) -> str:
    out = text
    wait_old = (
        "static int nw_dec_leave_50326564_wait(uint32 rom_off)\n"
        "{\n"
        "	if (!nw_dec_did_leave || !nw_dec_took_900)\n"
        "		return 0;\n"
        "	return nw_dec_leave_50326564_off(rom_off);\n"
        "}"
    )
    wait_new = (
        "static int nw_dec_leave_50326564_wait(uint32 rom_off)\n"
        "{\n"
        "	(void)rom_off;\n"
        "	return 0;\n"
        "}"
    )
    if wait_old in out:
        out = _replace_once(out, wait_old, wait_new, "cpu-wait-return0")
    if MARKER_STW in out:
        return out
    return _replace_once(
        out, fetch_old_after_fetch(), fetch_new_skip_block(), "cpu-fetch-skip"
    )


def patch_cpu_remove_skip(text: str) -> str:
    """execute-pair: stw+mfsr run for real. Keep wait mill off. Do not restore e298371e."""
    if MARKER_STW not in text:
        return text
    return _replace_once(text, fetch_new_skip_block(), fetch_old_after_fetch(), "cpu-remove-skip")


def fetch_old_after_fetch() -> str:
    return (
        "		uint32 opcode;\n"
        "		if (!guest_fetch(&opcode)) {\n"
        "			if (!spcflags().empty() && !check_spcflags())\n"
        "				goto return_site;\n"
        "			continue;\n"
        "		}\n"
        "#ifdef SHEEPSHAVER\n"
        "		/* Live 6b413a91: after 50326674/678, complete the\n"
        "		 * next 50325/50326 cmp+bc the same way (match then\n"
        "		 * CR fallthrough at the armed pc). Do not smash r8.\n"
        "		 * Do not skip-list. Do not or-in EE. */\n"
    )


def fetch_new_skip_block() -> str:
    return (
        "		uint32 opcode;\n"
        "		if (!guest_fetch(&opcode)) {\n"
        "			if (!spcflags().empty() && !check_spcflags())\n"
        "				goto return_site;\n"
        "			continue;\n"
        "		}\n"
        "#ifdef SHEEPSHAVER\n"
        "		/* Live e25a61f1: 50326564 stw+mfsr hang. Skip the\n"
        "		 * pair. Do not treat 900107d4 as cmp. Do not smash\n"
        "		 * r0/r8. Do not arm 503264fc. */\n"
        "		if (nw_dec_did_leave) {\n"
        "			const uint32 stw_off =\n"
        "				(pc() >= ROMBase &&\n"
        "				 pc() < ROMBase + 0x500000u)\n"
        "					? pc() - ROMBase\n"
        "					: 0xffffffffu;\n"
        "			const uint32 stw_nxt =\n"
        "				vm_read_memory_4(pc() + 4);\n"
        "			if (nw_dec_leave_50326564_stw_mfsr(stw_off, opcode,\n"
        "							   stw_nxt)) {\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static int nstw;\n"
        "					if (!nstw) {\n"
        "						nstw = 1;\n"
        "						char buf[144];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: DEC leave 50326 stw+mfsr skip pc=%08x op=%08x nxt=%08x\",\n"
        "							 (unsigned)pc(),\n"
        "							 (unsigned)opcode,\n"
        "							 (unsigned)stw_nxt);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				if (nw_ppc_is_stw(opcode) &&\n"
        "				    nw_ppc_is_mfsr(stw_nxt))\n"
        "					pc() += 8u;\n"
        "				else\n"
        "					pc() += 4u;\n"
        "				continue;\n"
        "			}\n"
        "		}\n"
        "		/* Live 6b413a91: after 50326674/678, complete the\n"
        "		 * next 50325/50326 cmp+bc the same way (match then\n"
        "		 * CR fallthrough at the armed pc). Do not smash r8.\n"
        "		 * Do not skip-list. Do not or-in EE. */\n"
    )


def patch_harness_text(text: str) -> str:
    if "nw_dec_leave_50326564_stw_mfsr(0x326564u" in text:
        return text
    old = (
        "		CHECK(!nw_nk_picspin_skip_after_g2(0x326564u, 0x4082000cu));\n"
        "		CHECK(!nw_nk_picspin_mill_off(0x326564u));\n"
        "		CHECK(!nw_dec_leave_cmp_wait(0x4082fff0u));\n"
    )
    new = (
        "		CHECK(!nw_nk_picspin_skip_after_g2(0x326564u, 0x4082000cu));\n"
        "		CHECK(!nw_nk_picspin_mill_off(0x326564u));\n"
        "		CHECK(!nw_dec_leave_cmp_wait(0x4082fff0u));\n"
        "		/* Live e25a61f1: 900107d4/7c0604a6 is stw+mfsr, not a wait. */\n"
        "		CHECK(nw_ppc_is_stw(0x900107d4u));\n"
        "		CHECK(nw_ppc_is_mfsr(0x7c0604a6u));\n"
        "		CHECK(!nw_ppc_is_cmp(0x900107d4u));\n"
        "		CHECK(nw_dec_leave_50326564_stw_mfsr(0x326564u, 0x900107d4u,\n"
        "						     0x7c0604a6u));\n"
        "		CHECK(!nw_dec_leave_50326564_stw_mfsr(0x326480u, 0x900107d4u,\n"
        "						      0x7c0604a6u));\n"
        "		CHECK(!nw_dec_leave_cmp_wait(0x7c0604a6u));\n"
    )
    return _replace_once(text, old, new, "harness-stw-mfsr")


def apply_false_stw_spr(root: Optional[Path] = None) -> None:
    r = Path(root) if root else repo_root()
    h = r / "SheepShaver" / "src" / "include" / "nw_boot_contract.h"
    c = r / "SheepShaver" / "src" / "nw_boot_contract.cpp"
    cpu = cpu_path(r)
    harness = r / "SheepShaver" / "src" / "kpx_cpu" / "tests" / "mmu_harness.cpp"
    h.write_text(patch_header_text(h.read_text()))
    c.write_text(patch_contract_text(c.read_text()))
    cpu.write_text(patch_cpu_text(cpu.read_text()))
    if harness.is_file():
        harness.write_text(patch_harness_text(harness.read_text()))


def apply_execute_pair(root: Optional[Path] = None) -> None:
    """Run stw+mfsr. Wait mill stays off. Skip mill comes out."""
    r = Path(root) if root else repo_root()
    cpu = cpu_path(r)
    text = cpu.read_text()
    text = patch_cpu_text(text)
    text = patch_cpu_remove_skip(text)
    cpu.write_text(text)
    h = r / "SheepShaver" / "src" / "include" / "nw_boot_contract.h"
    c = r / "SheepShaver" / "src" / "nw_boot_contract.cpp"
    if "nw_dec_leave_50326564_stw_mfsr" not in h.read_text():
        h.write_text(patch_header_text(h.read_text()))
        c.write_text(patch_contract_text(c.read_text()))


def fetch_mfsr_skip_block() -> str:
    return (
        "		uint32 opcode;\n"
        "		if (!guest_fetch(&opcode)) {\n"
        "			if (!spcflags().empty() && !check_spcflags())\n"
        "				goto return_site;\n"
        "			continue;\n"
        "		}\n"
        "#ifdef SHEEPSHAVER\n"
        "		/* Live mill-2: execute stw 900107d4; skip only the\n"
        "		 * following mfsr. Do not skip the store (skip-pair\n"
        "		 * planted 68fff0dc). Do not treat as wait-cmp. */\n"
        "		if (nw_dec_did_leave) {\n"
        "			const uint32 mfsr_off =\n"
        "				(pc() >= ROMBase &&\n"
        "				 pc() < ROMBase + 0x500000u)\n"
        "					? pc() - ROMBase\n"
        "					: 0xffffffffu;\n"
        "			if (mfsr_off == 0x326568u &&\n"
        "			    nw_ppc_is_mfsr(opcode)) {\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static int nmfsr;\n"
        "					if (!nmfsr) {\n"
        "						nmfsr = 1;\n"
        "						char buf[144];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: DEC leave 50326 mfsr skip pc=%08x op=%08x\",\n"
        "							 (unsigned)pc(),\n"
        "							 (unsigned)opcode);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() += 4u;\n"
        "				continue;\n"
        "			}\n"
        "		}\n"
        "		/* Live 6b413a91: after 50326674/678, complete the\n"
        "		 * next 50325/50326 cmp+bc the same way (match then\n"
        "		 * CR fallthrough at the armed pc). Do not smash r8.\n"
        "		 * Do not skip-list. Do not or-in EE. */\n"
    )


def patch_cpu_skip_mfsr(text: str) -> str:
    out = patch_cpu_remove_skip(text)
    if MARKER_MFSR in out:
        return out
    return _replace_once(
        out, fetch_old_after_fetch(), fetch_mfsr_skip_block(), "cpu-skip-mfsr"
    )


def apply_skip_mfsr(root: Optional[Path] = None) -> None:
    r = Path(root) if root else repo_root()
    cpu = cpu_path(r)
    text = cpu.read_text()
    text = patch_cpu_text(text)
    text = patch_cpu_skip_mfsr(text)
    cpu.write_text(text)
    h = r / "SheepShaver" / "src" / "include" / "nw_boot_contract.h"
    c = r / "SheepShaver" / "src" / "nw_boot_contract.cpp"
    if "nw_ppc_is_mfsr" not in h.read_text():
        h.write_text(patch_header_text(h.read_text()))
        c.write_text(patch_contract_text(c.read_text()))


def hang_skip_if(hang_off: int) -> str:
    return "if (hang_off == 0x%xu)" % int(hang_off)


def fetch_hang_skip_block(hang_off: int) -> str:
    return (
        "#ifdef SHEEPSHAVER\n"
        "		/* Live mill-4 KEEP hang. Skip that insn.\n"
        "		 * Do not skip 0x3264fc. Do not skip 0x326564.\n"
        "		 * Do not skip-list 50325. */\n"
        "		if (nw_dec_did_leave) {\n"
        "			const uint32 hang_off =\n"
        "				(pc() >= ROMBase &&\n"
        "				 pc() < ROMBase + 0x500000u)\n"
        "					? pc() - ROMBase\n"
        "					: 0xffffffffu;\n"
        "			%s {\n"
        % hang_skip_if(hang_off)
        +
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static int nhang;\n"
        "					if (!nhang) {\n"
        "						nhang = 1;\n"
        "						char buf[96];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: KEEP hang skip pc=%08x off=%08x\",\n"
        "							 (unsigned)pc(),\n"
        "							 (unsigned)hang_off);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() += 4u;\n"
        "				continue;\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        + fetch_old_after_fetch()
    )


def patch_cpu_skip_hang(text: str, hang_off: int) -> str:
    if MARKER_HANG_SKIP in text:
        if hang_skip_if(hang_off) in text:
            return text
        n = len(RE_HANG_OFF_CMP.findall(text))
        if n != 1:
            raise ValueError("mill patch not unique: hang-skip-off")
        return RE_HANG_OFF_CMP.sub(hang_skip_if(hang_off), text, count=1)
    return _replace_once(
        text,
        fetch_old_after_fetch(),
        fetch_hang_skip_block(hang_off),
        "cpu-skip-hang",
    )


def apply_skip_hang(root: Optional[Path], hang_off: int) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_skip_hang(cpu.read_text(), hang_off))


def fetch_keep_68k_block() -> str:
    return (
        "#ifdef SHEEPSHAVER\n"
        "		/* mill-22 KEEP: 68k emulator pc=50366084.\n"
        "		 * Do not skip-hang 50326 from this log.\n"
        "		 * Do not skip 0x366084 (68k interp). */\n"
        "		if (pc() == ROMBase + 0x366084u) {\n"
        "#if NW_BOOT_LOG\n"
        "			static int n68kkeep;\n"
        "			if (!n68kkeep) {\n"
        "				n68kkeep = 1;\n"
        "				nw_boot_log(\"G3: KEEP 68k hang pc=50366084\");\n"
        "			}\n"
        "#endif\n"
        "		}\n"
        "#endif\n"
        + fetch_old_after_fetch()
    )


def patch_cpu_keep_68k(text: str) -> str:
    if MARKER_68K_KEEP in text:
        return text
    return _replace_once(
        text, fetch_old_after_fetch(), fetch_keep_68k_block(), "cpu-keep-68k"
    )


def patch_cpu_read_noerr(text: str) -> str:
    if MARKER_READ_NOERR in text:
        return text
    old = (
        "						 * eofErr so the loop can exit.\n"
        "						 * Keep Write. */\n"
        "						uint32 pb = g3_rom0(gpr(16));\n"
        "						if (g3_ea_data(pb + 16u))\n"
        "							vm_write_memory_2(pb + 16u,\n"
        "									  0xffd9u);\n"
        "						if (g3_ea_data(pb + 40u))\n"
        "							vm_write_memory_4(pb + 40u, 0);\n"
        "						gpr(8) = 0xffffffd9u;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrd;\n"
        "							if (nrd < 8) {\n"
        "								nrd++;\n"
        "								nw_boot_log(\"G3: 68k Read A002/A450 eofErr\");\n"
    )
    new = (
        "						 * mill-22 KEEP 68k: eofErr still\n"
        "						 * looped HGetFileInfo. noErr + 0\n"
        "						 * actCount. Keep Write. */\n"
        "						uint32 pb = g3_rom0(gpr(16));\n"
        "						if (g3_ea_data(pb + 16u))\n"
        "							vm_write_memory_2(pb + 16u, 0);\n"
        "						if (g3_ea_data(pb + 40u))\n"
        "							vm_write_memory_4(pb + 40u, 0);\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nrd;\n"
        "							if (nrd < 8) {\n"
        "								nrd++;\n"
        "								nw_boot_log(\"G3: 68k Read A002/A450 noErr\");\n"
    )
    return _replace_once(text, old, new, "cpu-read-noerr")


def patch_cpu_setfpos_noerr(text: str) -> str:
    if MARKER_SETFPOS_NOERR in text:
        return text
    old = (
        "						/* SetFPos(PB). noErr retried at\n"
        "						 * 0xa8248. eofErr (-39) so the\n"
        "						 * read loop can exit. */\n"
        "						uint32 pb = g3_rom0(gpr(16));\n"
        "						if (g3_ea_data(pb + 16u))\n"
        "							vm_write_memory_2(pb + 16u,\n"
        "									  0xffd9u);\n"
        "						gpr(8) = 0xffffffd9u;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsfp;\n"
        "							if (nsfp < 8) {\n"
        "								nsfp++;\n"
        "								nw_boot_log(\"G3: 68k SetFPos A044 eofErr\");\n"
    )
    new = (
        "						/* mill-22 KEEP 68k: eofErr still\n"
        "						 * looped. noErr ioResult. */\n"
        "						uint32 pb = g3_rom0(gpr(16));\n"
        "						if (g3_ea_data(pb + 16u))\n"
        "							vm_write_memory_2(pb + 16u, 0);\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nsfp;\n"
        "							if (nsfp < 8) {\n"
        "								nsfp++;\n"
        "								nw_boot_log(\"G3: 68k SetFPos A044 noErr\");\n"
    )
    return _replace_once(text, old, new, "cpu-setfpos-noerr")


def apply_keep_68k(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_keep_68k(cpu.read_text()))


def apply_read_noerr(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_read_noerr(cpu.read_text()))


def apply_setfpos_noerr(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_setfpos_noerr(cpu.read_text()))


def patch_cpu_slot_26e90(text: str) -> str:
    if MARKER_SLOT_26E90 in text:
        return text
    n = text.count("0x26de0u")
    if n < 8:
        raise ValueError("mill patch missing: slot-26de0 count=%s" % n)
    out = text.replace("0x26de0u", "0x26e90u")
    out = out.replace(
        "G3: 68k skip slot helper 0x26de0",
        MARKER_SLOT_26E90,
    )
    if MARKER_SLOT_26E90 not in out:
        raise ValueError("mill patch missing: slot-26e90-log")
    return out


def apply_slot_26e90(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_slot_26e90(cpu.read_text()))


def patch_cpu_spin_26e88(text: str) -> str:
    if MARKER_SPIN_26E88 in text:
        return text
    old = (
        "							nw_boot_log(\"G3: 68k skip slot helper 0x26e90\");\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				if (r24 - 2u >= ROMBase + 0x2bdf0u &&\n"
    )
    new = (
        "							nw_boot_log(\"G3: 68k skip slot helper 0x26e90\");\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				/* mill-35 KEEP 68k spin r24=50026e88\n"
        "				 * op=1adc after dest 0x26de0 MOVEA. */\n"
        "				if (r24 - 2u == ROMBase + 0x26e88u) {\n"
        "					gpr(8) = 0;\n"
        "					g3_ccr = 4;\n"
        "					gpr(24) = ROMBase + 0x26e90u;\n"
        "					gpr(27) = 0xffffffffu;\n"
        "					gpr(29) = ROMBase + 0x380000u;\n"
        "					pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned n1adc;\n"
        "						if (n1adc < 8) {\n"
        "							n1adc++;\n"
        "							nw_boot_log(\"G3: 68k skip 1adc mill 0x26e88\");\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				if (r24 - 2u >= ROMBase + 0x2bdf0u &&\n"
    )
    return _replace_once(text, old, new, "cpu-spin-26e88")


def apply_spin_26e88(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_spin_26e88(cpu.read_text()))


RE_SKIP_68K_MILL = re.compile(
    r"if \(r24 - 2u == ROMBase \+ 0x([0-9a-fA-F]+)u\) \{\n"
    r"\t+gpr\(8\) = 0;\n"
    r"\t+g3_ccr = 4;\n"
    r"\t+gpr\(24\) = ROMBase \+ 0x([0-9a-fA-F]+)u;"
)


def _skip_68k_if(off: int, dest: int) -> str:
    return (
        "if (r24 - 2u == ROMBase + 0x%xu) {\n"
        "					gpr(8) = 0;\n"
        "					g3_ccr = 4;\n"
        "					gpr(24) = ROMBase + 0x%xu;"
        % (int(off), int(dest))
    )


def patch_cpu_skip_68k(text: str, hang_off: int) -> str:
    off = int(hang_off)
    dest = off + 8
    dm = RE_SKIP68_DEFAULT.search(text)
    if dm:
        old_off = int(dm.group(1), 16)
        if old_off == off and mill_stamp_68k(off) in text:
            return text
        text = text.replace(
            "skip68 = 0x%xu;" % old_off,
            "skip68 = 0x%xu;" % off,
            1,
        )
        if mill_stamp_68k(old_off) in text:
            text = text.replace(mill_stamp_68k(old_off), mill_stamp_68k(off), 1)
        elif mill_stamp_68k(off) not in text:
            text = text.replace(
                'nw_boot_log("G3: 68k r24 skip");',
                'nw_boot_log("G3: 68k r24 skip %s");' % mill_stamp_68k(off),
                1,
            )
        return text
    if MARKER_68K_R24 in text or MARKER_SPIN_26E88 in text:
        mm = RE_SKIP_68K_MILL.search(text)
        if mm:
            old_off = int(mm.group(1), 16)
            old_dest = int(mm.group(2), 16)
            if (
                old_off == off
                and old_dest == dest
                and MARKER_68K_R24 in text
                and mill_stamp_68k(off) in text
            ):
                return text
            text = _replace_once(
                text,
                _skip_68k_if(old_off, old_dest),
                _skip_68k_if(off, dest),
                "cpu-skip-68k-off",
            )
            if MARKER_68K_R24 not in text:
                text = text.replace(MARKER_SPIN_26E88, MARKER_68K_R24, 1)
            if mill_stamp_68k(old_off) in text:
                text = text.replace(mill_stamp_68k(old_off), mill_stamp_68k(off), 1)
            elif mill_stamp_68k(off) not in text:
                text = text.replace(
                    'nw_boot_log("G3: 68k r24 skip");',
                    'nw_boot_log("G3: 68k r24 skip %s");'
                    % mill_stamp_68k(off),
                    1,
                )
            return text
    old = (
        "							nw_boot_log(\"G3: 68k skip slot helper 0x26e90\");\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				if (r24 - 2u >= ROMBase + 0x2bdf0u &&\n"
    )
    new = (
        "							nw_boot_log(\"G3: 68k skip slot helper 0x26e90\");\n"
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				if (r24 - 2u == ROMBase + 0x%xu) {\n"
        % off
        +
        "					gpr(8) = 0;\n"
        "					g3_ccr = 4;\n"
        "					gpr(24) = ROMBase + 0x%xu;\n"
        % dest
        +
        "					gpr(27) = 0xffffffffu;\n"
        "					gpr(29) = ROMBase + 0x380000u;\n"
        "					pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						static unsigned nr24s;\n"
        "						if (nr24s < 8) {\n"
        "							nr24s++;\n"
        "							nw_boot_log(\"G3: 68k r24 skip %s\");\n"
        % mill_stamp_68k(off)
        +
        "						}\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "				if (r24 - 2u >= ROMBase + 0x2bdf0u &&\n"
    )
    return _replace_once(text, old, new, "cpu-skip-68k")


def apply_skip_68k(root: Optional[Path], hang_off: int) -> None:
    cpu = cpu_path(root)
    text = patch_cpu_skip_hang(cpu.read_text(), OFF_3265A4)
    cpu.write_text(patch_cpu_skip_68k(text, hang_off))


def patch_cpu_cfm_aa5a(text: str) -> str:
    if MARKER_CFM_AA5A in text:
        return text
    old = (
        "					} else if (op68 == 0xaa5au) {\n"
        "						uint16 sel = 0;\n"
        "						uint32 sp = gpr(1);\n"
        "						if (g3_ea_data(sp))\n"
        "							sel = vm_read_memory_2(sp);\n"
        "						/* Pascal: result word under args+selector.\n"
    )
    new = (
        "					} else if (op68 == 0xaa5au) {\n"
        "						uint16 sel = 0;\n"
        "						uint32 sp = gpr(1);\n"
        "						if (g3_ea_data(sp))\n"
        "							sel = vm_read_memory_2(sp);\n"
        "						if (sel == 0xfffcu) {\n"
        "#if NW_BOOT_LOG\n"
        "							{\n"
        "								static unsigned ncfmnat;\n"
        "								if (ncfmnat < 8) {\n"
        "									ncfmnat++;\n"
        "									nw_boot_log(\"G3: 68k CFM AA5A sel=65532 native\");\n"
        "								}\n"
        "							}\n"
        "#endif\n"
        "						} else {\n"
        "						/* Pascal: result word under args+selector.\n"
    )
    out = _replace_once(text, old, new, "cpu-cfm-aa5a-open")
    old2 = "					} else if (op68 == 0xa96fu) {\n"
    new2 = "						}\n					} else if (op68 == 0xa96fu) {\n"
    return _replace_once(out, old2, new2, "cpu-cfm-aa5a-close")


def apply_cfm_aa5a(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_cfm_aa5a(cpu.read_text()))


def patch_cpu_launch_upgrader(text: str) -> str:
    if MARKER_LAUNCH_UPGRADER in text:
        return text
    out = text.replace(
        "static uint32 g3_rf_off = 112182784u;\n"
        "static uint32 g3_rf_map = 187261u;\n"
        "static unsigned g3_rf_mapn = 2870u;",
        "static uint32 g3_rf_off = 74305024u;\n"
        "static uint32 g3_rf_map = 55022u;\n"
        "static unsigned g3_rf_mapn = 1882u;",
        1,
    )
    old = (
        "								snprintf(buf, sizeof(buf),\n"
        "									 \"G3: 68k Launch A9F2 enter CODE0 r24=%08x\",\n"
        "									 (unsigned)jt);\n"
        "								nw_boot_log(buf);\n"
    )
    new = (
        "								if (jt) {\n"
        "									snprintf(buf, sizeof(buf),\n"
        "										 \"G3: 68k Launch A9F2 enter CODE0 r24=%08x\",\n"
        "										 (unsigned)jt);\n"
        "									nw_boot_log(buf);\n"
        "								} else\n"
        "									nw_boot_log(\n"
        "										\"G3: 68k Launch A9F2 CFM Upgrader\");\n"
    )
    if old in out:
        out = _replace_once(out, old, new, "cpu-launch-upgrader-log")
    if MARKER_LAUNCH_UPGRADER not in out:
        raise RuntimeError("cpu-launch-upgrader-stamp")
    return out


def apply_launch_upgrader(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_launch_upgrader(cpu.read_text()))


def patch_cpu_splash_510(text: str) -> str:
    if MARKER_SPLASH_510 in text:
        return text
    raise RuntimeError("cpu-splash-510-stamp")


def apply_splash_510(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_splash_510(cpu.read_text()))


def patch_cpu_splash_510_even(text: str) -> str:
    if MARKER_SPLASH_510_EVEN in text:
        return text
    raise RuntimeError("cpu-splash-510-even-stamp")


def apply_splash_510_even(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_splash_510_even(cpu.read_text()))


def patch_cpu_pict_1000(text: str) -> str:
    if MARKER_PICT_1000 in text:
        return text
    raise RuntimeError("cpu-pict-1000-stamp")


def apply_pict_1000(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pict_1000(cpu.read_text()))


def patch_cpu_pef_upgrader(text: str) -> str:
    if MARKER_PEF_UPGRADER in text:
        return text
    raise RuntimeError("cpu-pef-upgrader-stamp")


def apply_pef_upgrader(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_upgrader(cpu.read_text()))


def patch_cpu_pef_enter(text: str) -> str:
    if MARKER_PEF_ENTER in text:
        return text
    raise RuntimeError("cpu-pef-enter-stamp")


def apply_pef_enter(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_enter(cpu.read_text()))


def patch_cpu_pef_imports(text: str) -> str:
    if MARKER_PEF_IMPORTS in text:
        return text
    raise RuntimeError("cpu-pef-imports-stamp")


def apply_pef_imports(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_imports(cpu.read_text()))


def patch_cpu_pef_sysenv(text: str) -> str:
    if MARKER_PEF_SYSENV in text:
        return text
    raise RuntimeError("cpu-pef-sysenv-stamp")


def apply_pef_sysenv(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_sysenv(cpu.read_text()))


def patch_cpu_pef_vol(text: str) -> str:
    if MARKER_PEF_VOL in text:
        return text
    raise RuntimeError("cpu-pef-vol-stamp")


def apply_pef_vol(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_vol(cpu.read_text()))


def patch_cpu_pef_dce(text: str) -> str:
    if MARKER_PEF_DCE in text:
        return text
    raise RuntimeError("cpu-pef-dce-stamp")


def apply_pef_dce(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_dce(cpu.read_text()))


def patch_cpu_pef_wait(text: str) -> str:
    if MARKER_PEF_WAIT in text:
        return text
    raise RuntimeError("cpu-pef-wait-stamp")


def apply_pef_wait(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_wait(cpu.read_text()))


def patch_cpu_pef_te(text: str) -> str:
    if MARKER_PEF_TE in text:
        return text
    raise RuntimeError("cpu-pef-te-stamp")


def apply_pef_te(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_te(cpu.read_text()))


def patch_cpu_pef_terec(text: str) -> str:
    if MARKER_PEF_TEREC in text:
        return text
    old = (
        "	} else if (idx == 223u) {\n"
        "		if (a4 && g3_ea_data(a4 + 3u)) {\n"
    )
    new = (
        "	} else if (idx == 263u) {\n"
        "		/* Dummy TERec Handle so 1010b428 lha lineHeight\n"
        "		 * does not DSI DAR=15018. Not leftover:pef-te. */\n"
        "		uint32 rec = g3_pef_newptr(256u);\n"
        "		uint32 h = g3_pef_newptr(8u);\n"
        "		if (rec && h && g3_ea_data(rec + 0x5fu) && g3_ea_data(h + 3u)) {\n"
        "			vm_write_memory_2(rec + 0x18u, 16);\n"
        "			vm_write_memory_2(rec + 0x5eu, 1);\n"
        "			vm_write_memory_2(rec + 0x3cu, 0);\n"
        "			vm_write_memory_4(h, rec);\n"
        "			r3 = h;\n"
        "		}\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nte;\n"
        "			if (nte < 8) {\n"
        "				char buf[96];\n"
        "				nte++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF TERec h=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 223u) {\n"
        "		if (a4 && g3_ea_data(a4 + 3u)) {\n"
    )
    return _replace_once(text, old, new, "cpu-pef-terec")


def apply_pef_terec(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_terec(cpu.read_text()))


def patch_cpu_pef_skipte(text: str) -> str:
    if MARKER_PEF_SKIPTE in text:
        return text
    old = (
        "	*ent_out = ent;\n"
        "	*toc_out = toc;\n"
        "	*sp_out = sp - 64u;\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "	*ent_out = ent;\n"
        "	*toc_out = toc;\n"
        "	*sp_out = sp - 64u;\n"
        "	/* Skip TENew height 1010b404..1010b534 (DSI DAR=15018).\n"
        "	 * Not leftover:pef-te / pef-terec. */\n"
        "	if (g3_ea_data(ent + 0xa037u))\n"
        "		vm_write_memory_4(ent + 0xa034u, 0x48000130u);\n"
        "	g3_did_pef_enter = 1;\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nskipte;\n"
        "		if (nskipte < 8) {\n"
        "			char buf[96];\n"
        "			nskipte++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF skipTE pc=%08x\",\n"
        "				 (unsigned)(ent + 0xa034u));\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
    )
    return _replace_once(text, old, new, "cpu-pef-skipte")


def apply_pef_skipte(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_skipte(cpu.read_text()))


def patch_cpu_pef_skipdi(text: str) -> str:
    if MARKER_PEF_SKIPDI in text:
        return text
    old = (
        "	if (g3_ea_data(ent + 0xa037u))\n"
        "		vm_write_memory_4(ent + 0xa034u, 0x48000130u);\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "	if (g3_ea_data(ent + 0xa037u))\n"
        "		vm_write_memory_4(ent + 0xa034u, 0x48000130u);\n"
        "	/* Skip GetDialogItem item 6 1010b3dc..1010b534 (DSI DAR=15000).\n"
        "	 * Not leftover:pef-skipte. */\n"
        "	if (g3_ea_data(ent + 0xa00fu))\n"
        "		vm_write_memory_4(ent + 0xa00cu, 0x48000158u);\n"
        "	g3_did_pef_enter = 1;\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nskipdi;\n"
        "		if (nskipdi < 8) {\n"
        "			char buf[96];\n"
        "			nskipdi++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF skipDI pc=%08x\",\n"
        "				 (unsigned)(ent + 0xa00cu));\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
    )
    return _replace_once(text, old, new, "cpu-pef-skipdi")


def apply_pef_skipdi(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_skipdi(cpu.read_text()))


def patch_cpu_pef_idx(text: str) -> str:
    if MARKER_PEF_IDX in text:
        return text
    old_tv = (
        "	for (i = 0; i < 295u; i++) {\n"
        "		uint32 t = stub + 4u + i * 8u;\n"
        "		vm_write_memory_4(t, stub);\n"
        "		vm_write_memory_4(t + 4u, 0);\n"
        "	}\n"
    )
    new_tv = (
        "	for (i = 0; i < 295u; i++) {\n"
        "		uint32 t = stub + 4u + i * 8u;\n"
        "		vm_write_memory_4(t, stub);\n"
        "		vm_write_memory_4(t + 4u, i);\n"
        "	}\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nidx;\n"
        "		if (!nidx) {\n"
        "			nidx = 1;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF idx n=295\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
    )
    text = _replace_once(text, old_tv, new_tv, "cpu-pef-idx-tv")
    old_ic = (
        "				uint32 idx = gpr(2);\n"
        "				uint32 r3;\n"
        "				if (idx > 294u && g3_ea_data(gpr(12) + 4u))\n"
        "					idx = vm_read_memory_4(gpr(12) + 4u);\n"
    )
    new_ic = (
        "				uint32 idx = 0;\n"
        "				uint32 r3;\n"
        "				if (g3_ea_data(gpr(12) + 4u))\n"
        "					idx = vm_read_memory_4(gpr(12) + 4u);\n"
        "				if (idx > 294u) {\n"
        "					const uint32 base = RAMBase + 0x116004u;\n"
        "					const uint32 tv = gpr(12);\n"
        "					if (tv >= base && ((tv - base) & 7u) == 0)\n"
        "						idx = (tv - base) / 8u;\n"
        "					if (idx > 294u)\n"
        "						idx = 0;\n"
        "				}\n"
    )
    return _replace_once(text, old_ic, new_ic, "cpu-pef-idx-ic")


def apply_pef_idx(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_idx(cpu.read_text()))


def patch_cpu_pef_gnd(text: str) -> str:
    if MARKER_PEF_GND in text:
        return text
    old_plant = (
        "static int g3_did_splash510;\n"
        "static uint32 g3_plant_splash510(void)\n"
        "{\n"
        "	uint32 doff = 0, ln = 0, h = 0, dlg = 0;\n"
        "	if (g3_did_splash510)\n"
        "		return 0;\n"
    )
    new_plant = (
        "static int g3_did_splash510;\n"
        "static uint32 g3_splash_dlg;\n"
        "static uint32 g3_plant_splash510(void)\n"
        "{\n"
        "	uint32 doff = 0, ln = 0, h = 0, dlg = 0;\n"
        "	if (g3_did_splash510)\n"
        "		return g3_splash_dlg;\n"
    )
    text = _replace_once(text, old_plant, new_plant, "cpu-pef-gnd-plant")
    old_set = (
        "	g3_did_splash510 = 1;\n"
        "	g3_pict1000_blit();\n"
        "	return dlg;\n"
    )
    new_set = (
        "	g3_did_splash510 = 1;\n"
        "	g3_splash_dlg = dlg;\n"
        "	g3_pict1000_blit();\n"
        "	return dlg;\n"
    )
    text = _replace_once(text, old_set, new_set, "cpu-pef-gnd-save")
    old_idx = (
        "	} else if (idx == 134u) {\n"
        "		if ((a3 & 0xffffu) == 510u)\n"
        "			r3 = g3_plant_splash510();\n"
        "	} else if (idx == 155u || idx == 170u) {\n"
    )
    new_idx = (
        "	} else if (idx == 134u) {\n"
        "		if ((a3 & 0xffffu) == 510u)\n"
        "			r3 = g3_plant_splash510();\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned ngnd;\n"
        "			if (ngnd < 8) {\n"
        "				char buf[96];\n"
        "				ngnd++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog 510 dlg=%08x\",\n"
        "					 (unsigned)r3);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 155u || idx == 170u) {\n"
    )
    return _replace_once(text, old_idx, new_idx, "cpu-pef-gnd-idx")


def apply_pef_gnd(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_gnd(cpu.read_text()))


def patch_cpu_pef_gndid(text: str) -> str:
    if MARKER_PEF_GNDID in text:
        return text
    old = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog 510 dlg=%08x\",\n"
        "					 (unsigned)r3);\n"
    )
    new = (
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id=%u dlg=%08x\",\n"
        "					 (unsigned)(a3 & 0xffffu), (unsigned)r3);\n"
    )
    return _replace_once(text, old, new, "cpu-pef-gndid")


def apply_pef_gndid(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_gndid(cpu.read_text()))


def patch_cpu_pef_d519(text: str) -> str:
    if MARKER_PEF_D519 in text:
        return text
    old_fn = (
        "static uint32 g3_splash_dlg;\n"
        "static uint32 g3_plant_splash510(void)\n"
    )
    new_fn = (
        "static uint32 g3_splash_dlg;\n"
        "static uint32 g3_dlog519;\n"
        "static uint32 g3_plant_dlog519(void)\n"
        "{\n"
        "	uint32 doff = 0, ln = 0, h = 0, dlg = 0;\n"
        "	if (g3_dlog519)\n"
        "		return g3_dlog519;\n"
        "	g3_plant_inst_map();\n"
        "	if (!g3_res_lookup(0x444c4f47u, 519, &doff, &ln))\n"
        "		return 0;\n"
        "	h = g3_res_plant(doff, ln);\n"
        "	if (h && g3_ea_data(h))\n"
        "		dlg = vm_read_memory_4(h);\n"
        "	doff = 0;\n"
        "	ln = 0;\n"
        "	if (g3_res_lookup(0x4449544cu, 519, &doff, &ln) && ln)\n"
        "		(void)g3_res_plant(doff, ln);\n"
        "	if (dlg && (dlg & 1u) && g3_ea_data(dlg + 1u))\n"
        "		dlg &= ~1u;\n"
        "	if (dlg && g3_ea_data(dlg + 10u))\n"
        "		vm_write_memory_1(dlg + 10u, 1);\n"
        "	g3_dlog519 = dlg;\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nd519;\n"
        "		if (nd519 < 8) {\n"
        "			char buf[96];\n"
        "			nd519++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF DLOG 519 dlg=%08x\",\n"
        "				 (unsigned)dlg);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	return dlg;\n"
        "}\n"
        "static uint32 g3_plant_splash510(void)\n"
    )
    text = _replace_once(text, old_fn, new_fn, "cpu-pef-d519-fn")
    old_idx = (
        "		if ((a3 & 0xffffu) == 510u)\n"
        "			r3 = g3_plant_splash510();\n"
    )
    new_idx = (
        "		if ((a3 & 0xffffu) == 510u)\n"
        "			r3 = g3_plant_splash510();\n"
        "		else if ((a3 & 0xffffu) == 519u)\n"
        "			r3 = g3_plant_dlog519();\n"
    )
    return _replace_once(text, old_idx, new_idx, "cpu-pef-d519-idx")


def apply_pef_d519(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_d519(cpu.read_text()))


def patch_cpu_pef_modal(text: str) -> str:
    if MARKER_PEF_MODAL in text:
        return text
    old = (
        "	} else if (idx == 134u) {\n"
    )
    new = (
        "	} else if (idx == 53u) {\n"
        "		/* ModalDialog(filter, itemHit). DITL 519 #1 OK. */\n"
        "		if (a4 && g3_ea_data(a4 + 1u))\n"
        "			vm_write_memory_2(a4, 1);\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nmd;\n"
        "			if (nmd < 8) {\n"
        "				char buf[96];\n"
        "				nmd++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF ModalDialog item=%u\",\n"
        "					 1u);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 134u) {\n"
    )
    return _replace_once(text, old, new, "cpu-pef-modal")


def apply_pef_modal(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_modal(cpu.read_text()))


def patch_cpu_pef_no519(text: str) -> str:
    if MARKER_PEF_NO519 in text:
        return text
    old = (
        "		else if ((a3 & 0xffffu) == 519u)\n"
        "			r3 = g3_plant_dlog519();\n"
    )
    new = (
        "		else if ((a3 & 0xffffu) == 519u) {\n"
        "			/* 519 success skipped Splash 510. Fail it. */\n"
        "			r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "			{\n"
        "				static unsigned nno519;\n"
        "				if (nno519 < 8) {\n"
        "					nno519++;\n"
        "					nw_boot_log(\n"
        "						\"G3: 68k Launch A9F2 CFM Upgrader PEF no519\");\n"
        "				}\n"
        "			}\n"
        "#endif\n"
        "		}\n"
    )
    return _replace_once(text, old, new, "cpu-pef-no519")


def apply_pef_no519(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_no519(cpu.read_text()))


def patch_cpu_pef_show(text: str) -> str:
    if MARKER_PEF_SHOW in text:
        return text
    old = (
        "	} else if (idx == 53u) {\n"
    )
    new = (
        "	} else if (idx == 173u) {\n"
        "		/* ShowWindow. DLOG 510/519 ship invisible. */\n"
        "		uint32 w = a3;\n"
        "		if (!w)\n"
        "			w = g3_splash_dlg;\n"
        "		if (w && g3_ea_data(w + 10u))\n"
        "			vm_write_memory_1(w + 10u, 1);\n"
        "		g3_pict1000_blit();\n"
        "		r3 = 0;\n"
        "#if NW_BOOT_LOG\n"
        "		{\n"
        "			static unsigned nsh;\n"
        "			if (nsh < 8) {\n"
        "				char buf[96];\n"
        "				nsh++;\n"
        "				snprintf(buf, sizeof(buf),\n"
        "					 \"G3: 68k Launch A9F2 CFM Upgrader PEF ShowWindow w=%08x\",\n"
        "					 (unsigned)w);\n"
        "				nw_boot_log(buf);\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "	} else if (idx == 53u) {\n"
    )
    return _replace_once(text, old, new, "cpu-pef-show")


def apply_pef_show(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_show(cpu.read_text()))


def patch_cpu_pef_forcesplash(text: str) -> str:
    if MARKER_PEF_FORCESPLASH in text:
        return text
    old = (
        "	if (g3_ea_data(ent + 0xa00fu))\n"
        "		vm_write_memory_4(ent + 0xa00cu, 0x48000158u);\n"
    )
    new = (
        "	if (g3_ea_data(ent + 0xa00fu))\n"
        "		vm_write_memory_4(ent + 0xa00cu, 0x48000158u);\n"
        "	/* Always take splash 101014b0 (nop bf eq skip). */\n"
        "	if (g3_ea_data(ent + 0xdfu))\n"
        "		vm_write_memory_4(ent + 0xdcu, 0x60000000u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nfs;\n"
        "		if (nfs < 8) {\n"
        "			nfs++;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF forceSplash\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
    )
    return _replace_once(text, old, new, "cpu-pef-forcesplash")


def apply_pef_forcesplash(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_forcesplash(cpu.read_text()))


def patch_cpu_pef_skipwait(text: str) -> str:
    if MARKER_PEF_SKIPWAIT in text:
        return text
    old = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF forceSplash\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF forceSplash\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	/* Skip wait 1010144c so splash 101014b0 runs. */\n"
        "	if (g3_ea_data(ent + 0x7fu))\n"
        "		vm_write_memory_4(ent + 0x7cu, 0x48000064u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nsw;\n"
        "		if (nsw < 8) {\n"
        "			nsw++;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-skipwait")


def apply_pef_skipwait(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_skipwait(cpu.read_text()))


def patch_cpu_pef_callsplash(text: str) -> str:
    if MARKER_PEF_CALLSPLASH in text:
        return text
    old = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	/* Skip helpers 101013f4..wait; splash 101014b0. */\n"
        "	if (g3_ea_data(ent + 0x27u))\n"
        "		vm_write_memory_4(ent + 0x24u, 0x480000bcu);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned ncs;\n"
        "		if (ncs < 8) {\n"
        "			ncs++;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF callSplash\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-callsplash")


def apply_pef_callsplash(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_callsplash(cpu.read_text()))


def patch_cpu_pef_skipalert(text: str) -> str:
    if MARKER_PEF_SKIPALERT in text:
        return text
    old = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	/* ResizeAndDisplayAlert GetNewDialog -> r3=0. */\n"
        "	if (g3_ea_data(ent + 0x9e9fu))\n"
        "		vm_write_memory_4(ent + 0x9e9cu, 0x38600000u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nsa;\n"
        "		if (nsa < 8) {\n"
        "			nsa++;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-skipalert")


def apply_pef_skipalert(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_skipalert(cpu.read_text()))


def patch_cpu_pef_nimp(text: str) -> str:
    if MARKER_PEF_NIMP in text:
        return text
    old = (
        "		if (nimp < 24) {\n"
        "			char buf[96];\n"
        "			nimp++;\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF import idx=%u r3=%08x\",\n"
        "				 (unsigned)idx, (unsigned)a3);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
    )
    new = (
        "		if (nimp < 80) {\n"
        "			char buf[96];\n"
        "			nimp++;\n"
        "			if (nimp == 1)\n"
        "				nw_boot_log(\n"
        "					\"G3: 68k Launch A9F2 CFM Upgrader PEF nimp\");\n"
        "			snprintf(buf, sizeof(buf),\n"
        "				 \"G3: 68k Launch A9F2 CFM Upgrader PEF import idx=%u r3=%08x\",\n"
        "				 (unsigned)idx, (unsigned)a3);\n"
        "			nw_boot_log(buf);\n"
        "		}\n"
    )
    return _replace_once(text, old, new, "cpu-pef-nimp")


def apply_pef_nimp(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_nimp(cpu.read_text()))


def patch_cpu_pef_jumpsplash(text: str) -> str:
    if MARKER_PEF_JUMPSPLASH in text:
        return text
    old = (
        "				r3 = g3_pef_host(idx, gpr(3), gpr(4), gpr(5));\n"
        "				gpr(3) = r3;\n"
        "				pc() = lr();\n"
        "				continue;\n"
    )
    new = (
        "				r3 = g3_pef_host(idx, gpr(3), gpr(4), gpr(5));\n"
        "				gpr(3) = r3;\n"
        "				/* After UseResFile, jump to splash 101014b0. */\n"
        "				if (idx == 79u) {\n"
        "					static unsigned njs;\n"
        "					njs++;\n"
        "					if (njs >= 6u) {\n"
        "						pc() = 0x101014b0u;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned njp;\n"
        "							if (njp < 8) {\n"
        "								njp++;\n"
        "								nw_boot_log(\n"
        "									\"G3: 68k Launch A9F2 CFM Upgrader PEF jumpSplash\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "						continue;\n"
        "					}\n"
        "				}\n"
        "				pc() = lr();\n"
        "				continue;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-jumpsplash")


def apply_pef_jumpsplash(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_jumpsplash(cpu.read_text()))


def patch_cpu_pef_plantsplash(text: str) -> str:
    if MARKER_PEF_PLANTSPLASH in text:
        return text
    old = (
        "		} else if (idx == 121u)\n"
        "			r3 = 1;\n"
        "		else\n"
        "			r3 = 0;\n"
    )
    new = (
        "		} else if (idx == 121u)\n"
        "			r3 = 1;\n"
        "		else {\n"
        "			r3 = 0;\n"
        "			/* Plant Splash 510 while PEF idles on UseResFile. */\n"
        "			{\n"
        "				static unsigned nps;\n"
        "				nps++;\n"
        "				if (nps >= 3u) {\n"
        "					uint32 dlg = g3_plant_splash510();\n"
        "					if (dlg && g3_ea_data(dlg + 10u))\n"
        "						vm_write_memory_1(dlg + 10u, 1);\n"
        "					g3_pict1000_blit();\n"
        "#if NW_BOOT_LOG\n"
        "					if (nps < 11u)\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF plantSplash\");\n"
        "#endif\n"
        "				}\n"
        "			}\n"
        "		}\n"
    )
    return _replace_once(text, old, new, "cpu-pef-plantsplash")


def apply_pef_plantsplash(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_plantsplash(cpu.read_text()))


def patch_cpu_pef_forceblit(text: str) -> str:
    if MARKER_PEF_FORCEBLIT in text:
        return text
    old = (
        "				if (nps >= 3u) {\n"
        "					uint32 dlg = g3_plant_splash510();\n"
        "					if (dlg && g3_ea_data(dlg + 10u))\n"
        "						vm_write_memory_1(dlg + 10u, 1);\n"
        "					g3_pict1000_blit();\n"
        "#if NW_BOOT_LOG\n"
        "					if (nps < 11u)\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF plantSplash\");\n"
        "#endif\n"
        "				}\n"
    )
    new = (
        "				if (nps >= 3u) {\n"
        "					g3_did_splash510 = 0;\n"
        "					g3_did_pict1000 = 0;\n"
        "					uint32 dlg = g3_plant_splash510();\n"
        "					if (dlg && g3_ea_data(dlg + 10u))\n"
        "						vm_write_memory_1(dlg + 10u, 1);\n"
        "					g3_pict1000_blit();\n"
        "#if NW_BOOT_LOG\n"
        "					if (nps < 11u)\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF forceBlit\");\n"
        "#endif\n"
        "				}\n"
    )
    return _replace_once(text, old, new, "cpu-pef-forceblit")


def apply_pef_forceblit(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_forceblit(cpu.read_text()))


def patch_cpu_pef_blitoff(text: str) -> str:
    if MARKER_PEF_BLITOFF in text:
        return text
    old = (
        "			g3_fb_xrgb(fb, (int)x, (int)y,\n"
        "				   clut[idx][0], clut[idx][1], clut[idx][2]);\n"
    )
    new = (
        "			g3_fb_xrgb(fb, (int)x, (int)y + 80,\n"
        "				   clut[idx][0], clut[idx][1], clut[idx][2]);\n"
    )
    text = _replace_once(text, old, new, "cpu-pef-blitoff-y")
    old2 = (
        "			nw_boot_log(\"G3: 68k DrawPicture A8F6 PICT 1000\");\n"
    )
    new2 = (
        "			nw_boot_log(\"G3: 68k DrawPicture A8F6 PICT 1000\");\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF blitOff\");\n"
    )
    return _replace_once(text, old2, new2, "cpu-pef-blitoff")


def apply_pef_blitoff(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_blitoff(cpu.read_text()))


def patch_cpu_pef_callgnd(text: str) -> str:
    if MARKER_PEF_CALLGND in text:
        return text
    old = (
        "					g3_pict1000_blit();\n"
        "#if NW_BOOT_LOG\n"
        "					if (nps < 11u)\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF forceBlit\");\n"
        "#endif\n"
    )
    new = (
        "					g3_pict1000_blit();\n"
        "					(void)g3_pef_host(134u, 510u, 0, 0);\n"
        "#if NW_BOOT_LOG\n"
        "					if (nps < 11u)\n"
        "						nw_boot_log(\n"
        "							\"G3: 68k Launch A9F2 CFM Upgrader PEF callGnd\");\n"
        "#endif\n"
    )
    return _replace_once(text, old, new, "cpu-pef-callgnd")


def apply_pef_callgnd(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_callgnd(cpu.read_text()))


def patch_cpu_pef_skipae(text: str) -> str:
    if MARKER_PEF_SKIPAE in text:
        return text
    old = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    new = (
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	/* Skip AE oapp 10101444 so skipWait splash runs. */\n"
        "	if (g3_ea_data(ent + 0x77u))\n"
        "		vm_write_memory_4(ent + 0x74u, 0x60000000u);\n"
        "#if NW_BOOT_LOG\n"
        "	{\n"
        "		static unsigned nae;\n"
        "		if (nae < 8) {\n"
        "			nae++;\n"
        "			nw_boot_log(\n"
        "				\"G3: 68k Launch A9F2 CFM Upgrader PEF skipAE\");\n"
        "		}\n"
        "	}\n"
        "#endif\n"
        "	g3_did_pef_enter = 1;\n"
    )
    return _replace_once(text, old, new, "cpu-pef-skipae")


def apply_pef_skipae(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_pef_skipae(cpu.read_text()))


def patch_cpu_trap_68k(text: str) -> str:
    if MARKER_TRAP_68K in text:
        return text
    old = (
        "					} else\n"
        "						gpr(8) = 0;\n"
        "					if (op68 == 0xa746u) {\n"
    )
    new = (
        "					} else {\n"
        "						/* mill trap-68k: do not false-noErr unknown A-lines. */\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned naln;\n"
        "							if (naln < 8) {\n"
        "								naln++;\n"
        "								nw_boot_log(\"G3: 68k A-line default native\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					}\n"
        "					if (op68 == 0xa746u) {\n"
    )
    return _replace_once(text, old, new, "cpu-trap-68k")


def apply_trap_68k(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_trap_68k(cpu.read_text()))


def patch_cpu_reenter_68k(text: str) -> str:
    if MARKER_REENTER_68K in text:
        return text
    old = (
        "			if (hang_off == 0x3265a4u) {\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static int nhang;\n"
        "					if (!nhang) {\n"
        "						nhang = 1;\n"
        "						char buf[96];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: KEEP hang skip pc=%08x off=%08x\",\n"
        "							 (unsigned)pc(),\n"
        "							 (unsigned)hang_off);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() += 4u;\n"
        "				continue;\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		uint32 opcode;\n"
    )
    new = (
        "			if (hang_off == 0x3265a4u) {\n"
        "#if NW_BOOT_LOG\n"
        "				{\n"
        "					static int nhang;\n"
        "					if (!nhang) {\n"
        "						nhang = 1;\n"
        "						char buf[96];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: KEEP hang skip pc=%08x off=%08x\",\n"
        "							 (unsigned)pc(),\n"
        "							 (unsigned)hang_off);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "				}\n"
        "#endif\n"
        "				pc() += 4u;\n"
        "				continue;\n"
        "			}\n"
        "			/* mill reenter-68k: mill-3239 last_hb 50326554,\n"
        "			 * no 68k. One-shot from 50326 wait into 68k\n"
        "			 * interp. Do not skip HARD 0x3264fc/564/568. */\n"
        "			if (hang_off >= 0x326000u && hang_off < 0x327000u &&\n"
        "			    hang_off != 0x3264fcu &&\n"
        "			    hang_off != 0x326564u &&\n"
        "			    hang_off != 0x326568u) {\n"
        "				static int nre68;\n"
        "				if (!nre68) {\n"
        "					nre68 = 1;\n"
        "					uint32 r24 = g3_fix_r24(gpr(24));\n"
        "					if (!g3_r24_ok(r24))\n"
        "						r24 = ROMBase + 0x2au;\n"
        "					gpr(24) = r24;\n"
        "					gpr(27) = 0xffffffffu;\n"
        "					gpr(29) = ROMBase + 0x380000u;\n"
        "					pc() = ROMBase + 0x366084u;\n"
        "#if NW_BOOT_LOG\n"
        "					{\n"
        "						char buf[96];\n"
        "						snprintf(buf, sizeof(buf),\n"
        "							 \"G3: 68k reenter from hang off=%08x r24=%08x\",\n"
        "							 (unsigned)hang_off,\n"
        "							 (unsigned)r24);\n"
        "						nw_boot_log(buf);\n"
        "					}\n"
        "#endif\n"
        "					continue;\n"
        "				}\n"
        "			}\n"
        "		}\n"
        "#endif\n"
        "		uint32 opcode;\n"
    )
    return _replace_once(text, old, new, "cpu-reenter-68k")


def apply_reenter_68k(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_reenter_68k(cpu.read_text()))


def patch_cpu_fixmul(text: str) -> str:
    if MARKER_FIXMUL in text:
        return text
    old = (
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nblkm;\n"
        "							if (nblkm < 8) {\n"
        "								nblkm++;\n"
        "								nw_boot_log(\"G3: 68k BlockMove A22E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else {\n"
        "						/* mill trap-68k: do not false-noErr unknown A-lines. */\n"
    )
    new = (
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nblkm;\n"
        "							if (nblkm < 8) {\n"
        "								nblkm++;\n"
        "								nw_boot_log(\"G3: 68k BlockMove A22E\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa868u ||\n"
        "						   op68 == 0xa84du) {\n"
        "						/* KEEP 6613: FixMul/FixDiv at\n"
        "						 * 0x8888 hit default native.\n"
        "						 * Pascal pop 8; D0 = 16.16. */\n"
        "						uint32 sp = gpr(1);\n"
        "						uint32 b = 0, a = 0;\n"
        "						if (g3_ea_data(sp))\n"
        "							b = vm_read_memory_4(sp);\n"
        "						if (g3_ea_data(sp + 4u))\n"
        "							a = vm_read_memory_4(sp + 4u);\n"
        "						if (g3_ea_data(sp))\n"
        "							gpr(1) = sp + 8u;\n"
        "						{\n"
        "							int32 sa = (int32)a;\n"
        "							int32 sb = (int32)b;\n"
        "							uint32 r;\n"
        "							if (op68 == 0xa84du) {\n"
        "								if (sb == 0)\n"
        "									r = sa < 0\n"
        "										? 0x80000000u\n"
        "										: 0x7fffffffu;\n"
        "								else {\n"
        "									long long n =\n"
        "										((long long)sa) << 16;\n"
        "									r = (uint32)(n / sb);\n"
        "								}\n"
        "							} else {\n"
        "								long long p =\n"
        "									(long long)sa *\n"
        "									(long long)sb;\n"
        "								r = (uint32)(p >> 16);\n"
        "							}\n"
        "							gpr(8) = r;\n"
        "						}\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned nfixm;\n"
        "							if (nfixm < 8) {\n"
        "								nfixm++;\n"
        "								nw_boot_log(\n"
        "									op68 == 0xa84du\n"
        "									? \"G3: 68k FixDiv A84D\"\n"
        "									: \"G3: 68k FixMul A868\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else {\n"
        "						/* mill trap-68k: do not false-noErr unknown A-lines. */\n"
    )
    return _replace_once(text, old, new, "cpu-fixmul-a868")


def apply_fixmul_a868(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_fixmul(cpu.read_text()))


def patch_cpu_disposeptr(text: str) -> str:
    if MARKER_DISPOSEPTR in text:
        return text
    old = (
        "					} else if (op68 == 0xa122u ||\n"
        "						   op68 == 0xa322u ||\n"
        "						   op68 == 0xa522u ||\n"
        "						   op68 == 0xa722u) {\n"
        "						/* NewHandle / Clear / Sys / SysClear.\n"
        "						 * A0 = handle (ptr to master ptr). */\n"
    )
    new = (
        "					} else if (op68 == 0xa01fu) {\n"
        "						/* DisposePtr(A0). KEEP 6613 0x8670\n"
        "						 * default native. noErr. Do not\n"
        "						 * skip-68k look-again 0x8670. */\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ndptr;\n"
        "							if (ndptr < 8) {\n"
        "								ndptr++;\n"
        "								nw_boot_log(\"G3: 68k DisposePtr A01F\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa122u ||\n"
        "						   op68 == 0xa322u ||\n"
        "						   op68 == 0xa522u ||\n"
        "						   op68 == 0xa722u) {\n"
        "						/* NewHandle / Clear / Sys / SysClear.\n"
        "						 * A0 = handle (ptr to master ptr). */\n"
    )
    return _replace_once(text, old, new, "cpu-disposeptr-a01f")


def apply_disposeptr_a01f(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_disposeptr(cpu.read_text()))


def patch_cpu_initfonts(text: str) -> str:
    if MARKER_INITFONTS in text:
        return text
    old = (
        "					} else if (op68 == 0xa9e6u) {\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned niniw;\n"
        "							if (niniw < 8) {\n"
        "								niniw++;\n"
        "								nw_boot_log(\"G3: 68k InitWindows A9E6\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa97cu) {\n"
    )
    new = (
        "					} else if (op68 == 0xa9e6u) {\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned niniw;\n"
        "							if (niniw < 8) {\n"
        "								niniw++;\n"
        "								nw_boot_log(\"G3: 68k InitWindows A9E6\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa8feu ||\n"
        "						   op68 == 0xa930u ||\n"
        "						   op68 == 0xa9ccu ||\n"
        "						   op68 == 0xa850u) {\n"
        "						/* KEEP 14552: InitCPort then 68k\n"
        "						 * hang. Host InitFonts/InitMenus/\n"
        "						 * TEInit/InitCursor. No Pascal\n"
        "						 * args. Do not skip-68k. Do not\n"
        "						 * remill DisposePtr A01F. */\n"
        "						gpr(8) = 0;\n"
        "#if NW_BOOT_LOG\n"
        "						{\n"
        "							static unsigned ninfnt;\n"
        "							if (ninfnt < 8) {\n"
        "								ninfnt++;\n"
        "								nw_boot_log(\n"
        "									op68 == 0xa930u\n"
        "									? \"G3: 68k InitMenus A930\"\n"
        "									: op68 == 0xa9ccu\n"
        "									? \"G3: 68k TEInit A9CC\"\n"
        "									: op68 == 0xa850u\n"
        "									? \"G3: 68k InitCursor A850\"\n"
        "									: \"G3: 68k InitFonts A8FE\");\n"
        "							}\n"
        "						}\n"
        "#endif\n"
        "					} else if (op68 == 0xa97cu) {\n"
    )
    return _replace_once(text, old, new, "cpu-initfonts-a8fe")


def apply_initfonts_a8fe(root: Optional[Path] = None) -> None:
    cpu = cpu_path(root)
    cpu.write_text(patch_cpu_initfonts(cpu.read_text()))


def apply(
    live: str,
    root: Optional[Path] = None,
    kind: Optional[str] = None,
    hang_off: Optional[int] = None,
) -> Dict[str, Any]:
    """Apply canned mill for LIVE_CLASS+kind. Idempotent. Stash first."""
    k = kind or mill_kind(live)
    out: Dict[str, Any] = {
        "ok": False,
        "applied": False,
        "live": live,
        "kind": k,
        "reason": None,
        "hang_off": hang_off,
    }
    if k not in ("skip-hang", "skip-68k") and (
        live == "wait-cmp-fwd-bc" or k == "wait-already"
    ):
        out["reason"] = "wait-already"
        return out
    leftover = live == "leftover" or k in LEFTOVER
    if leftover:
        if is_applied("leftover", root, kind=k, hang_off=hang_off):
            out["ok"] = True
            out["reason"] = "already"
            return out
        if k == "skip-hang":
            if not hang_off_millable(hang_off):
                out["reason"] = "hang-off-hard"
                return out
        if k == "skip-68k":
            if hang_off is None or not skip_68k_millable(hang_off):
                out["reason"] = "hang-off-hard"
                return out
        if k == "grok-escalate":
            out["ok"] = True
            out["reason"] = "grok-build"
            return out
        stash_files(root)
        if k == "poison-skip":
            # C++ mill is in ppc-cpu.cpp (MARKER_POISON).
            if not is_applied("leftover", root, kind="poison-skip"):
                out["reason"] = "apply-failed"
                revert(root)
                return out
        elif k == "unstick-stw":
            apply_execute_pair(root)
        elif k == "skip-hang":
            apply_skip_hang(root, int(hang_off))
        elif k == "keep-68k":
            apply_keep_68k(root)
        elif k == "read-noerr":
            apply_read_noerr(root)
        elif k == "setfpos-noerr":
            apply_setfpos_noerr(root)
        elif k == "slot-26e90":
            apply_slot_26e90(root)
        elif k == "skip-3265a4":
            apply_skip_hang(root, OFF_3265A4)
        elif k == "spin-26e88":
            apply_spin_26e88(root)
        elif k == "skip-326458":
            apply_skip_hang(root, OFF_326458)
        elif k == "skip-68k":
            apply_skip_68k(root, int(hang_off))
        elif k == "cfm-aa5a":
            apply_cfm_aa5a(root)
        elif k == "trap-68k":
            apply_trap_68k(root)
        elif k == "reenter-68k":
            apply_reenter_68k(root)
        elif k == "fixmul-a868":
            apply_fixmul_a868(root)
        elif k == "disposeptr-a01f":
            apply_disposeptr_a01f(root)
        elif k == "initfonts-a8fe":
            apply_initfonts_a8fe(root)
        elif k == "stay-code66":
            pass
        elif k == "launch-upgrader":
            apply_launch_upgrader(root)
        elif k == "splash-510":
            apply_splash_510(root)
        elif k == "splash-510-even":
            apply_splash_510_even(root)
        elif k == "pict-1000":
            apply_pict_1000(root)
        elif k == "pef-upgrader":
            apply_pef_upgrader(root)
        elif k == "pef-enter":
            apply_pef_enter(root)
        elif k == "pef-imports":
            apply_pef_imports(root)
        elif k == "pef-sysenv":
            apply_pef_sysenv(root)
        elif k == "pef-vol":
            apply_pef_vol(root)
        elif k == "pef-dce":
            apply_pef_dce(root)
        elif k == "pef-wait":
            apply_pef_wait(root)
        elif k == "pef-te":
            apply_pef_te(root)
        elif k == "pef-terec":
            apply_pef_terec(root)
        elif k == "pef-skipte":
            apply_pef_skipte(root)
        elif k == "pef-skipdi":
            apply_pef_skipdi(root)
        elif k == "pef-idx":
            apply_pef_idx(root)
        elif k == "pef-gnd":
            apply_pef_gnd(root)
        elif k == "pef-gndid":
            apply_pef_gndid(root)
        elif k == "pef-d519":
            apply_pef_d519(root)
        elif k == "pef-modal":
            apply_pef_modal(root)
        elif k == "pef-no519":
            apply_pef_no519(root)
        elif k == "pef-show":
            apply_pef_show(root)
        elif k == "pef-forcesplash":
            apply_pef_forcesplash(root)
        elif k == "pef-skipwait":
            apply_pef_skipwait(root)
        elif k == "pef-callsplash":
            apply_pef_callsplash(root)
        elif k == "pef-skipalert":
            apply_pef_skipalert(root)
        elif k == "pef-nimp":
            apply_pef_nimp(root)
        elif k == "pef-jumpsplash":
            apply_pef_jumpsplash(root)
        elif k == "pef-plantsplash":
            apply_pef_plantsplash(root)
        elif k == "pef-forceblit":
            apply_pef_forceblit(root)
        elif k == "pef-blitoff":
            apply_pef_blitoff(root)
        elif k == "pef-callgnd":
            apply_pef_callgnd(root)
        elif k == "pef-skipae":
            apply_pef_skipae(root)
        elif k == "getresource-a9a0":
            pass
        elif k == "getnewdialog-dlog":
            pass
        elif k == "code66-syserr99":
            pass
        elif k == "code66-resume":
            pass
        elif k == "code66-allow-9440":
            pass
        else:
            out["reason"] = "no-canned-mill"
            return out
        if not is_applied("leftover", root, kind=k, hang_off=hang_off):
            out["reason"] = "apply-failed"
            revert(root)
            return out
        out["ok"] = True
        out["applied"] = True
        return out
    if live != "false-stw-spr" or k not in mill_kinds(live):
        out["reason"] = "no-canned-mill"
        return out
    if is_applied(live, root, kind=k):
        out["ok"] = True
        out["reason"] = "already"
        return out
    stash_files(root)
    if k == "skip-pair":
        apply_false_stw_spr(root)
    elif k == "execute-pair":
        apply_execute_pair(root)
    elif k == "skip-mfsr":
        apply_skip_mfsr(root)
    if not is_applied(live, root, kind=k):
        out["reason"] = "apply-failed"
        revert(root)
        return out
    out["ok"] = True
    out["applied"] = True
    return out


def files_for_class(live: str) -> List[str]:
    if live in ("empty-vector", "dsi-on-store"):
        return ["SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-mmu.cpp"]
    return [
        "SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp",
        "SheepShaver/src/nw_boot_contract.cpp",
        "SheepShaver/src/include/nw_boot_contract.h",
    ]
