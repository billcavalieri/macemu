# New World boot plan (branch `newworld-boot`)

**Goal:** the Mac OS 9.2.1 installer's Welcome window, drawn by the guest's own
Toolbox, in SheepShaver on Apple Silicon. Then the rest of `OS921-BOOT-PLAN.md`
(G4 install, G5 Finder, G6 JIT).

**Base:** `g3` @ `f9c0ef0a`, tagged `g3-mill-frozen`. G0–G2 from that branch
(ROM decode, `MacRISC2` tree, NK v2 with MMU on, first DSI correct) are kept.
Everything after the 68k handoff is not.

---

## Why this branch exists

The `g3` mill lane (17,479 hang-caps, 17,367 applied mills, `ppc-cpu.cpp`
3.6k → 37.7k lines) ended with a 506×49 host-blitted PICT on black. Root
cause, from the mill 17479 log:

- The 68k emulator enters at `r24=5000002a`. At the **first** A-line trap
  (`A01F`, `pc=50008672`, ~150 68k instructions in) a host stub takes over.
  293 `op68 == 0x…` stubs then answer every trap the ROM issues, mostly with
  `gpr(8) = 0`.
- `_Launch` (`A9F2`) is intercepted; the Upgrader PEF is read from a fixed
  toast offset and its 274 InterfaceLib imports are host stubs (`r3 = 0`).
- No HFS mount, no System file, no INITs, no Window Manager. "System Error
  2050" was the installer correctly reporting a hollow environment.

A `CWindow` needs Window/Layer/Control/Dialog/Font/Resource/Memory Mgr and
their shared low-mem state. Those live in the ROM 68k image and the System
file, which is exactly what the mill lane bypassed. Trap-by-trap host stubs
have no fixed point; 25 consecutive KEEPs moved the framebuffer 0 px.

Rule for this branch: **fix the gate, do not skip past it.** No instruction
skipping, no A-trap stubs, no host-side `_Launch`. When the guest hangs, the
question is *what hardware or state did it probe that is missing*, answered
by diffing against a golden trace.

---

## Steps

Each step has a done-test and a time box. Stop and reassess at any red.

### S0 — Freeze (done)

`g3` committed at `f9c0ef0a`, tagged `g3-mill-frozen`. Reference only.

### S1 — Regression truth (dropped)

Dropped by decision: 9.0.4 / ROM 1.6 is not a goal of this branch and is not
a gate for New World work. Nothing here is required to keep the Old World
path booting; changes are judged only by the S3 golden diff.

### S2 — Cheap goal test (struck)

Struck: 9.2.1 does not run in SheepShaver from a 9.0.4 desktop either
(observed), so this test proves nothing about the 9.2.1 boot path.

### S3 — Golden reference (1–2 days)

`/opt/homebrew/bin/qemu-system-ppc -M mac99` boots the 9.2.1 CD today.
Capture from NK handoff to Welcome window:

1. The Open Firmware device tree QEMU exposes (`dev / ls`, `.properties`
   per node, or `-d` dumps).
2. PPC exception sequence (`-d int`).
3. 68k A-trap sequence (`-d in_asm` filtered on the 68k emulator's dispatch,
   or gdbstub breakpoints on the A-line handler).

Tooling (newworldview):

- QEMU trace importer alongside the existing `MillLogParser`.
- Diff view: QEMU trace vs `NW-BOOT` log, aligned on PPC exception / A-trap
  sequence. Output: **first divergence**, not first hang.
- SuperMario A-trap dispatch symbolication (68k PC → trap name); wire
  `SuperMarioParser` to `ATrapTable`. Fix the documented `A97C`/`A97D`
  swap.
- ROM driver inventory (`DRVR` / `ndrv` in parcels) so the hardware the
  ROM will probe is known before it probes it.

Done: `newworldview diff --qemu <trace> --nwboot <log>` prints the first
divergent event with both contexts.

#### S3 status: done

