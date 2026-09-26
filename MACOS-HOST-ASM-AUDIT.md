# macOS host C vs hand-written ARM64 / NEON

**Tree:** `billcavalieri/macemu` branch `newworld-boot` @ `1204ba0d`  
**Build that matters:** `SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj`, Release, `GCC_OPTIMIZATION_LEVEL = 3`, `VALID_ARCHS` includes `arm64`, `USE_SDL_VIDEO=1`, `USE_SDL_AUDIO=1`, `ENABLE_VOSF` undefined.  
**Question:** would any *host* macOS C/C++ see real wall-time wins as hand-written ARM64 / NEON / Accelerate, versus clang `-O3` + libc + the GPU?

**Answer in one line:** no. Do not write host ARM64 asm. The FPS and audio stories on this tree are guest JIT / MMU / vsync-present, not host blit loops.

---

## Yes / no by area

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
| Guest PPC → ARM64 JIT (`nw_jit.cpp`) | **Out of scope.** | Already machine code for guest ops. Remaining cost there is helper *calls* (`nw_jit_helper_fmadds`), not host video C. |

---

## 1. Hot execution paths (macOS Apple Silicon build)

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

---

## 2. Ranked by wall-time on Bill’s workloads

Workloads: OS 9.2.1 Finder boot, QuickTime playback, SheepBlaster 44.1 kHz stereo, dirty-rect present.

### Rank 1 — Video present / upload (`video_sdl2.cpp`)

**Verdict: wash. Hand asm would not move FPS.**

Evidence already in-tree:

- `present_sdl_video`: “Metal still presents the whole texture”; “locking each tile was the cost”; sweep exists because “each upload waited out a vsync, which is the 4 fps picture.”
- `update_display_static_bbox`: 32-bit NW `host_surface` wraps `the_buffer`; upload is raw `memcpy` of dirty spans. Shadow `memcmp` skips a QT tile that did not change (“Uploading an unchanged tile was a multi-megabyte copy”).
- `nw_upload_base` / the union lock: row loop of `memcpy(dst, src, rowbytes)`.

Bytes: 1024×768×4 ≈ 3 MiB/frame. Apple Silicon `memcpy` is far above that. A hand NEON copy cannot beat `libsystem` `memcpy`, and even a 2× copy would not beat `SDL_LockTexture` + Metal + `SDL_RenderPresent`.

What *did* move FPS on this path is C/policy: dirty union, skip identical tiles, sweep coalescing, retain the streaming texture, avoid `RenderClear` on software. That work is done.

### Rank 2 — Guest decode / JIT (not host C, listed so it is not mistaken for blit)

**Verdict: this is where QT and iTunes actually spend time. Host asm on video/audio C would not show up.**

`SHEEPBLASTER-SOUND.md` (measured): iTunes MP3 `host_per_audio` ≈ 3.55; `fmadds` compiled as a **call** to `nw_jit_helper_fmadds`, not an ARM FMA. `cpu_per_audio` (mixer copy) ≈ 0.07 — “The decode is not the budget” for the *mixer* copy; the decoder in the guest is. `vr`/`vmx` were 0 on that iTunes path.

Finder boot is JIT + MMU + I/O + NanoKernel. `nw_fb_damage_store` is a few integer ops per guest store into the FB bank. Rewriting that marker in asm is noise next to the store itself.

Do not rewrite `nw_jit.cpp` as the answer to this audit (already guest machine code). The remaining JIT wins are still JIT (inline FMA, fewer helper calls), not host blit NEON.

### Rank 3 — `nqd_host_scale` (`gfxaccel.cpp` ~380–417)

**Verdict: the only host loop clang will not auto-vectorize. Still not worth hand ARM64.**

```c
for (int x = 0; x < dw; x++) {
    const int sx = (int)((x * sw) / dw);
    /* 4-byte gather copy */
}
```

This is nearest-neighbor stretch when dest ≥ 2× source, 32-bit, srcCopy. Clang will not turn a per-pixel integer division + gather into NEON. A **vImage** / `vImageScale_ARGB8888` call could beat this C. Hand NEON would be worse than Accelerate, longer to maintain, and still on the order of ~1 ms for a large window.

It already has a GPU path: `nw_movie_scale_put` copies source rows with `memcpy`, `nw_present_movie` uploads them and `SDL_RenderCopy` scales. CPU scale still writes the guest FB (so the pixmap is real). If this ever shows up in a profile as >1–2% of `VideoHostPresent`, the move is Accelerate or “don’t CPU-scale, only GPU overlay” — not `.S`.

### Rank 4 — SheepBlaster (`nw_devices.cpp` `nw_sheepblaster_play` / `_pull`)

**Verdict: lose. Do not touch with asm.**

