# SheepBench

SheepBench is a benchmark app for Mac OS 9 that you open and run inside the guest. MacBench reports a comparison
with a G3. SheepBench reports what matters for this emulator instead:

- **Raw numbers**: ns per instruction, MB/s, operations, frames and I/O operations per second.
- **JIT speedup**: each test with the JIT on and with it off. This is the main number.
- **Percent of native**: the same C code timed on the Mac itself. 100 % means native speed.
- **GPU speedup**: QuickDraw with SheepForce on and with it off.
- **Run info**: the commit, the prefs that matter, host model, macOS version, and the type of each disk image.

The guest app is in `guest/sheepbench/`. Its host side ships inside SheepShaver as four Sheep Shears mailbox
commands, so nothing is installed on the Mac.

## Using it

1. Guest > Add SheepBench Disk... adds the bundled `SheepBench.hfv` to a VM. Restart the VM and open SheepBench on
   the "SheepBench" disk. The extfs download, `guest/dist/SheepBench-extfs.zip`, holds the same app.
2. Pick the suites and click Run. Stop, or Cmd-period, ends a run between repetitions.
3. File > Show Results lists one row per test. Save Report writes a tab-separated text file. Compare Reports puts
   two saved reports side by side.
4. Apple > About SheepBench lists what the host reported.

The JIT-off and GPU-off comparisons switch the emulator while it runs. They are off unless the VM's setting
"Allow SheepBench to change JIT and SheepForce while running" is on (Advanced; pref `bench_switch`, default false).
Without it, SheepBench measures the current settings and the two comparison boxes are greyed out.

When the host answers, every finished run is also written next to the VM's prefs file, as
`SheepBench/SheepBench <date>.tsv` and a `.json` made from it.

## The suites

| Suite | What it measures | Checked by |
|---|---|---|
| PowerPC CPU | The seven loops of `ppc_core_harness --loop-bench`, as the same instruction words, with the callee one page away as in the harness (ns per instruction). The shared C kernels: integer, byte copy, compare, call, call through a pointer, `lwarx/stwcx.`, double and single fma, a vector loop in C and the same loop in AltiVec. | The copies against the source, the loop and call counts, and each kernel's checksum against the value computed at build time and against the host's native run. |
| 68k CPU | add/eor/rol, move.l and move.b copies, bsr/rts, mulu/divu, in 68k code run by the ROM's 68k emulator through Mixed Mode (ns per 68k instruction). With switching allowed, the same tests again with only the host 68k JIT off, as separate rows. | Results worked out in C; the copies against the source. |
| Memory | memcpy, an 8-byte copy loop, BlockMove, BlockMoveData and memset at 4 KB, 64 KB, 1 MB and 16 MB; dependent-load latency around a shuffled ring of the same sizes. | Checksums of the destination. |
| MMU | Page size and virtual memory (run info). Loads one per page over 16, 256 and 4096 pages against loads in one page; the first write to each page of a new 4 MB block; rewriting, flushing and calling a routine (self-modifying code); MakeDataExecutable alone. | The load sums; the rewritten routine must return the new value. |
| QuickDraw | Fills, inversion, pattern fills, frames, lines, CopyBits from 32-, 16- and 8-bit sources, srcXor and srcOr, ScrollRect, text and icons, at 64 x 64, 512 x 512 and full screen, on screen where SheepForce's hooks apply. Each test names the hook it should reach, and the host's per-hook counters show which it did. | The same drawing done by QuickDraw in an offscreen GWorld, compared with the screen pixel by pixel where the window shows. |
| 3D | Every RAVE engine that takes a 640 x 480 ARGB32 offscreen context: flat, Gouraud and textured triangles, Z buffer with alpha blending, Z buffer with fog, at 100, 1000 and 10000 triangles a frame (frames per second). A QuickDraw 3D scene of 1000 and 10000 triangles through the interactive renderer, with the engine QuickDraw 3D picks and with SheepForce Metal asked for. | A red triangle on blue for each engine; a triangle over the middle for QuickDraw 3D. |
| Disk | A 32 MB file on each writable volume, sequential and random reads and writes at 512 B, 4 KB, 64 KB and 1 MB, with the no-cache flag (MB/s; I/O operations per second for small random blocks). Each row names the image type. | Every read against what was written there. |

