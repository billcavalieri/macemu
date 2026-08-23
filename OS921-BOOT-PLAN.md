# Mac OS 9.2.1 boot plan (SheepShaver, Apple Silicon)

**Target:** Mac OS 9.2.1 reaches the Finder on SheepShaver built from this tree, running on macOS 26+ and M-series ARM, with the CPU path being an ARM64 JIT.

**Tree:** `billcavalieri/macemu`. Work stays on this fork. Branches already cut: `arm64-jit`, `video-damage`, `memory-subsystem`. Donor `rcarmo/macemu-jit` is the `jit-ref` remote only.

**ROM path:** `billcavalieri/tome-extract` already unwraps installer tomes to `Mac OS ROM` (tbxi). Basilisk II is out of scope. SheepShaver consumes that ROM.

**Build:** Xcode, `SheepShaver/src/MacOSX`. Not autotools.

**Reference (read, do not vendor):** Ken Hawkins NewSheep — New World + nanokernel v2 bring-up that reached a 9.2.1 desktop. Lift invariants and exception order from it. Implement them here.

Grok Build executes this plan in the order below. Each work package has a done test.

---

## Why 9.2.1 is a different machine than 9.0.4

SheepShaver today is an Old World guest: patched ROM (`SheepShaver/src/rom_patches.cpp`, `rsrc_patches.cpp`), `emul_op` traps (`emul_op.cpp`, `thunks.cpp`), Name Registry (`name_registry.cpp`), and a CPU that treats guest RAM as a flat host mapping (`SheepShaver/src/kpx_cpu`, `emul_ppc`). Mac OS 9.0.4 is the last release that boots in that model.

9.2.1 is New World:

- Boot payload is a New World `Mac OS ROM` (tbxi), not a 4 MiB Old World dump.
- The nanokernel is v2. It programs SDR1, segment registers, and BATs, and expects DSI/ISI.
- Virtual memory is mandatory. A CPU that ignores `MSR[DR]/MSR[IR]` will die in the first page-in.

So the product is a New World SheepShaver: real PPC32 MMU, New World boot, then an ARM64 JIT that uses that MMU. Video damage and the memory-bank fast path are how it stays fast on M-series after it boots.

---

## Architecture

```
macOS 26  FSKit/Xcode app
    |
    v
SheepShaver (this tree)
    ├── New World boot   rom_patches.cpp, name_registry.cpp, MacOSX prefs
    ├── PPC32 MMU        kpx_cpu (HTAB + BAT + SR)  ← guest truth
    ├── ARM64 JIT        arm64-jit  ← host speed, walks the same TLB
    ├── Memory banks     memory-subsystem  ← RAM/ROM/IO decode
    └── Video damage     video-damage, video.cpp  ├─ present only dirty tiles
```

One guest physical address space. The MMU is the only translator from effective address to that space. The JIT never invents a second map.

---

## Work packages

### WP0 — ROM and install set (tome-extract)

**Owner:** Swift / tome-extract. No C++ in macemu.

1. Extract a New World ROM that 9.2.1 will accept. Preferred: ROM Update 1.0 → Mac OS ROM 1.6 (or the matching 9.2.1 ROM from the 9.2.1 install set). iMac Update 1.1 → ROM 1.2.1 is the 9.0.4 path; keep it as a regression ROM only.
2. Verify with `strings "Mac OS ROM" | grep "Boot "` and a size/type check (`tbxi`). Do not commit the ROM.
3. Produce a bootable HFS system volume: 9.2.1 retail/install, then a blank HFS image that the installer can target. The `.toast` installer CD is not itself a bootable system disk.
4. Check-in a `prefs` template (ROM path, disk path, ramsize, jit on) under `SheepShaver/src/MacOSX` as a documented local file, not a binary.

**Done:** one command line (or Xcode scheme env) points SheepShaver at a New World ROM + 9.2.1 installer + blank HFS. ROM never lands in git.

### WP1 — New World boot and nanokernel v2

**Owner:** Macemu Engineer. Files: `rom_patches.cpp`, `rsrc_patches.cpp`, `name_registry.cpp`, `macos_util.cpp`, `SheepShaver/src/include`, `MacOSX` prefs.

1. Detect New World vs Old World at load. Keep the Old World 9.0.4 path working; gate New World on ROM signature / Nanokernel version.
2. Stop applying Old World-only patches to a New World ROM. New World uses a different trampoline (tbxi + Open Firmware / BootX-style handoff, not the Old World reset vector patch set).
3. Present a Name Registry and device tree 9.2.1 will accept: uni-n / PCI / video / ADB / CUDA-or-PMU / NVRAM / RAM. Start from `name_registry.cpp` and the NewSheep device-tree choices, then match what 9.2.1’s nanokernel probes. Root `compatible` is `MacRISC2`. Gestalt machine ID is always 406; the real identity is root `model` / `compatible`. Required nodes: `/memory` (reg banks), `/cpus`, `/chosen`.
4. Nanokernel v2 exception vectors and timebase. DEC, decrementer, and `mftb` must be consistent. NK v2 lives at ~`ROMBase+0x310000`.
5. XPRAM / NVRAM shape for New World (`xpram.cpp`).
6. Trampoline: `saveKernelDataPtr` immediately after `saveReturnAddr`. KDP `Hnfo` comes from `HardwareInit` and skips the CPU probe / `mfsdr1`. `BATRangeInit` at KDP+0x2cc (32 longs).

