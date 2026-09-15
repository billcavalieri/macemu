# NW JIT mill tracker

Source dump: Debug ON 100 s `/tmp/g8/dss.log` (`G1: jit on`, Finder ~50 s, Control Strip ~70 s), 2026-09-15. Skip counts are how often that op was the **first unsup in a block** (copy-out), not total executions.

JIT stays on. Emulate over skip. Do not retry known-bad emits as-is. After each land: harness ≥450/0, `NW_SCRIPT` shots, look at PNGs (splash / Starting Up / Finder / Control Strip as relevant).

## Done (do not regress)

- [x] CR logicals: crnor, crxor, creqv, cror, crorc, crand, crandc, crnand
- [x] `isync` **helper** (`d3019fbe`): `execute_invalidate_cache_range` + ISB + end block. Not a skip.
- [x] `dss` (31/822) **nop** (`a8eaab1b`): ARM NOP, does not end the block. `xo=822` gone from skip_unsup.
- [x] Arena 8 MiB / 32k slots, last-probe evict (not home smash)
- [x] Page filter: RAM+ROM bits, clear after drop; **FB/IO never scanned** (`20d9fcda`)
- [x] Host-side Mac OS ROM from volume image (contiguous `<CHRP-BOOT>` scan)
- [x] `NW_JIT_STATS=1` 10 s ticks in Release; `evict` / `recompile_n` on the stats line

## 1. `skip_unsup` — mill from this log

| Status | encoding | name | n | Notes |
|---|---|---|---|---|
| [x] | **31/146** | **mtmsr** | **53.2 M** | Landed `b36a7c80`. Host `set_msr(rS)` + DTLB flush, end block. `/tmp/g8/mtmsr` splash/Finder/Control Strip. `xo=146` gone. |
| [x] | **16** | **bc** | **27.3 M** | Landed `cb26a3d9`. CR-only stays inlined; CTR/LK/AA use `nw_jit_helper_bc` (kpx `execute_branch`). `/tmp/g8/bc2` splash/Finder/CS. prim=16 gone. |
| [x] | **31/467** | **mtspr** leftover | **8.3 M** | Landed `5cf6cff7`. Leftover SPRs call kpx `mtspr_guest`. `/tmp/g8/mtspr2` splash/Starting Up/Finder. `xo=467` gone. |
| [x] | **35** | **lbzu** | **5.85 M** | Landed after EA-last + save EA across helper (not old shared path). `/tmp/g8/lbzu2` splash/Finder/Control Strip. prim=35 gone. |
| [ ] | **31/103** | **lvx** | **3.30 M** | AltiVec load 16 B. Real emit or helper, not a nop. Likely memcpy / Control Strip. |
| [ ] | **31/231** | **stvx** | **3.26 M** | AltiVec store 16 B. Pair with `lvx`. |
| [ ] | **31/339** | **mfspr** leftover | **2.89 M** | User + TBL/TBU/PVR/SPRG/VRSAVE **reads** already in. Other SPRs: extend helper, no SPRG writes. |
| [ ] | **50** | **lfd** | **2.87 M** | FP load double. Integer mill skipped FP on purpose. Later Finder/QD, not CS first. |
| [ ] | **54** | **stfd** | **2.87 M** | FP store double. Same. |
| [ ] | **31/235** | **mullw** | **2.68 M** | 32×32→32. Cheap ARM `MUL`/`MADD`. Safe mill. |
| [ ] | **31/11** | **mulhwu** | **2.37 M** | High unsigned multiply. Cheap integer emit. |
| [ ] | **31/60** | **andc** | **1.90 M** | `rA = rS AND NOT rB`. Same shape as `and`. Cheapest leftover logical. |

Earlier ticks (not always in the last top-12, still live):

| Status | encoding | name | Notes |
|---|---|---|---|
| [ ] | 31/55 | lwzux | Update-form; same class as lbzu/stwux. |
| [ ] | 31/150 | stwcx. | With lwarx; reservation. Do not retry as-is. |
| [ ] | 31/20 | lwarx | Same. |
| [ ] | 19/50 | rfi | Do not retry as-is. NK 68k path. |
| [ ] | 31/183 | stwux | Reverted: six 972-byte black frames. |
| [ ] | 31/86, 278, 54, 246 | dcbf / dcbt / dcbst / dcbtst | kpx already nops. Same mill as `dss` if they reappear. |