- Ring is 44100 frames of S16 stereo.
- Host callback is 512 frames ≈ 2 KiB / 12 ms (`audio_sdl.cpp`).
- `play`: scalar linear interpolate, even when input is already `twos` / 16-bit / stereo / 44100 (`step = 1<<16`, `frac = 0`). A **C** fast-path `memcpy` into the ring would beat NEON resample and still be in the noise vs `GetSourceData`.
- `pull`: four-byte pack with shifts. Trivial. If you cared, store the ring already S16BE and `memcpy` (again C).

`AudioSheepBlasterTick` cost is guest mixer / QT completion / iTunes loops, documented in `SHEEPBLASTER-SOUND.md`. Host ring math is not the stutter.

### Rank 5 — NQD fill / invert / 1:1 blit (`gfxaccel.cpp`)

**Verdict: wash. Duff’s device is 1990s memcpy fanfic.**

`do_fillrect` / `do_invrect` unroll 8× `uint64` via Duff. 8-bit fill already uses `memset`. 1:1 `NQD_bitblt` uses `memmove`. clang `-O3` will emit NEON for a *simple* invert/fill loop; Duff’s switch often **blocks** auto-vectorization.

If this ever matters (Finder window drag, not QT), the win is `memset`/`memmove` or a straight `for` so clang vectorizes — not hand NEON.

8→32 and 15→32 expansion in `NQD_bitblt` / `nw_fb_expand_clut8_to_mac32` are palette gathers. Classic SIMD, but NW 9.2.1 desktop is 32-bit; this is not the QT/Finder-boot budget.

### Rank 6 — 16-bit `Screen_blit`

**Verdict: not on the NW 32-bit path.** `update_display_static_bbox` sets `blit` only for `VIDEO_DEPTH_16BIT`. Same Duff templates in `video_blit.h`. Same advice: if someone runs 16-bit, simplify the C.

### Rank 7 — Damage bookkeeping, timer, MacOSX glue

`nw_fb_damage_*`: tile bitmap ~32× `uint32`, 48 rects. Called from JIT stores; the store is the cost.  
`timer_unix.cpp`: Mach time.  
`prefs` / clip / etherhelper / `sys_darwin.cpp`: cold.

---

## 3. Existing asm / NEON / Accelerate in tree

**None on the macOS Apple Silicon host path.**

| What | Where | Relevance |
|---|---|---|
| PowerPC host asm (get_sp, 68k exec, cache flush) | `SheepShaver/src/Unix/ppc_asm.S` | Linux/PPC host, not arm64 macOS |
| Unix/Amiga asm support | `BasiliskII/src/Unix/asm_support.s`, `BasiliskII/src/AmigaOS/asm_support.asm` | Not the Xcode app |
| x86 `bswap` inline asm | `SheepShaver/src/Unix/sysdeps.h` (`__x86_64__` / `__i386__` only) | ARM64 uses generic `bswap`; clang emits `REV`. Not a blit kernel. |
| Old dyngen JIT ops | `kpx_cpu/src/cpu/jit/*`, `ppc-dyngen-ops.cpp` | `ENABLE_DYNGEN 0` in `config-macosx-aarch64.h` |
| NW JIT emitter | `SheepShaver/src/nw_jit.cpp` | Guest PPC → ARM64. Out of scope. |
| Accelerate / vImage / vDSP / `arm_neon.h` | **no matches** in SheepShaver/Basilisk host sources | — |

`UNALIGNED_PROFITABLE` is defined only for x86 in `sysdeps.h`. ARM64 takes the extra align branches in the Duff blitters. Harmless; not a reason to write asm.

Release Xcode: `-O3`, empty `OTHER_CFLAGS` (no `-fno-vectorize`). clang is allowed to NEON-vectorize.

---

## 4. What would actually move the named workloads

| Workload | Bottleneck | Host asm? |
|---|---|---|
| OS 9.2.1 Finder boot | Guest JIT / MMU / I/O / NanoKernel | No |
| QuickTime picture | Guest draw + Metal present of the retained texture + vsync; full-frame dirtiness | No. Policy already: skip identical tiles, sweep bands, GPU overlay |
| SheepBlaster audio | Guest `GetSourceData` / iTunes loops / `nw_jit_helper_fmadds` | No |
| Dirty-rect present | `SDL_LockTexture` + `SDL_RenderCopy(NULL)` on Metal + `SDL_RenderPresent` | No |

If a profiler later shows `nqd_host_scale` as a real slice of `VideoHostPresent`, call `vImageScale` (or skip the CPU stretch and keep the GPU overlay only). That is still C, not machine code.

---

## Method notes

Read the files and the comments/measurements already in them. Did not rewrite loops, did not microbench on this Linux agent (no Apple Silicon, no SheepShaver GUI). Confidence on “asm will not beat memcpy / will not beat vsync” is high from the byte rates and the in-tree FPS comments. Confidence on `nqd_host_scale` wall-time share is moderate (loop shape is a SIMD candidate; named FPS story points elsewhere).