**Capture (QEMU 11.1.1, `-M mac99,via=pmu -m 512 -cpu g4`, toast as IDE CD).**
`-d int` / `-d in_asm` were too slow and too lossy (500 MB for a partial
boot); replaced with a TCG plugin, `research-score/golden/nwgolden.c`
(build line in its header). It logs every exception/interrupt
(`qemu_plugin_register_vcpu_discon_cb`) and every 68k A-line dispatch,
found by matching the emulator's four A-line handler entry words
(`80bc0028` at `ROM+0x3695E0/0x369660/0x369720/0x369780`), reading
`op=(r29>>3)&0xffff` and the 68k PC from `r24-2`. Full boot to Welcome in
99 s at native speed.

`research-score/golden/capture.py` drives the run: QMP screenshots every
5 s, detects the Finder desktop by frame stability, types `Mac OS Install`
+ Cmd+O, detects the Welcome window the same way, and writes `phases.json`
(byte offsets into `events.txt` for `desktop`, `launch`, `welcome`).
`ofwalk.py` + `qmp.py` dump the OF device tree over the serial console
(`devtree-mac99.txt`, checked in).

Event grammar (one line per event; the same grammar is the S4 contract for
SheepShaver's `NW-BOOT X …` / `NW-BOOT A …` lines):

    # nwgolden 1 target=ppc
    X <E|I|H> <from-pc> <vector> [<dar>|<srr1>]   DAR for 0x300/0x600, SRR1 for 0x700
    A <op> <68k-pc> <handler 0..3>                 0/1 OS, 2 Toolbox, 3 Toolbox autopop
    T <epoch-ms> <nX> <nA>                         ≤ 1 tick/s

Address layouts (both emulators compared in MacROM file-offset space):

| | QEMU mac99 | SheepShaver |
|---|---|---|
| 68k ROM (MacROM+0) | `0xFFC00000` | `0x50000000` |
| PPC part (MacROM+0x300000…) | `0x68000000` | `0x50400000` (copy) |
| NanoKernel RAM copy (MacROM+0x310000…) | `0x00F10000` | runs in place |
| Firmware | OpenBIOS `0xFFF00000` | — |

Golden run `~/nw-golden/run1/` (never in git: derived from ROM/toast):
`events.txt` 103.6 MB, 3,756,605 events from NK handoff; desktop at 70.7 s,
Welcome at 99.0 s (`shot-0019.png`). Exceptions by vector: PROGRAM 2.69 M
(all `twi` kernel calls at `ROM+0x36E8C0` plus NK priv traps), SC 112 k,
FPU 45 k, DEC 20 k, EXTERNAL 12.8 k, DSI 9.6 k, VPU 3.8 k, **ISI 0**.
A-lines: 860,499 across 318 distinct traps; first ten after handoff:
`_SetOSTrapAddress _InitZone _SetApplLimit _MoreMasters _NewPtrSysClear
_BlockMoveData _NewHandleSysClear _MoveHHi _HLock _DisposeHandle`.

**Tooling (newworldview, all built and unit-tested in `BootTraceTests`).**
Source snapshot of every S3 file (new and modified) is mirrored under
`research-score/golden/newworldview/` with the same relative paths; the
sibling repo `~/Documents/GitHub/newworldview` is where they build.

- `GoldenTraceImporter` (streaming, 100 MB in ~10 s), `NWBootLogImporter`
  (canonical `NW-BOOT X/A` plus the legacy `G3: DSI/ISI/sc/DEC/68k A-line`
  lines), `BootLayout.qemuMac99` / `.sheepShaver`, `BootTraceDiff`.
- `diff --qemu <events.txt> --nwboot <log> [--rom] [--phases] [--ignore]
  [--context N] [--compare-dar] [--strict-async]`: per-channel n-th
  occurrence comparison on ROM offsets, async vectors (0x500/0x900)
  matched by presence only, prints the first divergence with SheepShaver
  log context and golden stream context plus golden phase.
- `golden-stats`, `golden-atraps [--unique] [--phase]`, `trap-table
  [--trap A9F2] [--at off]`, `drivers <rom> [--devtree] [--json]`.
- `SuperMarioTrapTable` decodes the 9.2.1 long-form dispatch table (1024
  Toolbox + 256 OS longs ending at `RomRsrc`; packed form kept for older
  ROMs). `AnalysisEngine` seeds one `.function` symbol per implemented trap
  (`68k:2c010 → _Launch`, `68k:4b500 → _WaitNextEvent`). `ATrapTable.names`
  now comes from cxmon's 1177-entry table via `ATrapNames`; lookup is
  flag-insensitive (`A148 → _PtrZone`, `AD7C → _GetNewDialog`);
  `A97C = _GetNewDialog`, `A97D = _NewDialog` (swap fixed).
- `ROMDriverInventory` + `OpenFirmwareTreeDump`: 24 `prop` parcels, 2
  `node` parcels (CodePrepare/CodeRegister libs), 3 68k `DRVR`s (`.ATALoad`,
  `.EDisk`, `.ATADisk`). Cross-referenced with `devtree-mac99.txt`: ROM
  ndrvs match `nvram,flash`, `uni-north/pci`, `via-pmu/rtc` (via parent),
  `via-pmu-99/power-mgt`, `keylargo-ata/ata` ×2, `gmac/network`; the
  display is driven by OpenBIOS's own `driver,AAPL,MacOS,PowerPC` on
  `QEMU,VGA@e` (no ROM `cofb` match); `escc`, `usb`, `open-pic`, `mac-io`
  have no ROM native driver.

**Done-test result** (`diff --qemu ~/nw-golden/run1/events.txt --nwboot
ss-pr10-0b9c914c.log --rom "Mac OS ROM"`): first divergence is
SheepShaver's `ISI (0x400)` at `ROM+0x36E8C0` (the emulator's `twi`
kernel-call, first instruction after `rfi` into the 68k emulator). The
golden run raises **zero** ISIs after handoff; at that same PC it takes
`DEC (0x900)` then `PROGRAM (0x700) srr1=0002D032 trap`, i.e. the `twi`
executes and traps into the NK. So the first gate is instruction
translation for the PPC part at `0x5046E8C0` with `MSR[IR]=1`, not a
device probe. That is the S4 starting point.