**Measurement rule.** Each repetition is made long enough to take at least 20 ms. At least five repetitions are timed
and the median is taken. Repetitions are added until the noise (median absolute deviation / median) is under 2 %,
or until 25 repetitions or the test's time budget. A result that never settles is marked "noisy". A test that
cannot run is "skipped" with the reason, never zero. A failed check marks the row "FAILED" with what was wrong, and
it has no number. Quick runs use 10 ms, at most 9 repetitions and a smaller budget.

**Derived columns.** `pct_native`, `slowdown_x`, `jit_speedup_x` and `gpu_speedup_x` are ratios of two
measurements. Everything else in a row was measured. `pct_native` is 100 x native time / guest time for the same C
kernel, and `slowdown_x` is its inverse. The asm loops and the toolbox and graphics tests have no native column.

**Disk numbers.** The host reads disk images through the macOS file cache (no `F_NOCACHE`), so these figures
describe the emulator's disk path more than the SSD. The report says so.

## Host side

| Command | Number | What it does |
|---|---|---|
| `benchInfo` | 7 | The run info as `key=value` lines: PVR, clock and time base rates, RAM, the JIT mode and where it came from, `jit68k_host`, `NW_JIT_LEGACY`, SheepForce and its sub-prefs, the RAVE engine's state, `bench_switch`, the switch state, QuickDraw hook hit counts, and one line per drive (`kind=raw/qcow2/chd/sparsebundle/vhd/bincue`, the shared folder as `extfs`). The app adds the build commit (Info.plist `SheepGitCommit`, written by an Xcode phase, `+dirty` when tracked files differ), host model and CPU, and macOS version. |
| `benchNative` | 8 | Runs a kernel from `guest/sheepbench/kernels/sb_kernels.c` (compiled into the app for ARM64) with the guest's iteration count and parameter. It returns the checksum and the time per iteration: scaled to at least 2 ms, median of five, at most about 50 ms, on the emulation thread while the guest waits. |
| `benchSetJIT` | 9 | PowerPC JIT, host 68k JIT and SheepForce QuickDraw on or off, or "as at start-up". Refused unless `bench_switch` is on. Only what was on at start-up can be switched off and back on; a VM started in verify or fallback mode cannot be switched. The request is applied at the next interrupt check, between blocks on the emulation thread (`SheepBenchApplyPending` in `HandleInterrupt`): `nw_jit_set_mode`, `nw_68k_set_host_mode` and a SheepForce QuickDraw suspend flag (never a prefs write), then `nw_jit_invalidate_all`. The app waits until benchInfo shows nothing pending before it times anything. |
| `benchResult` | 10 | The finished report in chunks; the host writes the `.tsv` and `.json`. |

These commands do not count as the Sheep Shears tool calling, and SheepBench never sends HELLO, so running it does
not disturb the tool's session. `noshears true` turns them off with the rest of Sheep Shears.

**Plain SheepShaver.** Stock SheepShaver also writes `'Baah'` at 0x2800 but treats the Shears native selector as fatal
and quits. This SheepShaver now also writes `'SHRS'` at 0x2860 (`XLM_SHEARS_SIG`). SheepBench and the Sheep Shears
tools only execute the opcode when both are there. Anywhere else, SheepBench runs, reports the host as unknown and
offers no comparisons.

**Speed.** Nothing on the hot path changed. The switch rides on the interrupt check, which runs anyway; the QuickDraw
gate gained one relaxed atomic load; the hook counters are one increment per accelerated QuickDraw call.

## What the first runs found (October 16, 2026)

