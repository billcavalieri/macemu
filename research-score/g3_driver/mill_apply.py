#!/usr/bin/env python3
"""Canned class mills. Qwen does not pick file/PC/class. Hang-cap only after apply."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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
    "skip-68k",
    "cfm-aa5a",
    "trap-68k",
    "reenter-68k",
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
    "skip-68k",
    "cfm-aa5a",
    "trap-68k",
    "reenter-68k",
)
OFF_68K = 0x366084
MARKER_68K_R24 = "G3: 68k r24 skip"
RE_SPIN_R24 = re.compile(r"68k spin r24=([0-9a-fA-F]+)(?: op=([0-9a-fA-F]+))?")
RE_MAP_R24 = re.compile(r"68k map r24=([0-9a-fA-F]+) op=([0-9a-fA-F]+)")
RE_TRAP_PC = re.compile(
    r"68k (DialogDispatch|GetCCursor|DisposeDialog|GetNewDialog|"
    r"NewCWindow|NewWindow|SetPort|CloseRgn|InitCPort|GetResource|"
    r"OpenResFile|InitCursor|GetEOF|GetFPos)"
    r".*\bpc=([0-9a-fA-F]+)"
)
NO_SKIP_68K_TRAP_NAMES = frozenset(
    {
        "GetCCursor",
        "DialogDispatch",
        "SetPort",
        "DisposeDialog",
        "CloseRgn",
        "OpenResFile",
        "GetResource",
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
# BRA.S *, JMP (xxx.W,PC), RTS — loops/returns, not G3. Mill with G3_68K_MILL_LOOPS=1.
LOOP_68K_OPS = frozenset({0x60FF, 0x4EFA, 0x4E75})

# HARD 0x3264fc; skip-pair/skip-mfsr PCs already reverted worse.
HARD_SKIP_OFFS = frozenset({0x3264FC, 0x326564, 0x326568})

# GetCCursor proc (ROM): _GetCCursor at 0x5c86c, then DialogDispatch/SetPort.
# KEEP 0x5c86e skips into the proc; dest +8 still hits DialogDispatch.
# REVERT 0x5c89a/0x5c8d4 — do not skip-68k this WINDOW path.
UI_SKIP_68K_LO = 0x5C86C
UI_SKIP_68K_HI = 0x5C8C0  # through RTS 0x5c8be

# A-lines skip-68k mutes without 68k-loss. Do not mill skip of these.
NO_SKIP_68K_OPS = frozenset(
    {
        0xA97C,  # GetCCursor
        0xAA68,  # DialogDispatch
        0xA873,  # SetPort
        0xA983,  # DisposeDialog
        0xA8D9,  # CloseRgn
        0xA06E,  # OpenResFile (KEEP 0x16fc2/0x173f0)
        0xA9C9,  # GetResource (KEEP 0x16db8)
        0xA9A0,  # GetResource
        0xA88F,  # InitCursor (KEEP 0x151d8)
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
        try:
            tail = p.read_text(errors="replace")[-12000:]
        except OSError:
            continue
        if "pc=50366084" in tail:
            return True
    return False


def _log_for_n_simple(n: int) -> Optional[Path]:
    p = Path("/tmp/ss-g3-mill-%d.log" % n)
    if p.is_file():
        return p
    copy = HERE.parent / ("ss-g3-mill-%d.log" % n)
    if copy.is_file():
        return copy
    return None


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
        for k in ("cfm-aa5a", "trap-68k", "reenter-68k"):
            key = "leftover:%s" % k
            if key not in keys and key not in rev:
                return True
        return False
    keys = set(tested_keys(tested, "leftover"))
    rev = set(reverted or [])
    for k in KIND_68K:
        if k in ("skip-68k", "cfm-aa5a", "trap-68k", "reenter-68k"):
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
    if 0x326000 <= o < 0x327000:
        return False
    if UI_SKIP_68K_LO <= o < UI_SKIP_68K_HI:
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
    """True = do not mill skip-68k this ROM off (HARD/UI/FS/loop/data)."""
    if not skip_68k_millable(off):
        return True
    if skip_68k_loop_op(op) or skip_68k_ui_op(op):
        return True
    return False


def _r24_to_off(r24: int) -> int:
    r24 = int(r24)
    return r24 - 0x50000000 if r24 >= 0x50000000 else r24


def _68k_pairs_from_log(path: Optional[Path]) -> List[Tuple[int, Optional[int]]]:
    """(rom_off, op or None) from map lines then spin lines."""
    if path is None or not path.is_file():
        return []
    try:
        text = path.read_text(errors="replace")
    except OSError:
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
    if path is None or not path.is_file():
        return []
    try:
        text = path.read_text(errors="replace")
    except OSError:
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
    """Untested map/spin ROM offs. Returns (next offs up to limit, prefix counts, remaining n)."""
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
    p = Path(str(log_path))
    if not p.is_file():
        return None
    last = None
    for line in p.read_text(errors="replace").splitlines():
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
        if k in ("cfm-aa5a", "trap-68k", "reenter-68k", "grok-escalate"):
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