### S4 — Gate model at the 68k boundary (open-ended, measured per device)

1. Revert `skip-68k`, the 293 A-trap stubs, and the host `_Launch` /
   InterfaceLib import stubs. Keep G0–G2 code. Keep `nw_boot_log`.
2. Run. The ROM will hang on its first hardware probe.
3. Use the S3 diff to name the probe (device node, register, or trap).
4. Provide it the SheepShaver way: a minimal device model, or a
   `rom_patches.cpp` / `emul_op` driver replacement, as Old World does and
   as NewSheep did for New World. One device per change.
5. Done-test per device: the diff's first divergence moves later.

#### S4 step 1 status: done (commits `91306bc0`, `03773224`, `48222634`)

The mill was entangled with G2 inside `857f320e` ("G0–G2 live" already had
22.5k lines and 177 stubs), so a pure `git revert` was impossible. The
revert is reconstructive: the New World CPU/glue files were reset to the
clean G1/G2 base `9b0625e8` (ppc-cpu.cpp 1,006 lines) and only architectural
pieces were re-added. Three commits, one per category, each `git revert`-able:

| commit | scope | removed | kept / added |
|---|---|---|---|
| `91306bc0` 1a | `kpx_cpu` | all `g3_*` / `nw_dec_*` helpers, 23k-line `do_interpret` mill, 68k resume, r1/ea fix, ISI retry, sc 0x2e, PEF stubs, MMU-off and PA-whitelist guards, SPRG3 `hotints_vector`, `ivt_mapped`/`delay_ram_bats`, xlate logging | `take_exception` (SRR0/SRR1, MSR clear, vector by MSR[IP]); DSI, ISI, `sc`→0xc00, DEC→0x900 when EE, `tw`/`twi` and illegal→0x700 with SRR1 bits; DEC SPR + tick; fetch outside RAM/ROM/low-mem → 0x200 |
| `03773224` 1b | `nw_boot_contract.{h,cpp}`, `sheepshaver_glue.cpp` | skip-68k offset, G3 strings, video/click bridge, dec pin/leave, NK IRQ/PIC plant, picspin, host 68k dispatch, host HandleInterrupt policy, ROM HTAB PTE seed, DSI vector fill, planted 0x300 vector, identity BATs, KERNEL_DATA plants | G0 decode, G1 tree/KDP/HTAB gate, G0–G2 log lines; `NKSystemInfo` in r5 (data the Trampoline supplies); event API |
| `48222634` 1c | `rom_patches.cpp`, video, `main_unix.cpp`, project | NK code patches (PMDT-panic branch 0x31e5e0, print→`blr` ×4, `mtmsr`→`nop` ×2), G3 `MacOSX/video_sdl2.cpp` (dirty bbox, host paint, click, FB dumps), `--g3-skip-68k`, `/tmp/ss-g2-run.log` | project back on `BasiliskII/src/SDL/video_sdl2.cpp`; SDL software-renderer hint |

