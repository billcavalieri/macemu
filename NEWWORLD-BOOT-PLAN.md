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

### S1 — Regression truth (½ day)

Build this tree and boot **Mac OS 9.0.4** with **ROM 1.6** (ROM Update 1.0)
to the Finder on Apple Silicon.

- Done: Finder desktop, menu bar, About This Computer says 9.0.4.
- Red: does not boot. That is bug #1; nothing New World is trustworthy
  until it is green. `OS921-BOOT-PLAN.md` WP1 already requires this.
- Kill: red for > 2 days → restart New World work from upstream SheepShaver
  and port G0–G2 (device tree, MMU, NK exception handling) over cleanly
  instead of un-milling `ppc-cpu.cpp`.

### S2 — Cheap goal test (½ day, after S1 green)

From the 9.0.4 desktop, mount `Mac OS 9.2.1.toast` and run `Mac OS Install`.

- Outcome A: Welcome window appears. Stated goal met; proves the installer
  needs nothing but a working Toolbox. Reorder: install 9.2.1 to an HFS
  image first, then work on booting *that*.
- Outcome B: installer refuses on machine ID. The gate is Gestalt / ROM
  identity, a small target. Record exactly what it checks (resviewer on
  the Upgrader PEF: `Gestalt` selectors, `'Mac OS ROM'` version reads).
- Outcome C: crash/hang. Log it; it is an installer-vs-Toolbox bug, not a
  boot bug, and is out of scope until S4.

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
   `rom_patches.cpp` / `emul_op` driver replacement, as Old World does for
   9.0.4 and as NewSheep did for New World. One device per change.
5. Done-test per device: the diff's first divergence moves later.

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

- ROM and toast bytes never land in git.
- 9.0.4 with ROM 1.6 must keep booting after every change.
- "Fix the gate" over "skip the gate". If a change's only justification
  is that the guest gets further, it is a skip; do not land it.
