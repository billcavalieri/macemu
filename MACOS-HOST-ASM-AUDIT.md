# macOS host C vs ARM64, and NW JIT ranking

**Tree:** `billcavalieri/macemu` branch `newworld-boot` @ `1204ba0d` (this audit commit sits on top).  
**Build:** `SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj`, Release `-O3`, `arm64`, `USE_SDL_VIDEO` + `USE_SDL_AUDIO`, `ENABLE_VOSF` off.

Two questions, kept apart:

| | Question | One-line answer |
|---|---|---|
| **A. Host machine code** | Would host macOS C/C++ (video upload, blit, SheepBlaster ring, timer) gain from hand ARM64 / NEON / Accelerate vs clang `-O3`? | **No.** Do not write host `.S`. |
| **B. Better JIT** | Would the guest PPC → ARM64 JIT (`nw_jit.cpp`) move Finder / QuickTime wall-time? | **Yes, if at all — here.** Highest measured lever is still-disabled ARM FMA (`fast = 0` in `emit_fp_inline`), not host blit asm. |

`NW-JIT-PERF-ANALYSIS.md` and `PERF-DEEP-PASS.md` are **not on this tip**. Evidence is `nw_jit.cpp` / `nw_jit.h`, `ppc-cpu.cpp` `nw_jit_try`, `SHEEPBLASTER-SOUND.md`, `JIT_OPS.md` (2026-09-21), `NW-JIT-MILL.md` (2026-09-15, **stale** vs later mill/cache/DTLB commits).

---

# A. Host macOS C vs hand-written ARM64 / NEON

**Answer:** no. The FPS and audio stories on this tree are guest JIT / MMU / vsync-present, not host blit loops.

## A. Yes / no by area

| Area | Hand ARM64 / NEON vs clang? | Why |
|---|---|---|
| Dirty-rect video upload (`memcpy` into `SDL_LockTexture`) | **No. Wash, then lose.** | Apple `memcpy` is already NEON/AMX. Cost is lock + Metal upload + `SDL_RenderPresent`, not the copy. |
| Shadow `memcmp` vs `the_buffer_copy` | **No. Wash.** | Apple `memcmp` is SIMD. The win was *skipping* unchanged QT tiles, already in C. |
| Metal/SDL present of the retained texture | **No. Not CPU.** | Comments in `present_sdl_video` already say Metal presents the whole texture and vsync-per-strip was the 4 fps bug. GPU, not asm. |
| `nqd_host_scale` nearest-neighbor stretch | **Not hand asm.** C loses to Accelerate; wall-time still small. | Scalar gather-scale clang will not vectorize. `vImageScale` could beat this C. Hand NEON would lose to vImage and would not move QT FPS. |
| SheepBlaster ring pull / resample | **No. Lose.** | 44.1 kHz stereo is ~176 KB/s. Guest mixer/decode owns the time (`SHEEPBLASTER-SOUND.md`). |
| NQD fill/invert/bitblt (`gfxaccel.cpp`) | **No. Wash.** | Duff’s-device 64-bit loops. clang `-O3` or libc `memset`/`memmove` already win. Duff can *hurt* auto-vectorization; the fix is simpler C, not asm. |
| 16-bit `Screen_blit` (`video_blit.cpp`) | **No for NW 32-bit.** | NW OS 9.2.1 path sets `blit` only at 16-bit; 32-bit aliases `the_buffer` and copies raw. |
| Timer (`timer_unix.cpp`) | **No.** | Mach clock syscalls. Not a compute kernel. |
| Prefs / clip / etherhelper / OF / name registry / boot glue | **No. Cold.** | Out of scope even if they copied pixels. |
| Guest PPC → ARM64 JIT (`nw_jit.cpp`) | **Not host C.** See **B**. | Already guest machine code. Remaining cost is helper *calls* and block cuts, not a host blit kernel. |

## A.1 Hot execution paths (macOS Apple Silicon build)