Event stream (S3 grammar, consumed by `NewWorldViewCLI diff`):
`NW-BOOT X E <srr0> <vector> [<dar>|<srr1>]`, `NW-BOOT A <op> <68k-pc> <h>`
(observed when the fetch PC hits one of the ROM emulator's four A-line
handlers — observation only), `NW-BOOT T <ms> <nX> <nA> <pc> <msr>` once a
second (pc/msr are extra fields so a silent spin is still located).
Debug-only observers under `NW_BOOT_LOG`: `PCTRACE` ring dumped once when
the PC first enters the NK debug region ROM+0x325500..0x325fff, and
`NKPANIC` registers at ROM+0x326420/0x326428.

Build/run/diff recipe:

```
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj \
  -scheme SheepShaver -configuration Debug ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  -derivedDataPath /tmp/macemu-s4-dd
perl -e 'alarm 100; exec @ARGV' -- \
  /tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver.app/Contents/MacOS/SheepShaver \
  --config "$HOME/Library/Application Support/SheepShaver/os921/prefs" > /tmp/ss-s4.log 2>&1
NewWorldViewCLI diff --qemu ~/nw-golden/run1/events.txt --nwboot /tmp/ss-s4.log \
  --rom "$HOME/Downloads/Mac OS ROM"
```

`SheepShaver-MMUTests` (107 host checks) builds and passes on the same tree.

