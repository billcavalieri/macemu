# New World boot plan (branch `newworld-boot`)

**Goal:** the Mac OS 9.2.1 installer's Welcome window, drawn by the guest's own
Toolbox, in SheepShaver on Apple Silicon. Then the rest of `OS921-BOOT-PLAN.md`
(G4 install, G5 Finder, G6 JIT).

**Status:** G3 reached (S4 step 6); **G4 reached** (S4 step 7): the
installer put 9.2.1 on the 2 GB volume and the installed System boots to the
Finder (Mac OS Setup Assistant, Control Strip) in ≈ 140 s, interpreter. The
SDL window shows the guest display through SheepShaver's own video ndrv
with a real VBL interrupt (S4 step 8); the cursor tracks the ADB mouse.
PRAM persists across runs through a model of the boot flash's NVRAM
blocks (S4 step 9 addendum). Next: PMU restart/shutdown, then G5 polish
and G6 JIT.

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

#### S4 step 4b — the hardware-info block (closed by step 4c; kept as the record)

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

#### S4 step 4c — gates fixed from the hardware-info block to the device tree

Commits `849d07b7`, `075dbfcf`, `6671aad2`, `3a43b423`, `aa560162`,
`2fff43c7`, `31b507b8`, `ef5258d9`. Golden data: `~/nw-golden/dump2/`
(nwdump third trigger: `nkentry-r5.bin` SystemInfo, `hwinfo-kdp.bin`,
`hwinfo-configinfo-pa.bin`, `hwinfo-lowmem-pa.bin`, `hwinfo-bootinfo-pa.bin`
1.5 MiB boot-info area). Never in git.