Call chain that actually runs while OS 9.2.1 is up:

```
ppc-cpu.cpp tick_decrementer
  → nw_host_tick()                 SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp
      → VideoHostPresent()         SheepShaver/src/SDL/video_sdl2.cpp
          → SDL_PumpEvents / handle_events
          → nw_fb_commit()         SheepShaver/src/nw_io.cpp
          → video_refresh()        → update_display_static_bbox()
          → present_sdl_video()    → nw_upload_base() / row memcpy
                                   → nw_present_texture()
                                   → nw_present_movie()
                                   → SDL_RenderPresent()
```

**Video (60 Hz, CPU thread, same thread as the JIT).** New World does *not* refresh from the SDL redraw thread (`do_video_refresh` returns immediately). Host present steals guest time.

| Function | File | What it does on the hot path |
|---|---|---|
| `VideoHostPresent` | `SheepShaver/src/SDL/video_sdl2.cpp` | 60 Hz present + event drain |
| `update_display_static_bbox` | same | `nw_fb_damage_take` → per-row `memcmp`/`memcpy` shadow → `update_sdl_video` |
| `present_sdl_video` / `nw_upload_base` | same | Union dirty rects, row `memcpy` into streaming texture, Metal present |
| `nw_sweep_store` | same | Hold a tall movie band so each strip does not wait out a vsync |
| `nw_present_movie` | same | `SDL_UpdateTexture` + `SDL_RenderCopy` of source-res movie bands (GPU scale) |
| `nw_fb_damage_store` / `_rect` / `_take` | `SheepShaver/src/nw_io.cpp` | 64-px tiles + up to 48 exact rects. Bit ops on ~32 words. |
| `NQD_bitblt` / `nqd_host_scale` | `SheepShaver/src/gfxaccel.cpp` | Host CopyBits. 1:1 is `memmove`. Stretch ≥2× is scalar nearest-neighbor into the FB *and* a band copy for GPU overlay. |
| `NQD_fillrect` / `NQD_invrect` | same | Duff 64-bit fill/invert; 8-bit fill is `memset` |
| `Screen_blit` | `SheepShaver/src/CrossPlatform/video_blit.cpp` | Depth/endian convert. **Not used** for 32-bit NW (`blit = depth==16`). |

Xcode compiles `video_sdl2.cpp` from `BasiliskII/src/SDL/video_sdl2.cpp` (same content as the SheepShaver copy). `ENABLE_VOSF` is off in `config-macosx-aarch64.h`, so the SEGV-dirty path is dead.

**Audio (SDL callback thread, ~12 ms / 512 frames):**

| Function | File | What it does |
|---|---|---|
| `stream_func` | `SheepShaver/src/SDL/audio_sdl.cpp` | If SheepBlaster: `nw_sheepblaster_pull`. Else old mixer IRQ path. |
| `nw_sheepblaster_play` | `SheepShaver/src/nw_devices.cpp` | Linear resample into a 1 s S16BE stereo ring |
| `nw_sheepblaster_pull` | same | Byte-wise BE pack out of the ring |
| `AudioSheepBlasterTick` / `sb_pull` | `SheepShaver/src/audio.cpp` | Guest Time Manager: `GetSourceData` (68k mixer + QT decode) then `nw_sheepblaster_play` |

`audio_macosx.cpp` / `AudioDevice.cpp` / `MacOSX_sound_if.cpp` sit in the Xcode *group* but are **not** in `PBXBuildFile` Sources. The shipping macOS app is SDL audio, not Core Audio.

**Timer:** `SheepShaver/src/Unix/timer_unix.cpp` (`mach` `clock_get_time`). Glue only.

**MacOSX/ that is not on the hot path:** `prefs_macosx.mm`, `clip_macosx64.mm`, `sys_darwin.cpp`, `extfs_macosx.cpp`, `utils_macosx.mm`, `etherhelpertool.c`, Launcher, PrefsEditor.