### Hot PCs (same dump)

- `0027badc` `mtmsr` **53 M** — Finder→CS loop (was paired with `dss` at `0027bad4`).
- `007ad554` / `007ac5b4` / `00387ce8` **`bc`** — CTR loops.
- `50323678` **`rfi`** — NK 68k interpreter. Control Strip is 68k; PPC mill will not erase this.
- `00000708` **`mtspr`** — leftover SPR.

## 2. Log signals that are not skip_unsup

| Status | Signal | Last dump | Meaning |
|---|---|---|---|
| [ ] | **evict** | 9.22 M of 11.5 M compiles | Probe-full still throws work away. 8 MiB wrap (~324 calls) is mostly **dead ARM from evicts**. Next capacity: evict policy / more probes, not another size bump. |
| [ ] | **host flush** | 6439 calls, **0 entries** | `WriteMacInt*` PAs ≠ compile `phys_page`. Large arena + host memcpy can go stale. |
| [ ] | **dtlb miss** | 67 M vs 619 M hit | 256-entry DTLB vs ~300 FB pages + RAM. Grow DTLB (power of 2; fix ARM `AND #255`). |
| [x] | **icbi** | 11.6 k / 121 k drops | `isync` helper is working. Do not skip `isync` again. |
| [ ] | **mean block ~3.3 insns** | | Blocks **start** on unsup. Raising `NW_JIT_MAX_BLOCK` is pointless until `mtmsr`/`bc`/`lbzu` are not the first insn. |
| [ ] | **WP5 dirty tiles** | | Idle present bandwidth after CPU in that window is not the bottleneck. Does not move Control Strip. |

## 3. Tried and reverted (do not paste the old emit)

- **mtspr SPRG/VRSAVE** — NK hang, black `503106d8`.
- **lbzu** — bus-error bomb; broke `lbz` via shared helper.
- **stwux** — six 972-byte black frames.
- **lbzux** — not retried; same update-form class.
- **bc CTR-LK** — not as-is.
- **JIT `isync` = ISB only** — Apple Audio illegal instruction. Replaced by helper.
- **32 k slots overwriting the home probe** — evicted RAM-zero loop, black FB. Last-probe evict now.
- **Pagebit `(phys>>12)&4095`** — 16 MiB aliasing.
- **Copy-out skip `isync`** (`a237da39`) — temporary; replaced by `d3019fbe`. Do not ship another skip around it.

## 4. Avoided (never landed)

- `mtmsr` helper (now #1 skip).
- `andc` / `mullw` / `mulhwu`.
- `lvx` / `stvx`.
- CTR/LK `bc` new emit.
- `dcbf`/`dcbt`/`dcbst` nops if they show.
- WP5 dirty tiles.
- Host-invalidate PA match (0 hits).
- DTLB 256 → 1024.
- Page→slot list (`invalidate_page` still O(cache)).
- `NW_JIT_MAX_BLOCK` > 16.
- 68k JIT (Control Strip in NK interpreter).
- Volume-ROM HFS catalog (scan-only; OK while data fork is contiguous).
- Release `NW_SCRIPT` (shots are Debug-only).

## 5. Suggested order (JIT on)

1. **`mtmsr` helper** — only item that can move Finder→Control Strip on this dump.
2. **`andc`**, then **`mullw`/`mulhwu`**.
3. **`lvx`/`stvx`** if CS/Finder memcpy is still fat.
4. **CTR/LK `bc`** — new emit.
5. **Evict/probes** and **host PA**.
6. **`lbzu`/`stwux`/`rfi`/`lwarx`** — only with a new design.
7. **WP5** after that window is not CPU-bound.
8. Do not mill FP (`lfd`/`stfd`) or 68k until `mtmsr` and `bc` are native.

## 6. Smoke recipe

```bash
pgrep -x SheepShaver && exit 1
# Debug arm64, derivedData /tmp/macemu-s4-dd
HOME=/tmp/g8/home-isync NW_JIT=on NW_SCRIPT=/tmp/g8/<tag>.nws \
  .../SheepShaver --config /tmp/prefs-hd
# every 10 shot; SIGTERM; ppm2png; look at 001 + Finder + Control Strip
# grep skip_unsup for the landed xo; harness ≥450/0
```

Release wall clock: no `NW_SCRIPT`. `NW_JIT_STATS=1` for wrap/evict/skip ticks.