| gate | golden fact | SheepShaver fix (data or device model, never a code patch) |
|---|---|---|
| hardware-info block | r7 = 'RTAS', r9 → 0xc0-byte block ('Hnfo' at +0x70, machine id 0x3035 at +0x76, pointers to the boot-info entry list, the ProductInfo/DecoderInfo record, ConfigInfo tail tables 0xf80/0xf40/0xcfc, OpenPIC 0x80040000) | `nw_fill_hwinfo_be` (`nw_boot_contract.cpp`); `init_emul_ppc` passes r7/r8/r9; boot-info area at LA 0x64000000 (`NW_BOOTINFO_*`); the 68k-consumed ProductInfo/DecoderInfo record (`hwrec[]`: VIA 0x80016000, SCC 0x80012000, OpenPIC 0x80040000, flags 0x1c) |
| ConfigInfo tail | Trampoline fills +0xf80 interrupt source list (= union of the tree's `AAPL,interrupt-vectors`), +0xf40, +0xcfc | `nw_fill_config_info_be` tail tables |
| MMU protection | 68k emulator relies on DSI on RO pages (PP bits, R/C), `tlbie` by class, SR-tagged TLB | `kpx_cpu` `ppc-mmu` OEA protection, R/C update, class-based `tlbie` (`3a43b423`) |
| I/O access | mac99 devices at PA 0x80000000 (mac-io), 0xf0000000–0xffffffff (uni-north, PCI config, flash) | `nw_io.{h,cpp}` dispatch keyed on **PA** after translation; PMDT segments for 0x80000000 / 0xf0000000 with attr 0x3a (cache-inhibited, guarded); unclaimed accesses logged (`NW-BOOT IO …`), first touch per 4 KiB page always (`NW-BOOT IO page …`) |
| RAM geometry | NK 0x310548: r12 = `PA_RelocatedLowMem`; the first non-empty bank gets base += r12, size −= r12, total −= r12; lowmem zeroed + `MacLowMemInit` at PA r12; NK area at the top of the last bank (KDP+0x638/+0x63c); logical RAM = banks concatenated minus that area into per-segment page tables; installer 0x3123fc rewrites the **first** PMDT entry of each RAM segment in place | SystemInfo bank 0 described from **PA 0** with size `RAMBase+RAMSize` (the trim yields `(RAMBase, RAMSize)`); two `(0,0xffff,0xa00)` entries per RAM segment so the in-place rewrite cannot collide with the next segment's list. Found at 512 MiB (`ramsize 536870912` in the os921 prefs, kept so MemTop-relative addresses stay comparable with the golden — offsets from MemTop now match, e.g. 0x51004) |
| device tree | boot-info +0xc is a **BGsTree**: 12-byte node records `{sibling, child, first-prop}` in pre-order, then property records `{next (= size, 0 last), name[32], len, value pad 4}`; the 68k importer (ROM 0x44420–0x44580, first read from PC 0xffc44440 of root +0x14) walks it into the Name Registry. Parcels ('prcl' after the ROM image in the ROM file; no "parcels-offset" constant — locate by scanning): node 88 B `{link, ostype, hdrSize, flags, +20 childStride, a[32]@24, b[32]@56}`, child 60 B `{ostype, flags, 'lzss'@8, unpackedLen@12, cksum@16, packedLen@20, ptr@24, name[32]@28}`. 'node' parcels create `AAPL,CodePrepare` (flags 0x20000) / `AAPL,CodeRegister` (0x10000); 'prop' parcels match flags 1 name==a, 2 parent==a, 4 compatible∋a, 8 device_type==b; child 0x10000/0x20000 → code nodes, 0x100 skip (EtherPrintfLib), 0x20 add-if-absent (cofb vs a card's own ndrv); 'psum'/'rom ' ignored. Verified against the golden tree: identical property lengths and PEF headers for every shared node | `nw_bootinfo.{h,cpp}`: mac99-like tree (uni-north `pci`, Keylargo `mac-io` with gpio/via-pmu/rtc/power-mgt/escc/escc-legacy/ata-3 ×2 with cdrom/interrupt-controller, nvram flash, uni-n, cpus by PVR, memory, chosen, options, packages, aliases, rom/macos) plus a `display` node backed by SheepShaver's frame buffer (address/width/height/depth/linebytes; the ROM's cofb ndrv attaches); parcel merge with LZSS; `DecodeROM` keeps the 'prcl' blob (`nw_parcels_keep`); hardware record moved to +0x100000; 196 harness checks; `/tmp/bitree.py` walks a dump |

Result (60 s run, 512 MiB): the exception stream matches the golden
structurally through the NK, the 68k StartInit, the tree import and the
native driver start-up; remaining early differences are geometry (MemTop,
heap block Δ0x6c0, boot-info page-touch order). A-trap sequences match for
the first 1190 traps and thereafter differ only in the data-dependent
import loops (`a148 ffc41d9a` per property). The guest drivers now touch
exactly these unclaimed register pages, which is the device inventory of
the mac99 path: VIA-PMU 0x80016000/0x80017000, ESCC 0x80012000, OpenPIC
0x80040000 + source registers 0x80050000, NVRAM flash 0xff004000–0xff006000
(golden touches the same, PC 0x572d8), uni-n 0xf8000000, PCI config
0xf2800000/0xf2c00000, Keylargo ATA 0x80020060 / 0x80021060 (drive-select
0xa0, read back 0 → no drive).

Tools (in `/tmp`, not in git): `xdiff.py golden ss.log [--loose|--lowonly]`
strict first divergence; `xdiff2.py golden ss.log [minblk]` difflib
alignment of the first 2500 events after the NK probe; `adiff.py` the same
for `(op, 68k-pc)` A-trap pairs; `bitree.py dump [-v]`; `prcl.py`;
`dis68.py <addr> <len>` (cstool m68k40, resyncs on bad words).

#### S4 step 5 — a drive in the drive queue: done, hybrid route (commits `ee1b547a`, `d12d9fdc`, `5dce0c58`)

Both runs reach ROM `0xffc03808` (open `.LANDisk`, fails in both) and
`0xffc038a6`: walk `DrvQHdr` (`$30a`), for each drive ≤ 15 whose bit is set
in `$b0e` call the try-boot routine (`0xffc03916`: `_Read` 1024 bytes at
offset 0, check `'LK'`). Golden: one pass, `a002 ffc0392c` (`_Read`), then
the boot blocks draw (`aa14`/`a8a5` at `ffc03b0a`/`ffc03b24`). Before this
step our drive queue was empty (the keylargo-ata ndrv probed
`0x80020060`/`0x80021060`, found nothing) — the "flashing ?" stage.

**Decision: hybrid.** Host-backed `EMUL_OP` DRVRs (SheepShaver classic:
`.AppleCD`/`.Disk` on the toast/hfv images, later video, ADB, XPRAM) plus
small device models only for what the NK's and the ROM's own native code
need (OpenPIC, VIA-PMU shim, NVRAM flash, uni-n, PCI config). Not the QEMU
route (ATA/DBDMA/ATAPI models), not NK code patches, not a stub for the
ROM's keylargo-ata ndrv. Rationale: performance (one `EMUL_OP` per I/O
request, host memory, the same path the JIT already takes on Old World)
and stability (the NK's own interrupt path and page tables are untouched;
the 68k side is identical to the classic SheepShaver contract).

Boundary table (what runs where):

| piece | who | how |
|---|---|---|
| NK, 68k emulator, interrupts, page tables | ROM, unpatched | — |
| `EMUL_OP` reach | `patch_68k_emul` | opcode-table entries at ROM+0x380000 only (`b 0x366084` dispatch; `r29` = table \| op<<3, `r24` = next 68k pc). Old World's 0x36f9xx helpers and DR-emulator patch are skipped |
| host "Mac address" | `nw_la_to_pa()` in `cpu_emulation.h` | LA<RAMSize → RAMBase+LA; LA≥0xffc00000 → ROMBase+off; KDP LA 0x68ffe000 → PA learned at the first `EMUL_OP` (`execute_emul_op`); SheepMem / frame buffer / boot-info identity. `Execute68k` keeps the live emulator registers; classic `HandleInterrupt` injection is off (it wrote into the real KDP) |
| 68k ROM data patches (3) | `nw_patch_68k_drivers` | SetSysAppZone constants 0x2800/0x4000 → 0x3000/0x4800 (XLM globals at 0x2800 survive); DRVR bodies + icons in the zero padding at ROM+0x330000 (no DRVR 4 in 9.2.1); the `lea -$32(a7),a7` opening the ROM's driver-install routine (after `NewPtrSysClear; move.l a0,$11c; rts`) → `EMUL_OP_INSTALL_DRIVERS` + `nop`; the handler adjusts a7 and the ROM routine still installs `.EDisk` etc. |
| drivers | `nw_install_drivers` (`emul_op.cpp` OP_INSTALL_DRIVERS) | `DrvrInstallRsrvMem`, `HLock`, DCE fill, `Open` — `.AppleCD` first (drive 1), then `.Disk`; `AddDrive` from the drivers as on Old World |
| boot device | boot-info tree (`nw_bootinfo.cpp`) | `/host-drives/cdrom@1` and `/host-drives/disk@0`, `device_type "scsi"`, `AAPL,boot-cookie` = driver refnum; `/chosen bootpath` = the one prefs `bootdriver` names, no partition number |

StartLib (`GetStartupDevice`, parcel shlb, code base 0x5df60 in RAM this
run) is the arbiter; decoded from the PEF (`/tmp/sldis.py`, TOC strings
`device_type name ide ata scsi pci reg AAPL,bus-id device_id
AAPL,boot-cookie AAPL,USBNodeType`): resolve `bootpath` component-wise by
`name` + `reg`; for each drive-queue driver take `_Status 'boot'`
(0x6666 = none), dispatch on bits 11–15 of the boot ID (0x2000 → ATA:
`ata`/`ide` node with `AAPL,bus-id` = bus, child `device_id`/`reg` = dev;
0x2800 → none; else SCSI: `RegistryEntrySearch` from the root for
`AAPL,boot-cookie` == refnum (4 bytes) whose `device_type` starts `scsi`;
found node's unit = the boot ID's SCSI target (`>>27`)); accept when the
cookie node is the `bootpath` node (`RegistryEntryIDCompare`), the unit
addresses agree, and a `:N` partition in the path equals the one read from
the drive's partition map (skipped when either is −1). SheepShaver's DRVRs
answer 'boot' with `(drive<<11)<<16 | refnum` (CD) / `drive<<16 | refnum`
(disk), i.e. SCSI form, target = CD drive number / 0. The keylargo-ata ndrv
deletes the `ata-3` children it cannot probe (`RegistryEntryDelete` from
the ndrv, both buses), hence the separate root node. The `ata-3` nodes stay
as in the golden tree; the ndrv loads, probes, finds nothing, and is left
alone.

Result (clean build, three 40 s runs, 512 MiB): `.AppleCD` installed
refnum −62, `.Disk` −63, `GetStartupDevice` succeeds on the first call,
one `.LANDisk` open before the boot-block `_Read` (`a002 ffc0392c`, trap
~90 500), then the A-trap sequence matches the golden's post-boot-block
sequence (differences: the golden's `aaf1`/`aaf4` ATA-interrupt traps;
RAM heap Δ0x800 from the SetSysAppZone move). The System file loads from
the toast image and System-heap QuickDraw code runs (`a893` from RAM
`0x802a28` at trap ~162 000, ≈ 15 s). The window is black: no display
driver yet. `SheepShaver-MMUTests`: 196 checks pass.

Known stall (next gate, not this one): at ≈ 24 s a native task loaded
from the CD — the Multiprocessing CPU plugin (PEF at toast 0x2082860:
`Core99Probe`, `FindMPIC`, `FindUniNorth`, `CalculateBusClock`,
`WaitForZeroPCI`, `MakeSignal`, `SignalProcessor`) — spins on OpenPIC /
uni-n registers, once with EE=0 at RAM `0x27db84` (no exceptions at all),
once in MP timed waits (`sc` 0x5d/0x5e, timeout 0x4fff→0x7fff). That is
the OpenPIC + uni-n item below; the golden has both.

Next gates, in order: OpenPIC (0x80040000, sources 0x80050000) and uni-n
(0xf8000000) models so the CPU plugin and the NK's interrupt path see
real registers; host display driver (`driver,AAPL,MacOS,PowerPC` on the
`display` node, `NATIVE_VIDEO_DO_DRIVER_IO`-style) so the Happy Mac and
the Welcome window appear; VIA-PMU shim (timer / ADB); ADB input; XPRAM.
Also open: `a9c9 SysError` seen once at the end of one run (after System
code starts) — triage after the display driver.

G3 is reached when the ROM mounts the CD, loads the System, and the
System's own `_Launch` starts `Mac OS Install`, which draws its window
through the guest QuickDraw. A host blit is not G3. `G3_WINDOW=yes` is
never set by host code.

#### S4 step 6 — interrupts, device models, input: done, Welcome reached (commits `1db182ad`, `62d69bd0`, `5e5031f1`, `3447e358`, `6d7dca79`)

Gates fixed, in the order they were hit. Each is a device the golden
machine has and native code (NK, CPU plugin, PMU library, ADB Manager)
addresses directly; each got a model in `nw_devices.cpp` (hybrid rule:
models only for what native code touches, `EMUL_OP` DRVRs for the rest).

1. **OpenPIC, uni-n, PCI config, Keylargo timer/GPIO.** The MP CPU plugin's
   `Core99Probe`/`FindMPIC`/`CalculateBusClock` and the NK's interrupt
   path read real registers now. External interrupts: the CPU takes
   0x500 when the model raises `nw_io_ext_irq` and MSR[EE] is set (ahead
   of DEC); the models tick from the DEC sampling point on the host-side
   timebase (`tb_host_ticks`, unaffected by guest `mttb`).
2. **Exception state.** SRR1 carried only MSR[16..31]; the NK reads
   SRR1[VEC] to decide whether the interrupted context owns the vector
   unit, so SRR1 now takes MSR[0,5-9,16-31] and MSR[VEC]/[POW] are cleared
   on entry (7400 set). FP-unavailable (0x800) is raised for FP opcodes
   with MSR[FP] clear (the NK lazily enables FP as it does VEC). XER keeps
   its unarchitected bits: the 68k emulator stores mode flags in
   XER[22..23] and MixedMode's native entry tests them with `mfxer`.
   TAU: `THRM1/2` reads complete their comparison once `THRM3[E]` is set;
   the CPU plugin spins on `[TIV]` with interrupts off.
3. **Trampoline OpenPIC programming.** The Trampoline programs every
   source it collected from the tree (`AAPL,interrupt-vectors` /
   `-priorities`): `IVPR = masked | prio | vector`, vector = index in its
   list, level sense from the specifier, `IDR` = CPU 0, `CTPR` 0. The NK's
   0x500 handler maps the IACK vector to a 68k level with
   `lbz level, 0xf00(vector)` on the ConfigInfo page (the priority byte
   table in list order) by absolute PA — so the filled ConfigInfo now
   lives at PA 0x3000 (`NW_CI_PA`, golden hardware-info +0xc), r3 at NK
   entry points there, and the low-memory host mapping is 0x4000. The 68k
   StartInit afterwards only toggles mask bits; before this every source
   stayed at priority 0 and nothing was delivered. `nw_trampoline_irqs[]`
   is the golden mac99 list; the ConfigInfo tail tables are generated from
   it.
4. **VIA-PMU.** Shift-register protocol on the VIA at 0x80016000, PMU
   commands the PMU library and the 68k `PMgrOp` path issue, one-second
   and timer interrupts through GPIO1/`extint-gpio1` (source 0x2f) and
   `via-pmu` (0x19).
5. **Input.** The golden machine takes input over USB; SheepShaver has no
   USB. The Trampoline's `HandleSpecialNode` (ROM-file table at 0x197d0:
   `adb`/`chrp,adb0`, `adb`/`pmu`, `adb`/`pmu-99`, via-cuda, via-pmu,
   power-mgt, usb, keyboard, mouse …) encodes the input path it finds in
   the ProductInfo `UnivROMFlags` word (record +0x24, mirrored +0x7c;
   low-mem `$dd4`, ExpandMem +0x384/+0x3dc): bit 2 for "P99 ADB detected",
   bit 1 for "Virtual (USB-emulated) ADB detected!". The 68k ADB Manager
   (`ffc2b5f6`) selects bus routines by `UnivROMFlags & 0xe`: 0xa keeps
   the ROM default (no bus; every command completes without a device),
   0xc installs the PMU-99 routines (`ffc06bf0`: `PMgrOp 0x20` packets,
   `$19a` = PMU ADB interrupt handler `ffc06d56`). Golden `via=pmu`:
   `c003bf1a`; golden `via=pmu-adb`: `c003bf1c`; the rest of the record
   identical. SheepShaver presents the `via=pmu-adb` shape: tree
   `via-pmu/adb` (compatible `pmu-99`) with `keyboard@8`, `mouse@9` and
   the `kbd`/`keyboard`/`adb-*` aliases, `UnivROMFlags` bit 2, an ADB bus
   model behind the PMU (keyboard address 2 handler 1, mouse address 3
   handler 2, Talk/Listen R3, the address-collision dance, autopoll mask),
   and `adb.cpp` routes host key/mouse events to it on New World. The ROM
   enumerates both, autopolls (`[2c code ff]` with PMU interrupt bits
   0x14), and the System's RAM keyboard handler (`0019e4b6`) runs `a079
   GetADBInfo / a9c3 KeyTranslate / a02f PostEvent` as in the golden.

Result (clean build, `/tmp/prefs-nodisk` = golden-equivalent prefs, 512
MiB, foreground 200 s run): ADB Manager installs the keyboard handler
(`a079 0019e39c`, `a07c 0019e492`, trap ≈ 119 k), Finder `InitWindows`
(`a912`) at ≈ 172 k, `_Launch` (`a9f2`) at ≈ 393 k, 652 k traps and 1.8 M
exceptions in 200 s; typing `Mac OS Install` + Cmd+O in the Finder opens
the installer and its Welcome window is drawn into the frame buffer
(verified from a guest frame-buffer dump during development; the SDL
window mirrors that buffer). `SheepShaver-MMUTests`: 396 checks.

Not done, not blocking: the ROM's own display driver semantics (the
guest draws straight into the `display` node's frame buffer; no mode
switch, gamma, or VBL from a driver), XPRAM/NVRAM persistence, Keylargo
GPIO details, the IDE probes at 0x80020000/0x80021000 and SCC polling at
0x80012000 (all answered by the unclaimed-I/O default), BAT range 1
overlap in `nw_boot_contract.cpp`, DEC-pending clear on `mtdec`.

Trampoline reference points (Mac OS ROM file `0x26f2ca` bytes, relocated
copy at LA `0x1fee0000` ↔ file offset `X - 0x1fee0000 + 0xc264`; the
early copy runs at 0x200000): `HandleSpecialNode` table `0x197d0`,
strings `0x18ee3` "P99 ADB detected", `0x18655` "Initializing ADB
information", `0x186b9` "Virtual (USB-emulated) ADB"; flag merge into
the template at pc `0x206994`; 68k side `ffc0ab74` ORs the ROM
ProductInfo flags `c001bf00` into the template, `ffc01cf2` copies the
univ table (`a22e`), `ffc000c0..ffc00190` stores `$dd4/$dd8/$2408`.

Operator notes learned here: SheepShaver must run in the foreground of
the tool shell (`perl -e 'alarm N; exec @ARGV' -- …`; backgrounded
children are killed when the command returns). `perl alarm` does not stop
`qemu-system-ppc` (it ignores SIGALRM); use `(qemu … & pid=$!; sleep N;
kill -9 $pid)`. An orphaned QEMU holds an exclusive lock on the toast:
SheepShaver then logs `WARNING: Cannot open … (Resource temporarily
unavailable)`, `.AppleCD` gets a placeholder drive, and the ROM boots to
the "?" floppy — check for that line before suspecting a regression.
Never rebuild either scheme while a run is in progress (same DerivedData
products).

#### S4 step 7 — G4: install, boot the installed volume, host window (commits `dc56b3e9`, `46dddcf2`, `2e45d57f`, `24815925`)

Method: unattended runs. `NW_SCRIPT=<file>` (Debug builds only,
`nw_script.{h,cpp}`) schedules timed keyboard and mouse input into the
modelled ADB bus and takes frame-buffer snapshots and RAM dumps — the
counterpart of the golden capture's QMP driver, and only what a person at
the window could do (grammar in `nw_script.h`). Cursor moves are closed
loop on the guest's `Mouse` low-memory global, so click targets are
frame-buffer coordinates.

1. **Install** (real prefs: 512 MiB, `disk` = blank 2 GB hfv, `cdrom` =
   9.2.1 toast, `bootdriver -62`). Finder ≈ 40 s; the "disk is
   unreadable, initialize?" dialog ≈ 50 s (Initialize, name, erase
   Continue) — the guest wrote the HFS volume through `.Disk`. Typing
   `Mac OS Install` + Cmd+O in the Finder at 60 s, Welcome ≈ 95 s, then
   Continue / Select Destination (Macintosh HD, 317 MB) / Continue /
   Continue at (515,383), License Agree at (450,306), Start at (515,383)
   at ≈ 147 s. The install ran ≈ 17 min and ended with "The installation
   process has finished" (run 7, 47 snapshots).
2. **Host window.** The guest drew correctly but the SDL window stayed
   black: `SDL_RenderPresent` lives in `VideoVBL()`, reached only through
   the classic video driver's VBL (`NATIVE_VIDEO_VBL`) and Old World
   interrupt injection, both off on New World. `VideoHostPresent()` is the
   present half of `VideoVBL()`; `nw_host_tick()` (glue) calls it at 60 Hz
   from the CPU thread (the renderer's thread), from the same coarse tick
   as the device models. No guest state involved.
3. **Boot the installed volume** (`/tmp/prefs-hd` = real prefs with
   `bootdriver 0`; the user's prefs file is not edited). First attempt
   stopped at the start of "Starting Up" (progress bar ≈ 10 %): native
   user-mode code called through `MixedModeMagic` spun forever at RAM
   `0xaf49xx/0xaf66xx` after a write to PA `0x80008600` (Keylargo DBDMA
   channel 6, SCC-B transmit: `0x20002000` = FLUSH), polling a status bit.
   A RAM dump (`dump` script command) identified the PEF: imports
   `DriverServicesLib`, `NameRegistryLib`, `BlueAbstractionLayerLib`
   (`BALSerialOpen` … `LMGetSCCRd/Wr`), data strings `.AOut`, `.BOut`,
   `chrp,es2`, `chrp,es3` — the System's built-in SCC serial driver,
   opened at startup by the installed `Internal V.90 Modem` extension
   (port B). The CD's System never opened a serial port, so G3 did not
   see it. SheepShaver has no SCC; the tree no longer presents `escc` /
   `escc-legacy` (rule as for usb, ethernet), the Trampoline interrupt
   list drops the six escc sources and the ata-3 `AAPL,interrupt-index`
   values follow (vector == list position); pinned tail values in the
   harness updated (396 pass). Second attempt: "Mac OS 9.2 Starting Up"
   at 60 s, extensions load, Finder with the Mac OS Setup Assistant at
   ≈ 140 s (run 10).

Operator notes: `sleep 3` between consecutive SheepShaver runs (an
instance still tearing down holds the toast/hfv locks: `WARNING: Cannot
open … (Resource temporarily unavailable)`, then the boot has no disks).
`ResViewerCLI ls/resources/get` reads the hfv as well as the toast (paths
`Macintosh HD/System Folder/…`).

#### S4 step 8 — display driver and its VBL interrupt (commits `ba0ea33a`, `39a8cb87`)

Why: on the installed System the cursor never moved (run 13: the script's
closed-loop `mouse` gave up at 15,15) while keyboard input worked, and a
control run with the ROM's cofb ndrv moved it. The classic SheepShaver
driver had registered its VSL service but nothing ever serviced it: the
OS hangs the display's VBL tasks (cursor tracking among them) on the
driver's VSL interrupt, which on Old World is SheepShaver's host-side
`VideoVBL()` injection and on New World did not exist.

1. **Driver on the node.** `nw_build_video_driver()` (`rom_patches.cpp`)
   takes the `VideoDriverStub.i` PEF, replaces its `DoDriverIO` body
   (`lwz r2,0x2808; lwz r0,0x28d8; mtctr; bctr`, the Old World low-mem
   hook) with the NativeOp for `NATIVE_VIDEO_DO_DRIVER_IO` + `blr`, and
   `nw_bootinfo` puts it on the `display` node as
   `driver,AAPL,MacOS,PowerPC` (the parcel's `prop` flag adds cofb only
   when the node has none). `InitCallUniversalProc()` at
   `OP_INSTALL_DRIVERS` gives `FindLibSymbol` its CFM path. The CPU fetch
   guard admits SheepMem: `CallMacOS` returns through a trampoline there
   (run 11's machine check at `5058f1ec`). Boot screen, dialogs and the
   Finder then render through the SheepShaver driver.
2. **VBL as a display interrupt.** The display node gets `interrupts`
   `{0x1d, level}`, OpenPIC source 0x1d (uni-north's line for pci slot e
   in the mac99 layout, unused by the golden), 68k level 2, Trampoline
   list position 7 (`AAPL,interrupts`/`interrupt-index` 7). Device model
   (`nw_devices.cpp`): asserted every 1/60 s of timebase while the
   driver's VBL enable is on (`nw_display_vbl_enable`, the driver's
   `cscSetInterrupt`), cleared by the handler (`nw_display_vbl_clear`).
   The driver installs the handler the way a PCI ndrv does
   (`nw_install_vbl_handler`, `video.cpp`): `RegistryPropertyGet
   driver-ist` → `{set, member}`; `GetInterruptFunctions` for the
   member's enabler; `InstallInterruptFunctions` with a NativeOp thunk
   for `NATIVE_VIDEO_VBL` (in the system heap) that runs
   `VideoDriverVBL()` — clear the line, `VSLDoInterruptService` — and
   returns `kIsrIsComplete`; then the enabler (unmasks IVPR 0x1d).
3. **Two guest-side facts learned on the way**, both found by reading
   the ROM's 68k level dispatcher (`ffc0eb80`: autovector stubs →
   `a2 = *(0x68ffefd0)`, pending words `$28/$2c(a2)`, per-level mask
   table `$14(a2)` = ConfigInfo `+0xf40`, vector table at
   `ExpandMem+$210`, `$8c(a2,level)` = vector in service) from register
   dumps at the repeating emulator trap `6806e8d0` (kernel trap #4, the
   68k `RTE`):
   - ConfigInfo `+0xf40[level]` still held the golden masks (level 2 =
     `0x80540000`, positions 0/9/11/13 of the 15-entry list). With our
     list, `pending(7) & mask(2) == 0`: the dispatcher returned without
     acknowledging and the NK re-posted the interrupt forever. ATA would
     have met the same. Derived from `nw_trampoline_irqs` now
     (`ba0ea33a`).
   - Interrupt-set members are numbered per 68k level in list order
     (set for level 2: gpio1 = 1, ata = 2, 3, display = 4); the handler
     returns `kIsrIsComplete = 0` — a positive value names a child-set
     member and the first attempt (1) sent the Interrupt Manager after a
     child that did not exist (System Error box, no A-traps, DEC ticks
     only).

Result (run 33, 180 s, interpreter): Finder with the Control Strip at
≈ 170 s on the SheepShaver driver, handler at 60 Hz, closed-loop cursor
moves land (run 32: `mouse at 299,299`, click at 461,40 registered). The
−29208 "Apple Monitor Plugins" alert stays: it appears with the ROM's
cofb ndrv as well, so it is a Mac OS 9.x / SheepShaver-driver matter, not
a New World gate. Harness: VBL model (enable, half frame, frame, acknowledge,
level clear, re-arm), IVPR 0x1d, list and mask pins (411 pass).

Open after this step: the guest cursor is a hardware cursor (not drawn
into the frame buffer) driven by relative ADB motion, so the host pointer
and the guest cursor do not coincide in the window — a New World input
mode (software cursor + relative mouse, or absolute positioning through
the driver) is the next usability gate. Also XPRAM/NVRAM persistence
(the startup-disk choice), PMU restart/shutdown, Keylargo GPIO details,
BAT range 1 overlap.

#### S4 step 9 — New World input mode (commits `4b093935`, `7e761abe`, `c140f7d4`, `74976bda`)

Goal: what you see is where the guest clicks. Design: the driver reports
a software cursor (QuickDraw draws it into the frame buffer) and the SDL
front end feeds relative ADB motion from a grabbed mouse.

1. **Software cursor** (`video.cpp`): on New World
   `video_can_change_cursor()` is false regardless of `hardcursor` — the
   host pointer is not the Mac cursor there (nothing writes
   MTemp/RawMouse) — and `UseHardwareCursor()` follows it. `ROMType` is
   now identified at the end of `DecodeROM()` (`IdentifyROMType()`,
   `rom_patches.cpp`) because `VideoInit` runs before `PatchROM`.
2. **Relative grab** (`video_sdl2.cpp`, `utils_macosx.mm`): grabbed
   (SDL relative mode, host pointer hidden) whenever the window has
   focus; Ctrl+G releases (Ctrl+F5 too) and the release sticks until
   Ctrl+G or a click in the window, which captures and is swallowed;
   while released `SDL_MOUSEMOTION` is dropped; the title says "Ctrl+G to
   release". Operator scripts (`nw_script_active()`) keep the window out
   of it. macOS: `SetRelativeMouseMode` synthesizes a focus-lost (settle
   window + real focus check), the relative-mode hide is not drained by
   `SDL_ShowCursor` (`macosx_force_host_cursor`), and `NSWindow
   setTitle:` must run on the main thread (`macosx_set_window_title`).
3. **The gate that was really there.** With all of the above the cursor
   moved during boot and froze once the installed System had loaded its
   extensions (moves at 55 s and 68 s landed, 84 s and later never did;
   keyboard fine; CD boot unaffected). Bus trace: at ≈ 75 s the cursor
   device driver writes `Listen R3 [63 00]` to the mouse and reads
   register 3 back three times, three rounds 2.5 s apart. We answered
   `03 02` — flags clear, i.e. "exceptional event, SRQ disabled" — and
   the driver dropped the device; ≈ 500 mouse reports per move were then
   delivered and acknowledged (autopoll re-armed after each) but never
   reached MTemp. A real device answers register 3 with bit 6 set and
   bit 5 = SRQ enable, and handler `0x00` rewrites that bit
   (`BasiliskII/src/adb.cpp` does the same). Modelled in
   `nw_devices.cpp`; the probe no longer happens at all. QEMU's
   `adb-mouse` answers the bare address too, but the golden run boots the
   CD and never reaches that driver, so the golden could not show it.

Result (run 10, 200 s, interpreter, installed volume): software cursor,
move at 55 s → 202,202 in 16 steps; at 178 s (Finder) → 589,53 in 34
steps with the cursor drawn on the Macintosh HD icon; double-click at
186 s opens the Macintosh HD window. Harness 411 pass (the two Talk R3
expectations carry the flag bits). Removed on the way: a `cscSetInterrupt`
override ("keeping VBL") that never fired, and a composited hardware
cursor tried while chasing the freeze.

Open: PMU restart/shutdown, Keylargo GPIO details, BAT range 1 overlap.
XPRAM/NVRAM persistence is done (NVRAM flash model, addendum below). A
750 PVR (`NW_PVR=00080202`) is documented, not the default — see below.
The Apple Monitor Plugins dialog is not a gate; see the addendum.

Found while using it (commit `350e1e4b`): a highlighted menu item's text
came out speckled black. The frame buffer already held those pixels
(`shot` during the highlight), with or without `gfxaccel`, so the guest
computed them: partial-coverage pixels were exactly `highlight ×
(1 − c)` and full-coverage ones black, i.e. the antialiasing blend's
`fg × c` term was zero. An AltiVec instruction histogram over the
highlight put the work in System code at `0x3d39xx–0x3d3e10`; a `dump`
of that range disassembled (`llvm-mc -triple=powerpc`) shows the blend
unpacking the foreground colour with `vmrghb v17,v0,v17` — destination
aliased with a source. kpx_cpu's merge (and pack, unpack,
`vsldoi`/`vslo`/`vsro`, `vsl`/`vsr`, `vperm`, the sums) wrote `vD` element
by element while reading the inputs by reference, so the register was
zeroed. Old World SheepShaver never ran 9.1+, whose QuickDraw uses
AltiVec here. Inputs are copied now.

**750 PVR (`0x00080202`).** The T-sample loop at `5032571c` / `503259dc` /
`50326660` is not 750 L2CR init. The NanoKernel debugger-wait at
`50326420` (`crset 6`) heartbeats `lwz`/`addi`/`stw` at 0 then `bl 503259c0`
(serial get-char); with no UART that returns −1 and `b 5032665c` forever.
Entry is `50312960 b 50326420` from `503146ac` (unhandled exception, panic
code 2). The fault is a program exception at `6806e8fc`: `twi 31,r31,15`
(`0x0fff000f`), NanoKernel KCall 15 = SystemCrash (`Defines.s`). The 68k
emulator reached that from `6806e2a0`, which does `lwz r6,232(r31)` /
`lwz r7,236(r31)` / `andc` / `cntlzw` / `cmplwi 32` / `bt cr7, trap15`.
`r31` is EDP `68fff000`. On a 7400 those words are `+232=fe000000`
`+236=0` (seven free slots); on a 750 they stay 0, `cntlzw` of 0 is 32,
crash. The 7400 store is 68k ROM at `ffc01198` `move.l #$fe000000,$e8(a2)`
after `_Gestalt('ppcf')` bit 4 (`gestaltPowerPCHasVectorInstructions`) and
`_HWPriv($2000)` succeed (`r24=ffc011a0` on the 7400 run). A 750 has no
AltiVec, so that path is skipped — correct for the Gestalt bit. HID0 and
L2CR are 0 at first FPU-unavail on both CPUs; the 750 L2I/L2IP poll at
`5031954c` (`mfspr 1017` / `rlwinm. r8,r8,31,0,0` / `bne .-8`, wait while
L2IP set) never ran before the crash, and ignored L2CR writes would read
L2IP=0 and not spin. Default PVR stays 7400 (QEMU golden; 9.2.1 QuickDraw
AltiVec). `NW_PVR` in `main_unix.cpp` overrides it for experiments; every
run logs `NW-BOOT G1: PVR xxxxxxxx (default|NW_PVR)` so a result names
its CPU, and a non-hex value is ignored rather than becoming PVR 0.

**Apple Monitor Plugins / −29208.** Two 200 s installed-volume runs
(`/tmp/g6/i2.log`, `/tmp/g6/run.log`) with `every 10 shot` through t=190
never drew the dialog: shots from Welcome through Finder (`shot-000`…
`shot-019`) and the Apple-menu grab at 186 s (`menu.ppm`) show the
desktop only. Mouse landed (`t=55.5 mouse at 202,202`, `t=178.7 mouse
at 10,6`). Highlight check on `menu.ppm` (x 48..124, y 80..93): 36
pixels with a channel below (51,51,153), all near-black in bbox
90,86–97,93 — the cursor at the script target, not speckled text.

The dialog text, when it is shown, is the extension's own STR# 101
(`ResViewerCLI get` on `Macintosh HD/System Folder/Extensions/Apple
Monitor Plugins`): DLOG/DITL 21000 (`^0^1`, OK) with string [1]
"Apple Monitor Plugins did not load completely.\r\rError:" and the
OSErr appended. Other STR# 101 entries name memory, resource,
component, file, system-too-old, **monitor not supported**, prefs,
**communicating with the display**, missing Display Enabler — no
string contains the digits 29208. The INIT PEF hardcodes `li r31,
-29210` (GetResource `'gnht',100` = RemoraManager failed) and
`li r31, -30472`; it does not contain −29208, and neither does the
data-fork PEF (no `addi` immediate, no 16- or 32-bit constant). The
INIT's own codes sit in the same −29xxx range, so −29208 is most likely
a Remora-internal code computed or held in a handler, not a Display
Manager OSErr: Displays.h in SuperMario and Carbon names
`kDMDriverNotDisplayMgrAwareErr` as **−6228**. The number is whatever
OSErr the INIT/handlers pass to string [1]; this session did not catch
the dialog on screen to read it.

TEMP `VideoStatus`/`VideoControl` log (`NW-BOOT VIDEO`, removed before
commit) on the second run, every selector except VBL-rate
`cscGetInterrupt` / `cscGetHardwareCursorDrawState`:

| t | selector | result |
|---|---|---|
| 0 | control 70 | controlErr (−17) |
| 0, 11, 89 | status 28 `cscGetMultiConnect` | statusErr (−18) |
| 21 | status 20 `cscGetGammaInfoList` | statusErr |
| 51, 89 | status 30 `cscGetTimingRanges` | statusErr |
| 89 | status 32 `cscGetCommunicationInfo` | statusErr |
| 130 | status 43 (not in Video.h through 32) | statusErr |

Answered with `noErr`: `cscGetGamma` (8), `cscGetCurMode` (10),
`cscGetConnection` (12, nine times), `cscGetModeTiming` (13),
`cscGetPreferredConfiguration` (16), `cscGetVideoParameters` (18).
`cscGetDDCBlock` (27) was **never called**. `cscGetConnection` reports
`kMultiModeCRT3Connect`, sense 6/`0x23`, flags `kAllModesValid|
kAllModesSafe` — a fake 21″ CRT, no `kHasDDCConnection`. The extension
has gnht 180 "iMac Device Component" and DisplayIIC/ADB/USB handlers;
`cscGetCommunicationInfo` is the I2C/DDC probe (`VDCommunicationInfoRec`,
`kVideoBusI2C`). We have no I2C bus and no EDID / `AAPL,ddc` property
on the `display` node; the golden's `QEMU,VGA@e` node has no such
property either (PCI ids, model, compatible, reg, assigned-addresses,
width/height/depth/linebytes, driver). The same dialog was already
observed with the ROM cofb ndrv (S4 step 8), so it is not a wrong
`cscGetModeTiming` on our driver.

Most likely cause: Apple Monitor Plugins is looking for an Apple panel
it can talk to (DDC/I2C, ADB, or USB). SheepShaver's display is a linear
frame buffer with no DDC data by design. Not a property we omit that
QEMU has, and not a selector we should fake. Leave the extension on the
volume; do not suppress the alert.

Open: S4 step 7 (cofb) and S4 step 8 (our ndrv; run 33's script sent a
Return at 150 s just to dismiss it) saw the dialog on this same volume;
the two runs above did not, with no SheepShaver-side video change in
between other than the S4 step 9 software cursor. It is therefore
timing-dependent or otherwise not deterministic, and the paragraph
above is an inference from the selector log, not from a caught
failure. When it next shows, read the OSErr off the shot and match it
against the `NW-BOOT VIDEO` probe before deciding anything.

**XPRAM/NVRAM persistence (`nw_nvram.{h,cpp}`).** Where PRAM goes on
this ROM, from two probe runs (TEMP counters in `EmulOp` and on every
`nw_io` access ≥ `0xff000000`, removed): `_WriteXPRam` (`a052`) is
called 74 times per boot, SheepShaver's classic XPRAM/NVRAM `EMUL_OP`s
fire **0** times (`/tmp/g6/nv1.log`), and `~/.sheepshaver_nvram` is never
written. Only `nvram1` of the classic New World patch set matches this
ROM (at 0x7510; `nvram2`–`7` patterns are absent, hence `patch_68k
incomplete`), and that routine is unreachable: the live ProductInfo copy
(`UnivInfoPtr` = 0x45e00, `/tmp/g6/nv2-ram0.bin`) has `ClockPRAMPtr`
(+0x44) → ROM 0x7824, a table whose entries are `jmp (a6)` / `moveq
#0,d0; rts` / `rts` (0x7864–0x7868); the OS trap table has `a051` →
ROM `ffc08102`, `a052` → System code at `0012b0d0`. The PRAM bytes end
up in the RAM shadow of the ROM's `nvram,flash` ndrv by a route not
traced here.

That ndrv is the whole hardware contract (parcel PEF, code base 0x57f10
in RAM, `/tmp/g6/nvram-ndrv.dis`). It never reads the tree's `reg`:
`addis r4,r3,-256` puts the flash at **0xff000000** and the two 8 KiB
banks at +0x4000/+0x6000 (`nvram@fff04000` is the same chip — the
bridge decodes the 16 MiB ROM window onto the 1 MiB boot flash, 16
aliases). A bank is valid when byte 0 = 0x5a, the CHRP checksum of the
16-byte header (byte 1 taken as 0, 8-bit end-around carry) matches byte
1, and adler32 (mod 65521, seed 1) of [0x14..0x2000) matches +0x10; the
value is the generation at +0x14 (+0x1f4/+0x20c). Init (+0x2b8) picks
the higher generation, ties to A, copies 8192 bytes. Commit (+0x6e8):
if adler of the shadow equals the stored one, nothing; else generation
+1, new adler, erase the *other* bank (+0x540: 0x20, 0xd0 at its first
byte, poll, 0xff at 0xff004000, verify all 0xff), program every byte
(+0x614: 0x40 then the byte, poll each, 0xff, verify), swap. The poll
(+0x444) reads 0xff004000 for up to 5 s: 0x80 done, 0x38 error (−604),
timeout −2415. Intel/Sharp boot-block flash command set, 8 KiB
parameter blocks exactly where the two banks sit. Before the model:
`/tmp/g6/nv2.log` 3425153 — Special → Restart wrote `20` and `d0` at
`ff006000`, then 356 K reads of `ff004000` from t=180 to 185
(`pc 0005839c` = +0x444+0x48), no program cycle: the erase timed out.

Above the chip: the Trampoline (Mac OS ROM file 0x6300–0x6620, strings
0x15c98–0x15f2b) walks the 16-byte CHRP partition headers of the bank
OF hands it, looks for sig 0xa0 `APL,MacOS75`, and if absent carves one
of 0x50 units out of the first 0x7f free partition of ≥ 0x51 units,
writing through OF; failing that it tells Mac OS the constant 0x1400.
`nw_nvram_init` does the same on the current bank. With no image file
it presents what a fresh machine has: bank A generation 1 with the 0x5a
header (name `nvram` — chosen, not observed on hardware), `common`
(0x20, 0x13d units, empty = OF defaults), `APL,MacOS75` at 0x13f0
(data 0x1400, 0x50 units), free 0x7f (0x71 units); bank B erased. The
model claims 0x4000 bytes at 0xff004000 and the 15 other aliases
(`NW_IO_MAX_DEVICES` 16 → 32); state machine as above, erase/program
complete at once, status read at any address in command mode, program
only clears bits, a bad sequence sets 0x30 until 0x50. The image (both
banks, 16 KiB) is written to `<XPRAM file>.flash` (`XPRAMFilePath()`,
new in `xpram_unix.cpp`; `~/.sheepshaver_nvram.flash` here) after each
completed sequence (the driver's 0xff) and at exit; runs killed by the
200 s alarm keep their commit.

Result, four runs on one image file (`/tmp/g6/nv3.log`…`nv6.log`,
`HOME=/tmp/g6/home` so the file is `/tmp/g6/home/.sheepshaver_nvram.flash`):
nv3 fresh — `nvram flash …: bank A gen 1, bank B gen 0, APL,MacOS75
data at 0x1400`; Special → Restart → `erase bank B`, then the program
cycle byte by byte (`W1 ff006000 40`, `W1 ff006000 5a`, `W1 ff006001
40`, `W1 ff006001 82`, …) → `bank B programmed 8192 bytes, generation
2`; the file's bank B carries `NuMc` at 0x140c and a PRAM image at
0x1400. nv4 — `bank A gen 1, bank B gen 2`; the ndrv checks both banks
and its copy loop (`pc 0005825c`) now reads `ff006000…` (bank B). nv5 — Mouse control panel,
tracking slider dragged to Very Slow (`nv5-mouse-veryslow.png`), close,
Restart → `erase bank A` … `bank A programmed 8192 bytes, generation 3`;
XPRAM byte 0x08 changed 0x1b → 0x03, nothing else. nv6, a fresh boot —
`bank A gen 3, bank B gen 2` and the Mouse panel opens at Very Slow
(`nv6-mouse-panel.png`). Harness 444 pass (33 new: adler32 vector,
fresh layout, aliases, erase/program/status sequence, file round trip,
Trampoline carve). No `IO page ff004000 first` line any more.

Not done: the restart itself — after the commit the guest blanks the
screen and waits for the PMU (the open PMU restart/shutdown item), so
the proof used a new run per boot. The `common` partition is empty;
Startup Disk's OF `boot-device` write into it is not exercised. The
dead `nvram1` patch is left as is.

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
- QEMU 11.1.1, `newworldview`, resviewer, the local ROM/toast, and
  `~/nw-golden` dumps: see **Tools and files (operator reference)**.
- `ROM-milling-brief.md`, `OS921-BOOT-PLAN.md` as history and as the
  WP0–WP6 architecture. This file governs sequencing.

## Tools and files (operator reference)

Live paths on the operator host. ROM, toast, and golden capture bytes never
land in git. Recapture a QEMU dump only when a gate needs a new slice.

### Guest images

| what | path | notes |
|---|---|---|
| Mac OS ROM (CHRP / tbxi) | `~/Downloads/Mac OS ROM` | 2.4 MB; 9.2.1 New World |
| Mac OS 9.2.1 CD | `~/Downloads/Mac OS 9.2.1 copy.toast` | 641 MB toast / ISO |

### QEMU (golden gatherer)

Homebrew QEMU 11.1.1. Binary: `/opt/homebrew/bin/qemu-system-ppc`.

Typical machine: `-M mac99,via=pmu -m 512 -cpu g4`, toast as IDE CD.

Plugins and drivers (in-tree, under `research-score/golden/`):

- `nwgolden.c` / `libnwgolden.dylib`: exception and A-line event stream
- `nwdump.c` / `libnwdump.dylib`: NK-entry and hardware-info dumps
- `capture.py`, `ofwalk.py`, `qmp.py`

Golden captures (never in git):

| dir | useful files |
|---|---|
| `~/nw-golden/run1/` | `events.txt`, `phases.json`, `markers.tsv` |
| `~/nw-golden/dump2/` | `nkentry-r9.bin`, `hwinfo-hnfo.bin`, `nkentry-r5.bin`, `hwinfo-kdp.bin`, `hwinfo-configinfo-pa.bin`, `hwinfo-lowmem-pa.bin`, `hwinfo-bootinfo-pa.bin` |
| `research-score/golden/` | `devtree-mac99.txt` (checked in) |

Default pack source is `~/nw-golden`. Do not recapture `run1` or `dump2`
unless the next gate needs a register or Hnfo slice that is not already there.

### newworldview

Sibling repo: `~/Documents/GitHub/newworldview`.
CLI: `NewWorldViewCLI`. S3 sources are also mirrored under
`research-score/golden/newworldview/`.

Live on this branch:

- `decode-rom`
- `trap-table` (example: `A9F2` is `_Launch` at ROM+`0x02C010`)
- `drivers`
- `diff` / `golden-stats` / `golden-atraps`
- `lookup`
- `toast-ls` / `toast-get`

Unused on this branch (mill lane): `analyze-logs`, `build-grok-pack`,
`build-grok-pack-from-log`, `export-histogram`, `export-pipeline`,
`export-annotations`.

### resviewer

Sibling repo: `~/Documents/GitHub/resviewer`.
CLI: `ResViewerCLI`. Resource and PEF inspection of the toast (System file,
installer PEF, DITL, CODE). No changes needed in that repo for this branch.

Live: `ls`, `find`, `find-imm`, `resources`, `get`, `derez`.

### How they are used on a gate

1. Name the first divergence with `NewWorldViewCLI diff` against
   `~/nw-golden/run1/events.txt`.
2. If the probe is ROM or OF: `decode-rom`, `trap-table`, `drivers`, plus
   `dump2` / a new `nwdump` slice.
3. If the probe is a guest file or PEF: `ResViewerCLI` / `toast-ls` /
   `toast-get` on the 9.2.1 toast.
4. Land one SheepShaver-side fix. Re-diff. The first divergence must move
   later.

## Non-negotiables carried over

- ROM, toast and golden capture bytes never land in git.
- (Removed: the 9.0.4 / ROM 1.6 regression requirement. 9.0.4 is not a goal
  of this branch and is not a gate for New World changes.)
- "Fix the gate" over "skip the gate". If a change's only justification
  is that the guest gets further, it is a skip; do not land it.