**Done:** Device tree root `compatible` is `MacRISC2`, Gestalt machine ID is 406, and `/memory`, `/cpus`, `/chosen` exist. HTAB half of the gate is **either** `mtsdr1` in the SPR log **or** a valid HTAB already in the Hnfo block — plus `BATRangeInit` at KDP+0x2cc. `saveKernelDataPtr` is immediately after `saveReturnAddr`. Do **not** require `mfsdr1`. Old World 9.0.4 still boots.

### WP2 — PPC32 MMU (the boot gate)

**Owner:** Macemu Engineer. Files: `SheepShaver/src/kpx_cpu/**`, `emul_ppc/**`, `sheepshaver_glue.cpp`.

Implement a real 32-bit PowerPC MMU:

| Piece | Behavior |
|---|---|
| `MSR[IR]`, `MSR[DR]` | Instruction and data translate independently |
| BAT (IBAT/DBAT 0–3) | Hit before HTAB; respect Vs/Vp, WIMG, PP |
| Segment registers | 16 SRs → VSID for hashed page table |
| SDR1 | HTABORG + HTABMASK |
| PTE search | Primary then secondary hash; `_PTE.V`, API, R/C bits |
| DSI / ISI | `DSISR`, `DAR`, `SRR0`/`SRR1` exact. 9.2.1 page-in depends on this |
| `tlbie` / `tlbia` / `tlbsync` | Invalidate the host TLB cache |
| Protection | PP + key; write faults set DSISR correctly |

Software TLB in front of the walk (page-granule, ASIDs or VSID+page). Every JIT block and interpreter fetch uses `mmu.translate(ea, ir/dr, width)`.

No identity map. No patching the nanokernel to skip VM.

**Done:** Host-side Xcode target `SheepShaver-MMUTests` (Debug, `ARCHS=arm64`) programs BAT + a synthetic HTAB and proves BAT hit, HTAB hit, fault, IR-only vs DR-only, and `tlbie` drops the cached translation. `mmu.translate(ea, ir/dr, width)` is the only map. Then on the guest: first DSI follows elliotnunn/NanoKernel `HotInts.s` `DataStorageInt` (not a DSISR-only handler) and retries without flooding the same `DAR`. Do **not** require `mfsdr1`. Just-enough PPC32 MMU. Do not grow a chipset.

### WP3 — ARM64 JIT on the MMU

**Owner:** Macemu Engineer. Branch `arm64-jit`. Donor: `jit-ref` (rcarmo) for emit patterns only.

1. Port the Basilisk II AArch64 JIT approach onto SheepShaver’s PPC front end (`kpx_cpu`), not a second CPU core.
2. Translation cache keys: `(phys_page, guest_pc, msr_ir, endian)`. Invalidate on `tlbie`, code-modify, and WIM change.
3. W^X: JIT buffer is `MAP_JIT` / `pthread_jit_write_protect_np` on Apple Silicon. Guest executable pages are never host-writable at the same time.
4. Fast path: TLB hit inlined as an ARM load from a 64-bit host pointer; miss calls the C walker.
5. Interpreter remains the source of truth. Equivalence tests: block of PPC ops, JIT vs interpreter, same GPR/CR/XER/FPSCR and same MMU side effects (R/C bits).

**Done:** nanokernel idle loop faster than interpreter by a measured factor on an M-series Mac. 9.2.1 still page-faults correctly with JIT on.

### WP4 — Memory banks

**Owner:** Macemu Engineer. Branch `memory-subsystem`.

Guest physical space is banks: RAM, ROM (Mac OS ROM + overlays), PCI/IO, video, NVRAM. Decode once. RAM and ROM hits are pointer math; IO hits a trap table. The MMU outputs a physical address; banks resolve it.

**Done:** A bank map printed at boot. ROM execute-only. Video bank marked for damage tracking. No functional change to 9.0.4 first; then New World ROM is a bank, not a memcpy into RAM.

### WP5 — Video damage

**Owner:** Macemu Engineer. Branch `video-damage`. Files: `video.cpp`, `gfxaccel.cpp`, MacOSX framebuffer.

9.2.1 QuickDraw writes the framebuffer through the MMU. Present only dirty tiles. Promote `update_display_static_bbox`; do not invent a second tracker.

**Done:** Idle Finder does not upload a full frame every vsync.

### WP6 — Bring-up sequence

