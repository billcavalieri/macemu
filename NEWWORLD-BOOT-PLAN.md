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