These come from windowless test VMs: 1024 x 768 at 32 bits, 512 MB, `jit true`, `sheepforce true`, on the
development Mac. They are not tuned numbers.

**Fixed: SheepForce lines hook.** QuickDraw hook code 5 (lines, on by default) filled the bounding box of a diagonal
`LineTo`, so a diagonal line came out as a filled rectangle. QuickDraw's own drawing of the same line in an offscreen
GWorld left white where the screen had the line colour. `NQD_lines_hook` now takes only rectangles one pixel high
or wide (frames and rules) and leaves everything else to the ROM. After the change, LineTo matches at all three
sizes and FrameRect still reaches the hook.

**Not fixed: stale code after a rewrite (JIT).** The MMU test rewrites a two-instruction routine (`li r3,n; blr`),
calls `MakeDataExecutable` on it and calls it, tens of thousands of times. With the JIT on, about 2 calls in 20,000
to 80,000 return the previous value (for example, call 1964 returned 1963). With the JIT off it never happens. This
reproduced in each of four runs, with and without the CPU suite before it. The reproducer is
`tools/sheepbench/test.sh switch SUITES=mmu`. The JIT invalidates the page on `icbi`
(`powerpc_cpu::jit_host_icbi`), so the stale block must be reached some other way: a block link or a lookup that
does not see the invalidation. This is not yet investigated.

**SheepForce Metal is not offered to SheepBench.** RAVE's engine list in SheepBench holds only Apple's software
engine, and a QuickDraw 3D scene runs at the same speed whether or not SheepForce Metal is asked for. The emulator
registers its engine once (`SheepForceRaveRegisterNative`), from the application that is current at that moment.
The inferred cause, not proven: the RAVE manager keeps its engine list in per-application CFM globals, so other
applications never see the registration. Engines that ship as extensions register in every application that loads
the manager.

**GPU vs CPU QuickDraw.** With SheepForce QuickDraw on, small fills are much slower than on the host CPU. PaintRect
and InvertRect at 64 x 64 run at about 8,000-9,000 a second with the GPU, against about 200,000 with it off (25x).
At full screen the GPU wins: PaintRect 2x, a gray pattern FillRect 7x. FrameRect and ScrollRect are slower on the
GPU at every size. CopyBits, text and icons show no difference (they do not reach the GPU path, or reach it the
same way).

**Invert and the unused byte.** SheepShaver's native (host CPU) InvertRect flips all 32 bits of a 32-bit pixel.
QuickDraw leaves the top byte, which is unused and never shown. The QuickDraw check ignores that byte.

**Other figures.**
- 68k code runs at about 25 ns per 68k instruction with the host 68k JIT.
- Loads one per page over 4096 pages (16 MB) cost 38 ns, against 1.4 ns within one page. That is the emulator's
  translation miss, beyond its TLB.
- The AltiVec loop runs at 20 M floats/s, slower than the same loop in C (54 M/s), and gains only 1.3x from the JIT.
- BlockMove runs at about 200 MB/s for every size, against 2-4 GB/s for memcpy and the 8-byte loop, and gains
  only 1.5x from the JIT.

## Tests

- `tools/shears/test.sh unit` covers the payload decoding, the report assembly and the JSON conversion.
- `tools/sheepbench/test.sh [switch|noswitch|corrupt|nosig|all] [SUITES=cpu,memory,...]` boots a scratch copy of the
  test disk without a window, with SheepBench in Startup Items and a "SheepBench Auto" file in the System Folder. It
  then checks the report:
  - `switch`: every row ran or says why it did not; % native is filled; JIT-off values are there and the CPU kernels
    are faster with the JIT.
  - `noswitch`: no JIT-off values when the setting is off.
  - `corrupt`: `NW_BENCH_CORRUPT=1` makes the host's checksums wrong, and the kernel rows must turn FAILED. This shows
    the checks bite.
  - `nosig`: `NW_BENCH_NO_SIG=1` hides the signature. The app must never call the host, must report it as unknown,
    and writes its report to the disk and shuts the guest down.