Basilisk II’s Xcode app also uses `video_sdl2.cpp` (not Cocoa `video_macosx.mm`). Same video conclusion; Bill’s named workloads are SheepShaver NW.

## A.2 Ranked by wall-time (host C only)

Workloads: OS 9.2.1 Finder boot, QuickTime playback, SheepBlaster 44.1 kHz stereo, dirty-rect present.

### A rank 1 — Video present / upload (`video_sdl2.cpp`)

**Verdict: wash. Hand asm would not move FPS.**

Evidence already in-tree:

- `present_sdl_video`: “Metal still presents the whole texture”; “locking each tile was the cost”; sweep exists because “each upload waited out a vsync, which is the 4 fps picture.”
- `update_display_static_bbox`: 32-bit NW `host_surface` wraps `the_buffer`; upload is raw `memcpy` of dirty spans. Shadow `memcmp` skips a QT tile that did not change (“Uploading an unchanged tile was a multi-megabyte copy”).
- `nw_upload_base` / the union lock: row loop of `memcpy(dst, src, rowbytes)`.

Bytes: 1024×768×4 ≈ 3 MiB/frame. Apple Silicon `memcpy` is far above that. A hand NEON copy cannot beat `libsystem` `memcpy`, and even a 2× copy would not beat `SDL_LockTexture` + Metal + `SDL_RenderPresent`.

What *did* move FPS on this path is C/policy: dirty union, skip identical tiles, sweep coalescing, retain the streaming texture, avoid `RenderClear` on software. That work is done.

### A rank 2 — Guest decode / JIT (pointer to B)

**Not a host-C candidate.** QT and iTunes spend time in compiled guest code and C helpers. See **B**. Host blit asm would not show up.

### A rank 3 — `nqd_host_scale` (`gfxaccel.cpp` ~380–417)

**Verdict: the only host loop clang will not auto-vectorize. Still not worth hand ARM64.**

```c
for (int x = 0; x < dw; x++) {
    const int sx = (int)((x * sw) / dw);
    /* 4-byte gather copy */
}
```

This is nearest-neighbor stretch when dest ≥ 2× source, 32-bit, srcCopy. Clang will not turn a per-pixel integer division + gather into NEON. A **vImage** / `vImageScale_ARGB8888` call could beat this C. Hand NEON would be worse than Accelerate, longer to maintain, and still on the order of ~1 ms for a large window.

It already has a GPU path: `nw_movie_scale_put` copies source rows with `memcpy`, `nw_present_movie` uploads them and `SDL_RenderCopy` scales. CPU scale still writes the guest FB (so the pixmap is real). If this ever shows up in a profile as >1–2% of `VideoHostPresent`, the move is Accelerate or “don’t CPU-scale, only GPU overlay” — not `.S`.

### A rank 4 — SheepBlaster (`nw_devices.cpp` `nw_sheepblaster_play` / `_pull`)

**Verdict: lose. Do not touch with asm.**

- Ring is 44100 frames of S16 stereo.
- Host callback is 512 frames ≈ 2 KiB / 12 ms (`audio_sdl.cpp`).
- `play`: scalar linear interpolate, even when input is already `twos` / 16-bit / stereo / 44100 (`step = 1<<16`, `frac = 0`). A **C** fast-path `memcpy` into the ring would beat NEON resample and still be in the noise vs `GetSourceData`.
- `pull`: four-byte pack with shifts. Trivial. If you cared, store the ring already S16BE and `memcpy` (again C).

`AudioSheepBlasterTick` cost is guest mixer / QT completion / iTunes loops, documented in `SHEEPBLASTER-SOUND.md`. Host ring math is not the stutter.

### A rank 5 — NQD fill / invert / 1:1 blit (`gfxaccel.cpp`)

**Verdict: wash. Duff’s device is 1990s memcpy fanfic.**