**Redo (undo the revert):** `git revert 48222634 03773224 91306bc0`, or
restore the whole mill with `git checkout g3-mill-frozen -- SheepShaver/src`
(the G3 `MacOSX/video_sdl2.cpp` comes back with it; re-point the Xcode
project's `video_sdl2.cpp` reference to `path = video_sdl2.cpp;
sourceTree = SOURCE_ROOT;`).

#### S4 step 2 — first divergence (closed by step 4a; kept as the record)

100 s run: **0 exceptions, 0 A-lines**. The diff reports no comparable
events; the `T` lines put the CPU in ROM+0x325xxx–0x326xxx with MSR=0x2000.
`PCTRACE`/`NKPANIC` name it:

```
0x310750  bl 0x310790 -> bl 0x325520      NK banner print ("Hello from multitasking …"), returns
…
0x31e878  bltl 0x31e5e0 -> b 0x326420     NK panic entry (lr=5031e87c, r1=KDP=17efe000, sprg0=KDP, msr=0)
0x32665c..0x32667c                        debugger wait loop: ++*(u32*)0, poll 0x3259c0 (no debug port → -1), forever
```

`0x31e7c0..0x31e878` builds an `'area'` record per PMDT entry (page index
at +0, count-1 at +2, phys/flags at +4; base = (index<<12)+r26) and panics
when the next area's base is **below** this one's (`subf.` … `bltl`). The
PMDT the NK is converting comes from the ConfigInfo block that
`patch_nanokernel_boot` writes at ROM+0x30d000 for the Old World layout, and
from the `NKSystemInfo` bank list in r5. Golden takes this path without a
panic. So the first gate is a **G1 handoff-data gate**: the ConfigInfo PMDT /
SystemInfo banks handed to NK v2 are out of order or overlapping for the
9.2.1 ROM's expectations. Next: dump the 9.2.1 ConfigInfo PMDT (resviewer /
newworldview `BootInfoParser`) and the QEMU `mac99` ConfigInfo after the
Trampoline, and make SheepShaver's handoff data consistent. This is data,
not code, and not a skip (the mill's answer was to rewrite the `bltl`).

#### S4 step 4a — handoff-data gates fixed (commits `263565b1`, `d78ccec6`, `59a10dab`, `55d7f2ab`)

Golden reference data, captured by the QEMU plugin
`research-score/golden/nwdump.c` (build: `cc -O2 -shared -fPIC -Wall
-undefined dynamic_lookup -I/opt/homebrew/include $(pkg-config --cflags
--libs glib-2.0) -o libnwdump.dylib nwdump.c`; run the S3 QEMU command with
`-plugin libnwdump.dylib,dir=$HOME/nw-golden/dump2`): registers and the
r3/r4/r5/r9 records at NK entry (PC 0x10000), KDP±0x4000 and PA 0..0xffff
when the NK reads the PMDT. `~/nw-golden/dump2/` (never in git). What it
established, in the order the gates were fixed:

| gate | golden fact | SheepShaver fix (data, Trampoline's job) |
|---|---|---|
| PMDT panic 0x31e878 | 9.2.1 ROM ships an **empty** page map; the Trampoline fills ConfigInfo: per-segment PMDT (ascending areas, `(0,0xffff,0xa00)` terminators), 4 segment maps, BATRangeInit, BatMap nibbles, `PA_RelocatedLowMem`, v1.01 | `nw_fill_config_info_be` (ROM identity, NK 1 MiB → ROM+0x300000, IRP/KDP/EDP one page each with PA 0, SheepMem, DR cache, frame buffer); `nw_config_info_pagemap_ok` |
| NK SystemInfo | bank list at +0x30, RAM size at +0/+4 | `nw_fill_system_info_be`, bank 0 = whole RAM, low memory at its start (`PA_RelocatedLowMem` = RAMBase) |
| DEC storm | NK timeslice comes from `NKProcessorInfo` (+0xc TB Hz); zero → `mtdec 0` forever | `nw_fill_processor_info_be` (PVR, CPU/bus/TB Hz, cache geometry); DEC decrements at TB rate |
| vectors at PA 0 | Trampoline copies ROM+0x300000 (0x2800 B) to PA 0 | `Host2Mac_memcpy(0, ROM+0x300000, 0x2800)` in `init_emul_ppc` |
| emulator kernel calls | `twi r31,n` table at ROM+0x36e8c0 → 0x700 to the NK | Old World rewrote it to host routines; New World leaves it (and the DR emulator entry) to the NK |
| CPU feature probes | NK probes MQ (`mtspr 0`), AltiVec (`lvewx` with MSR[VEC]=0 → 0xf20), SIAR (read-only in QEMU) and expects the exceptions | `kpx_cpu` G4 SPR model (7400 as QEMU `-cpu g4`), MSR[VEC] gating, TB write offset |
| 68k ROM location | ROM's own MacLowMemInit says reset PC `0xffc0002a`; Trampoline BAT range 1 maps LA 0xffc00000 (4 MiB, RO) to the ROM copy | BAT range 2 → ROM area, BatMap slot 1; the ROM's table is no longer overridden (we had pointed it at the ROM area identity, which sent the 68k into 0xffffaa00) |

Result: the exception stream matches the golden exactly for the first 22
non-timer events after the NK probe (`X E …3113c0 0700`, `…3113c8 0700`,
`…3113dc 0f20`, `…3164b8 0700`, kernel calls 2 and 0, first KDP/EDP/IRP/CI
page faults), the 68k runs ROM code at 0xffc0aeec…; **107** harness checks.
Stream comparison: `NewWorldViewCLI diff` stops at the first NK-internal
0x700 (it treats SheepShaver's 0x68xxxxxx PCs as outside the ROM); until
that is fixed in newworldview, `/tmp/xdiff.py`-style normalisation (NK
0x00f1xxxx / 0x5031xxxx → ROM offset, drop 0x900) is the comparison.

#### S4 step 4b — next divergence (open): the hardware-info block

Golden line 3457 `X E 6806c9e8 0300 68fffa18` … then `68061c14 0300
80017e00` / `80016600` (VIA at mac-io 0x80016000). Ours: `6806d7cc 0300
000025f8`, `6806d7e0 0300 2e4d4ed6`, `6808b150 0300 00001e00`, `68061c14
0300 00000600`, then a 68k bus-error loop (0 A-lines, 10 M events/60 s).
The 68k (`ffc0aa4a movea.l $2c(a0),a2; move.b $1e00(a2),d3`) reads its I/O
base addresses from a record it finds through `KDP+0xfd0`; the NK sets
`KDP+0xfd0 = LA_InfoRecord+0xf00` (0x3108c0) and fills IRP+0xf00 with the
0xc0-byte block the Trampoline passes in **r9 when r7 == 'RTAS'**
(0x310690), checking `'Hnfo'` at +0x70 (0x3109f0; present → skips the CPU
probe table). We pass r7 = 0, so the block is zero: VIA base 0, and the
"garbage" DSI addresses in the golden (0x64051de2) are pointers from this
block (+0x8 = 0x64051dd0). Golden block (`dump2/nkentry-r9.bin`): +0 ROM
PA 0xc00000, +4 0x6400000c, +8 0x64051dd0, +0xc 0x3000 (ConfigInfo PA),
+0x10 0x68feff80, +0x14 0x68feff40, +0x18 0x80040000, +0x3c 0x1400,
+0x70 'Hnfo', +0x74 0x00403035, +0x78 0x00250024, +0x7c 0x08000800,
+0x80 0x00190002, +0x84 0x08000037, +0x88 0x00010000, +0x94 4,
+0x9c 0x00410000, +0xa0 0x63173569, +0xa8 0x68fefcfc; r23 = 0x80012000 is
the NK debug SCC (KDP-0x8e0). Next: decode the 68k's use of each field
(`ffc0aa40`, `ffc0aee4`), build the block for SheepShaver's layout, pass
r7/r8/r9. After that the host side: SheepShaver's own accessors still speak
the Old World map (lowmem host page at 0, `KernelData` at 0x68ffe000) while
the NK owns LA→PA (Mac LA x ↔ PA RAMBase+x, KDP at the NK's PA); that is
the gate after this one.

G3 is reached when the ROM mounts the CD, loads the System, and the
System's own `_Launch` starts `Mac OS Install`, which draws its window
through the guest QuickDraw. A host blit is not G3. `G3_WINDOW=yes` is
never set by host code.

### S5 — Rest of `OS921-BOOT-PLAN.md`

WP3 ARM64 JIT on the MMU, WP4 memory banks, WP5 video damage. Not before G3.

---

## What is retired on this branch

- `research-score/g3_driver/` mill pipeline: `mill_apply.py`,
  `mill_pef_*.py`, `run_qdloop_25.py`, skip-68k policy, histogram /
  annotation / Grok escalation exports. Left in tree for history; not run.
- `state.json` mill counters. Do not resume from them.
- Any hang-cap KEEP/REVERT loop whose success metric is "not worse".

## What stays

- `nw_boot_log` and the G0/G1/G2 log lines and accept criteria.
- resviewer as the resource / PEF inspection tool (no changes needed).
- `ROM-milling-brief.md`, `OS921-BOOT-PLAN.md` as history and as the
  WP0–WP6 architecture. This file governs sequencing.

## Non-negotiables carried over

- ROM, toast and golden capture bytes never land in git.
- (Removed: the 9.0.4 / ROM 1.6 regression requirement. 9.0.4 is not a goal
  of this branch and is not a gate for New World changes.)
- "Fix the gate" over "skip the gate". If a change's only justification
  is that the guest gets further, it is a skip; do not land it.