| Gate | What you see |
|---|---|
| G0 | After DecodeROM, 4 MiB at `ROMBase`. NewWorld signature at `ROMBase+0x30d064`. Nanokernel v2 at `ROMBase+0x310000`. Both ROM 1.6 (`lzss-offset`, tome-extract) and the 9.2.1 install-set Mac OS ROM tbxi (`parcels-offset` / `parcels-size` → `prcl`) pass. iMac Update 1.1 / ROM 1.2.1 is 9.0.4 regression only. Old World 9.0.4 still boots. |
| G1 | Root `compatible` is `MacRISC2`; Gestalt machine ID is always 406; real ID is root `model`/`compatible`. `/memory` (reg banks), `/cpus`, `/chosen` present. NK v2 ~`ROMBase+0x310000`. HTAB: `mtsdr1` in the SPR log **or** a valid HTAB already in the Hnfo block. `BATRangeInit` at KDP+0x2cc (32 longs). `saveKernelDataPtr` immediately after `saveReturnAddr`. Do **not** require `mfsdr1`. |
| G2 | First DSI follows elliotnunn/NanoKernel `HotInts.s` `DataStorageInt`: SPRG0=KDP, SPRG1=saved r1, SPRG2=LR, SPRG3=VecTbl; save r0/r1 then `mfsrr0`→r10, `mfsrr1`→r11, SPRG2→r12, CR→r13; then MSR[DR] ON, `lwz` faulting insn at SRR0 into r27, go to MemRetry. `AlignmentInt` is the handler that does `mfdsisr`/`mfdar`. Do not treat first DSI as DSISR-only. No same-`DAR` flood. Just-enough PPC32 MMU. Do not grow a chipset. |
| G3 | 9.2.1 installer window (mouse + video) |
| G4 | Installer writes the HFS target; reboot from that volume |
| G5 | Finder desktop, menu bar, about box says 9.2.1 |
| G6 | JIT on; measurements recorded |

If a gate fails, fix that gate.

## Locked G0–G2 (Grok Build)

These three gates are locked. Do not invent extra probes — in particular **do not require `mfsdr1`**.

**G0 (WP0 / Swiftly).** `DecodeROM` in `SheepShaver/src/rom_patches.cpp` already takes the tbxi path (`lzss-offset` **or** `parcels-offset` / `parcels-size` with a `prcl` signature). After decode there is 4 MiB at `ROMBase`. Check NewWorld at `ROMBase+0x30d064` and nanokernel v2 at `ROMBase+0x310000`. Both ROM 1.6 (lzss, tome-extract) and the 9.2.1 install-set Mac OS ROM (parcels → prcl) must pass. iMac Update 1.1 / ROM 1.2.1 stays 9.0.4 regression only.

**G1 (WP1).** Device tree root `compatible` is `MacRISC2`. Gestalt machine ID is always 406; the real machine identity is root `model`/`compatible`. Need `/memory` (reg banks), `/cpus`, `/chosen`. NK v2 ~`ROMBase+0x310000`. KDP Hnfo from `HardwareInit` skips the CPU probe / `mfsdr1`. Treat **either** `mtsdr1` in the SPR log **or** a valid HTAB already in the Hnfo block as the HTAB half of the gate. Plus `BATRangeInit` at KDP+0x2cc (32 longs). `saveKernelDataPtr` immediately after `saveReturnAddr`.

**G2 (WP2).** First DSI is `DataStorageInt` in elliotnunn/NanoKernel `HotInts.s`, not a DSISR-only path (`AlignmentInt` is the one that does `mfdsisr`/`mfdar`). Sequence: SPRG0=KDP, SPRG1=saved r1, SPRG2=LR, SPRG3=VecTbl; save r0/r1 then `mfsrr0`→r10, `mfsrr1`→r11, SPRG2→r12, CR→r13; MSR[DR] ON; `lwz` the faulting insn at SRR0 into r27; go to MemRetry. Host harness: `SheepShaver-MMUTests` (`SheepShaver/src/kpx_cpu/tests/mmu_harness.cpp` + `ppc-mmu.*`). Just-enough PPC32 MMU. Do not grow a chipset.

## Xcode

Scheme `SheepShaver` (MacOSX target). `SheepShaver-MMUTests` is a native Debug tool target on `SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj` for WP2 (`ARCHS=arm64`). `MACOSX_DEPLOYMENT_TARGET=26.0`, JIT entitlement `com.apple.security.cs.allow-jit`. Debug = interpreter + MMU logs. Release = JIT + damage. No new Unix configure path.

## Measurement

Same Mac, same RAM, same ROM: time to Finder, PPC ops/s, tiles/s, JIT flush/s, DSI/s after idle. Fill after G6.

## Out of scope

Basilisk II / 68k. A third emulator tree. Merging NewSheep as a subtree (read it; reimplement here). Committing ROMs, .smi, .toast, or disk images.

## Grok Build order

1. WP0 ROM + images (tome-extract).
2. WP2 MMU harness (can start before WP1 finishes).
3. WP1 New World patches until G1.
4. WP2 guest hooked until G2.
5. G3–G5 on interpreter.
6. WP4 banks, then WP3 JIT, then WP5 damage, then G6.

One PR per gate when possible. Commit messages name the gate.