`do_fillrect` / `do_invrect` unroll 8× `uint64` via Duff. 8-bit fill already uses `memset`. 1:1 `NQD_bitblt` uses `memmove`. clang `-O3` will emit NEON for a *simple* invert/fill loop; Duff’s switch often **blocks** auto-vectorization.

If this ever matters (Finder window drag, not QT), the win is `memset`/`memmove` or a straight `for` so clang vectorizes — not hand NEON.

8→32 and 15→32 expansion in `NQD_bitblt` / `nw_fb_expand_clut8_to_mac32` are palette gathers. Classic SIMD, but NW 9.2.1 desktop is 32-bit; this is not the QT/Finder-boot budget.

### A rank 6–7 — 16-bit `Screen_blit`, damage bookkeeping, timer, MacOSX glue

16-bit blitters are not on the NW 32-bit path. `nw_fb_damage_*` is a few integer ops next to the guest store. `timer_unix.cpp` is Mach time. Prefs / clip / etherhelper / `sys_darwin.cpp` are cold.

## A.3 Existing asm / NEON / Accelerate in tree

**None on the macOS Apple Silicon *host* path.** (The NW JIT *is* ARM64 emission for guest ops; that is section B, not a blit `.S`.)

| What | Where | Relevance |
|---|---|---|
| PowerPC host asm (get_sp, 68k exec, cache flush) | `SheepShaver/src/Unix/ppc_asm.S` | Linux/PPC host, not arm64 macOS |
| Unix/Amiga asm support | `BasiliskII/src/Unix/asm_support.s`, `BasiliskII/src/AmigaOS/asm_support.asm` | Not the Xcode app |
| x86 `bswap` inline asm | `SheepShaver/src/Unix/sysdeps.h` (`__x86_64__` / `__i386__` only) | ARM64 uses generic `bswap`; clang emits `REV`. Not a blit kernel. |
| Old dyngen JIT ops | `kpx_cpu/src/cpu/jit/*`, `ppc-dyngen-ops.cpp` | `ENABLE_DYNGEN 0` in `config-macosx-aarch64.h` |
| NW JIT emitter | `SheepShaver/src/nw_jit.cpp` | Guest PPC → ARM64. Section B. |
| Accelerate / vImage / vDSP / `arm_neon.h` | **no matches** in SheepShaver/Basilisk host sources | — |

`UNALIGNED_PROFITABLE` is defined only for x86 in `sysdeps.h`. ARM64 takes the extra align branches in the Duff blitters. Harmless; not a reason to write asm.

Release Xcode: `-O3`, empty `OTHER_CFLAGS` (no `-fno-vectorize`). clang is allowed to NEON-vectorize.

---

# B. NW ARM64 JIT (guest PPC → host machine code)

This is **not** “write host blit in NEON.” It is “make the translator denser / cache better / hop more, without breaking the holds that already shipped and reverted.”

Live path: `powerpc_cpu::nw_jit_try` in `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp` → cache get/compile → `fn(&jc)` → optional hops → fault commit.

## B. What is already landed (do not re-propose as new)

From `nw_jit.h` / `nw_jit.cpp` and later commits than `NW-JIT-MILL.md`:

| Item | Tip state |
|---|---|
| Opcode mill | `JIT_OPS.md`: **308 done, 34 partial, 0 todo**, 3 exclude (345 names). Mill leftovers in the Sept 15 tracker (`mtmsr`, `bc`, `lvx`, `mullw`, …) are allowlisted. |
| Code buffer / cache | `NW_JIT_CODE_SIZE = 1<<25` (32 MiB), 8 banks, `NW_JIT_CACHE = 262144`, `NW_JIT_PROBE = 16`, coldest-of-16 evict, 68k PC hits bias (`0x68000000`), tombstones, `page_may_have_code` so FB/I/O are never scanned. |
| DTLB | 1024 sets × 2 ways, PR tag, SR gen, BAT gen, **host-pointer inline `ldr`/`str` + `REV`** (`NW_JIT_DTLB_HOST`). Miss → C helper which fills. |
| ITLB | 256-entry fetch cache. Hop stops on miss (`NW_JIT_HOP_ITLB_MISS`). |
| GPR pins | x21–x24 for this block; dropped on every `BLR` (helpers write `gpr[]`). |
| skip-io | 68k emu VIA/PMU/SCC/PCI loads stay in JIT via `jit_io_load` (`ppc-cpu.cpp` ~1891). Used to be one insn/block. |
| Fault-exit | ON path commits on `FAULT_EXC`, `FAULT_SMC` (`pc = jc.pc+4`), DSI (`take_data_dsi`, hop-aware `dsi_block_pc`), mid-block I/O (`commit` + resume at faulting pc). |
| Class gate | Integer / FP / VMX stay in **separate** blocks. Hops refuse a successor with VEC/FP off (`HOP_VEC_GATE` / `HOP_FP_GATE`). Comment: ungated hops inverted QuickDraw. |
| Flush-by-cause | `NW_JIT_FL_*` counters: store, icbi, tlb, sr, bat, sdr1, wrap, istore, host, other. Printed from `nw_jit_stats_print`. |
| Chain hops | Up to 8 in `nw_jit_try`; `NW_JIT_TAIL_MAX = 1`. Equality on `chain_pc` only — taken-`bc` hop blacked glyphs; LR/CTR hop blacked the FB. |
| ARM FMA emit | **Code exists** (`fmadd s0, …` in `emit_fp_inline`) and is **hard-off**: `const int fast = 0`. |

`NW-JIT-MILL.md` numbers (evict 9.22 M / 11.5 M compiles, DTLB 256, 8 MiB arena, mtmsr as #1 skip) describe a **older** dump. Treat them as history, not current occupancy.

## B. Ranked by Finder / QuickTime wall-time

Legend: **M** = measured in-tree (doc or comment with a number). **H** = hypothesized from code shape; needs a live `NW_JIT_STATS=1` / `sb-cost` tick on this tip. **Hold** = shipped once and broke a named workload.

### B rank 1 — Re-enable ARM FP (`emit_fp_inline`, `fast = 0`) — **M + Hold**

**Workload:** iTunes / SheepBlaster decode, QT audio. Finder boot is weaker (less `fmadds`).

`SHEEPBLASTER-SOUND.md` (measured on a full song):

- `itunes-cost` `host_per_audio` 2.3–3.9, avg **3.55**.
- Loop at `1dfa2354`: `addi`, `rlwinm`, `lfs`, `lfsx`, `fmuls`, `fmadds`.
- Loop at `1dedfd38`: `lfs`, `fmuls`, `fmadds`, `fadds`, `fsubs`, `stfs`.
- “`fmadds` is compiled as a call to `nw_jit_helper_fmadds`, not an ARM floating-point instruction. **That is the cost.**”
- `interp` 0, `skip` 0 — loops *are* compiled. `vr`/`vmx` 0 on that path. `fp_gate` ~180/s. `class_change` ~0 after the first second.

In `nw_jit.cpp` `emit_fp_inline`:

```c
/* ARM inline of these ops, mixed integer/FP blocks, and the
 * taken-branch hop each shipped once and iTunes lost its text
 * and its audio. Every op stays on the helper. */
const int fast = 0;
```

The ARM `fadd`/`fmul`/`fmadd` + `fcvt` sequence is sitting dead. Helpers still do host `double`/`float` plus `nw_jit_fpscr_fprf`. Even the “fast” path still `BLR`s `nw_jit_fprf_fd` for FPSCR.

**Do not flip `fast = 1` as a drive-by.** The hold is iTunes text+audio. A real land needs VERIFY against kpx on NaN/denorm/FPSCR enables, then the same song + Finder. Expected wall-time if it sticks: the only JIT change with a measured multi-second-per-audio-second bill.

Also not host NEON: this is the *guest* FPU as ARM FP.

### B rank 2 — Class-change cuts (int vs FP vs VMX) — **M, correctness hold on VEC**

**Workload:** same iTunes loops (integer and FP alternate → separate blocks). QT QuickDraw is AltiVec-class; mixing VEC into an integer block with `MSR[VEC]=0` skipped `0xf20` and inverted QD (comment in `nw_jit_try`).

`ppc-cpu.cpp`: a block ends when `is_fp_insn` / `is_altivec_insn` differs from op0 (`NW_JIT_CUT_CLASS_CHANGE`). Hops re-check MSR and marshall FPR/VR on class change.

**Safe hypothesized win:** allow **integer + FP in one block when the first op already required FP** (MSR[FP] already tested). That is exactly the iTunes loop. **Unsafe:** mix VMX into a non-VMX block, or hop to VMX with VEC off.

This is a translator policy change, not host asm. It also makes rank 1 worth more (longer FP/int blocks, fewer `execute()` round trips).

### B rank 3 — AltiVec / lvx density (helpers vs NEON) — **H for QT picture; M that iTunes is not this**

`SHEEPBLASTER-SOUND.md`: iTunes path `vr=0`, `vmx=0`. QuickDraw on 9.2.1 *does* use AltiVec (`main_unix.cpp` PVR 7400 comment).

Today: `lvx`/`stvx` go through `guest_data_probe` helpers; many VX ops are `nw_jit_helper_v*` scalar word loops or `nw_jit_helper_vmx` → kpx. `vslo`/`vsro` are allowlisted but **one-op `ends_block`** (Starting Up lock).

Native NEON for `vand`/`vxor`/`vaddubm` is a density win on QD memcpy/blit inner loops. Same class of work as rank 1, different workload. Harness must keep VR big-endian word order. **Hypothesized** for QT FPS; do not confuse with host `nqd_host_scale`.

### B rank 4 — Chain hops / `chain_disp` / ITLB — **H, with holds**

Hops already exist (cap 8). Stops: no `chain_pc`, `pc != chain_pc`, ITLB miss, A-line, cache miss, VEC/FP gate. Taken-`bc` hop and LR/CTR hop are **holds**.

Hypothesized: after rank 1–2, `HOP_FP_GATE` / `CLASS_CHANGE` / `CACHE_MISS` counts (already in `nw_jit_hop_stop_count`) say whether hop policy still matters. Growing ITLB (256) only helps if `HOP_ITLB_MISS` is hot. **Need a live stats tick**; mill did not publish hop-stop histograms for this tip.

### B rank 5 — DTLB miss / host-pointer path — **partially M, rest H**

Inline hit is `ldr`/`str` + `REV` when `NW_JIT_DTLB_HOST` is set. FB stores bind a host pointer; FB is not a code page. Mill’s 67 M miss / 619 M hit was **256-entry**. Tip is 1024×2. Further growth is hypothesized; flush sources (`dtlb-flush mtmsr/rfi/mtsr`) matter more than another size bump.

`mtmsr` still **ends the block** (`[~]` in `JIT_OPS.md`). Finder→CS used to be a 53 M `mtmsr` skip; it is a helper now, but still a block cut + DTLB PR flush. That is Finder/CS, not QT decode.

### B rank 6 — Compile cache occupancy, wrap, O(n) invalidate — **H; mill stale**

- `nw_jit_invalidate_page_src` still walks **all 262144 slots** (pagebit filter first). Mill listed “page→slot list” as avoided.
- Wrap: 32 MiB / 8 banks; crossing a bank `invalidate_bank`. Apple `pthread_jit_write_protect_np` + icache sync timed in `g_wx_ns` (Debug `NW_BOOT_LOG` only).
- Evict: coldest-of-16. Mill’s 80%+ evict rate is pre-262144. **Need `occ_max` / `evict` / `wrap` from `NW_JIT_STATS=1` on this tip** before spending a week on a slot index.

Host flush (`NW_JIT_FL_HOST`): mill had thousands of calls and **0 entries** (`WriteMacInt*` PAs ≠ compile `phys_page`). If still true, host-invalidate matching is not a Finder win.

G5–G6 (`G5-G6-brief.md` / `NEWWORLD-BOOT-PLAN.md`): ~190 k **flush/s** and JIT slower than interpreter (210 s vs 130 s to Finder). That is the **old flush-all** world. Do not use those rates as current.

### B rank 7 — Opcode mill leftovers / 68k — **M that they are not the iTunes budget**

`JIT_OPS.md` todo count is 0. Remaining `[~]`: RA≠0 update forms, L=0 compares, `ends_block` (`rfi`, `icbi`, `tlbie`, `mtmsr`, `vslo`/`vsro`). Mill: Control Strip is 68k at `50323678` `rfi` — “PPC mill will not erase this.” A 68k JIT is a different project.

`lwarx`/`stwcx.` still in mill “do not retry as-is.” Not Finder/QT.

### B rank 8 — VERIFY harness gaps — **correctness tax, not FPS**

`mmu_harness.cpp` has ~2000 `CHECK(` sites; mill bar was ≥450/0. Gaps that block rank 1: FPSCR exception enables vs ARM FP, denorms, Rc/`cr1`, helper vs kpx bit-identical `fmadds`. VERIFY still compiles a shadow and lets kpx own the guest (`nw_jit.h` 4b). Turning `fast` on without harness coverage is how iTunes lost text+audio.

### B rank 9 — Fault-exit / skip-io / DSI resume — **mostly done**

skip-io compiled in. DSI after hop uses `dsi_block_pc`. Mid-block I/O commits and resumes. First-insn I/O/DSI returns 0 for kpx. Further work here is bugfix, not a ranked FPS lever, unless a live log shows `skip_io` / `jit dsi` in the inner loop again.

## B. What would actually move the named workloads (JIT)

| Workload | JIT lever | Host blit asm? |
|---|---|---|
| Finder boot | Cache occupancy (measure first), `mtmsr` still ending blocks, NK/68k `rfi`, DTLB flushes | No |
| QuickTime picture | AltiVec helper density + present policy (section A). Guest draw, then Metal. | No |
| SheepBlaster / iTunes | **Re-land ARM FP (`fast`) under VERIFY; then mixed int+FP blocks** | No |
| Dirty-rect present | Not JIT. Section A. | No |

---

## Combined: what to do vs what not to do

| Do | Do not |
|---|---|
| Treat `emit_fp_inline` `fast = 0` as the #1 *JIT* experiment, behind VERIFY + iTunes/Finder | Write host ARM64 `memcpy`/`memcmp`/resample |
| Measure `NW_JIT_STATS=1` wrap/evict/`occ_max`/dtlb/hop-stops on *this* tip before cache surgery | Trust Sept 15 mill evict/DTLB numbers as current |
| Keep VEC class-change / hop gates (QD hold) | Mix VMX into integer blocks or hop taken-`bc`/LR/CTR |
| If `nqd_host_scale` ever profiles hot: vImage or GPU-only overlay | Hand NEON gather-scale |

---

## Method notes

Read the files and the comments/measurements already in them. Did not rewrite loops, did not microbench on this Linux agent (no Apple Silicon, no SheepShaver GUI).

**Missing docs:** `NW-JIT-PERF-ANALYSIS.md`, `PERF-DEEP-PASS.md` — not in the tree. Used `SHEEPBLASTER-SOUND.md` (measured iTunes), `JIT_OPS.md`, `NW-JIT-MILL.md` (stale), `G5-G6-brief.md` / `NEWWORLD-BOOT-PLAN.md` (old flush-all rates).

Confidence: high that host blit asm will not beat memcpy/vsync. High that iTunes cost is helper FP, not the SheepBlaster ring. High that ARM FMA was tried and reverted. Moderate on current cache occupancy (no live `occ_max` on this agent). Moderate on AltiVec vs QT picture share (`vr=0` on iTunes; QD is a different path).
