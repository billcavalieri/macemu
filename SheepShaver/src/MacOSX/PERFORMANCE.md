# Where the emulation time goes, and what was done about it

Measured on an Apple M4, Mac OS 9.2.1 guest, 512 MB, `sheepforce true`, `jit true`. Everything here is host CPU
seconds of the VM process (`ps` cputime), not wall time, because a guest that is waiting costs nothing.

## How to measure (all in `tools/perf`)

| tool | what it gives |
|---|---|
| `workload.py OUTDIR [--app A] [--label L] [--sample] [--no-profile]` | boots a clone of the test disk and measures three phases through the VM's own control socket (nothing is typed or clicked on your Mac): **boot** (power-on to the QuickDraw hooks line), **gui** (the same scripted Finder activity every run: menus, new windows, dragging, text entry, window closes) and **idle** (the desktop doing nothing). Writes CPU seconds, a profile dump per phase (unless `--no-profile`) and, with `--sample`, a host `sample` per phase. |
| `ab_workload.sh OUTDIR RUNS "label\|app\|ENV…" "label\|app\|ENV…"` | alternating A/B of two builds (or one build with two environments). Judge a change on this, never on a sampling profile: a sampling profile once overstated a change tenfold. |
| `jit_report.py PROFILE` | the inventory below: instruction mix, C helpers (exact call counts), interpreter fallbacks, block shape and exits, hottest blocks, code-cache upkeep, why block exits fell back to the C chain helper |
| `sample_summary.py SAMPLE` | host time by function and by area for the emulation thread |

`NW_JIT_PROFILE=<file>` turns the profiler on in any build (every compiled block counts its own executions; every helper call
site counts its calls; `<file>.now` dumps, `<file>.reset` zeroes the counters; the `.prof` files are large, the text reports
are in `tools/perf/results/`). It costs about 1%.

Run-to-run noise on the same binary is about ±1.5% on boot and ±3% on the gui phase (the gui phase sometimes lands in a
regime where the code cache thrashes, see below). A change was kept only if it was neutral-or-better in alternating runs of
the same binary with the change switched off (`NW_JIT_LEGACY=<name>`), and the guest still booted and the test suites
passed.

## Before (the inventory)

`tools/perf/results/before.*.txt` (guest instruction mix, helpers, blocks) and `before.host-*.txt` (host time).

* **The interpreter is not a factor.** 0.0006% of guest instructions run in it (≈11,000 in a boot). Everything runs in
  translated blocks. There is no "falls out of the JIT" problem; the cost is what the translated code and its
  surroundings do.
* **Host time, emulation thread** (boot / busy desktop / idle): the host 68k layer (`nw68_*`, which executes 68k
  instructions natively on behalf of the guest's 68k emulator) 49% / 26–29% / 25–32%; generated PPC code 13–20% / 21–26% / 20%;
  block-to-block dispatch (chain helper, cache and TLB lookups, register copies) 7–11% / 11–14% / 12%; code-cache
  upkeep (compile, compaction, icache invalidation) 4% / 12% / 3%; MMU 4–6%.
* **Instruction mix** (busy desktop): `bc` 17%, `addi` 13%, `lwz` 11%, `cmpi` 7%, `b` 6%, `cmpli` 5%, `add` 5%, `stw` 5%; 54% of
  executed instructions contain a call into C (almost all of it the data-TLB miss path and real-mode accesses). Average block
  length 3.6–4.5 instructions: 58% of block executions are 1–4 instructions long.
* **C helpers** (busy desktop, 179 million calls in 40 s, 0.09 per guest instruction): the chain helper 28%,
  real-mode loads and stores 42% (the NanoKernel runs with translation off and every access was a call), the system service
  (mfspr/mtspr of SPRG/SRR, sync, twi, rfi) 14%, the rest small (`stmw`/`lmw` 4%, `lvx`/`stvx` 3%, `sraw`, `bc`).
* **Block exits**: 36% of the exits of a busy desktop took the slow C chain helper instead of jumping to the successor. Causes,
  exact (busy desktop, before): successor needs a register that is not yet copied into the JIT's state 36%,
  computed target (blr/bctr) not in the indirect-target cache 34%, never linked 13%, link invalidated by a store
  into translated code 13%, hop budget 12%.
* **Instruction TLB**: it was emptied on every privilege or translation switch (1.5 million flushes in a boot,
  7 million in 40 s of desktop): 21% of lookups missed in a boot, 56% on a busy desktop.
* **Code cache**: a busy desktop made 800,000 translations in 40 s because the 64 MB arena (≈1 KB of host code per
  translated block; `lwz` alone is 94 instruction words with both slow paths) was full of live blocks and was compacted 200
  times, copying 12.7 GB, and invalidating the instruction cache over all of it twice each time.
* **Debug leftovers in the Release path**: a diagnostic scan of all 512 MB of guest RAM (0.4 s of every boot, three passes
  of two `memmem` scans), `dcbz` clearing a 32,768-entry table every time, a call out of every translated
  kernel wake-up path that does nothing in Release (20,000 a second idle), a counter increment (load, add, store through a
  pointer) on every guest load and store.

## What changed, in order of measured value

Every item has a kill switch (`NW_JIT_LEGACY=<name>`, a comma list; `all` for every one of them) so the old behaviour can be A/B'd in
the same binary. Numbers are the change from the alternating A/B (median CPU seconds, boot / gui / idle).

| change | switch | effect |
|---|---|---|
| Instruction TLB per context (IR, PR) instead of one table emptied on every switch | `itlb` | −3.9% / −3.9% / −4.9% ; ITLB flushes 1.5 M → 14 k per boot, miss rate 21% → 3.5% (boot), 52% → 8% (desktop) |
| Interpreter and JIT share one general-register file (no copy in at the start of a chain, none out at its end, no walk down the chain to find which registers to copy) | none (structural) | slow exits for "register not copied yet" 38 M → 0.5 M in 40 s (the first big drop of the boot, together with the 68k row) |
| 68k dispatcher: cached blocks run in one call (no copy of the decoded block, no re-hash, one lock instead of two), frames and bus contexts no longer zero-filled (≈1.5 KB per dispatch), the per-page "is plain memory" answer memoised, the page-translation cache inlined, no byte-by-byte loop for an access inside one page | `d68`, `xlate` | −6% boot (no per-item flag for the zero-fill and memo parts); the fused call itself −1.5% |
| Real-mode (MSR[DR]=0) loads and stores served inline from a physical-page cache (RAM and ROM; stores only to RAM pages that hold no translated code) | `real` | −1.2% / −0.2% / −3.1% ; 200 M calls removed in 40 s |
| Code arena: free a quarter of the arena per compaction instead of one bank; one instruction-cache invalidation per translation and per compaction instead of two | `arena` | −1.0% / −2.4% / +1.1% (noise) ; compactions in a busy desktop 203 → 140, copied bytes −36% |
| A store into translated code invalidates only cross-page links and cached targets, not every direct link in the cache | `epoch` | links re-made 12 M → 4 M in 40 s; chain-helper calls −30% |
| `sync`, `eieio`, `dcbst`, `dcbf`, `dcbt`, `dcbtst` inline (barrier or nothing); supervisor `mfspr`/`mtspr` of SRR0/SRR1/SPRG0–3 inline and no longer end the block | `sysnop` | −1.2% / +0.4% / −4.6% ; system-service calls −45% |
| Data-TLB hit counter no longer in generated code (4 instructions and a store-to-load dependency on every guest load and store) | `dcount` | with `arena`: −1.0% / −2.4% |
| Diagnostic RAM scan off unless `NW_AUDIO_SCAN=1`; `dcbz` no longer clears an empty block cache; kernel wake-up log call not emitted in Release; `getenv` out of the link path | – | ≈ −2.5% of a boot together |

Also changed: the link hop budget 31 → 255 (`NW_JIT_BUDGET=<n>`; −0.3% / −1.4% / −3.3% in five alternating runs).

Cumulative, host CPU seconds, medians of alternating runs (boot / busy desktop / idle desktop, 15–20 s):

| build | boot | busy | idle |
|---|---|---|---|
| before (profiler only) | 25.97 | 29.38 | 6.16 |
| + code arena, no hit counter, real-mode cache, 68k dispatch, shared registers | 22.4 | 27.7 | 6.9 |
| + link epoch, inline system ops | 21.6 | 27.0 | 6.7 |
| + instruction TLB per context | 19.9 | 26.4 | 6.8 |
| + hop budget, 68k stats off, the rest (**final**) | **19.73** | **25.73** | **5.80** |

Final five-run alternating A/B, original build against the final build: **boot −24.0%, busy desktop −12.4%, idle −5.8%**.
(The intermediate rows come from separate A/B sessions; the machine's absolute speed drifts a little between sessions,
so only the first and last rows are directly comparable.)

Host-time shares after (`tools/perf/results/after.host-*.txt`): boot — 68k layer 48%, generated code 22%, dispatch 4%
(was 7%), code-cache upkeep 3.8%; busy desktop — generated code 35%, 68k layer 33%, dispatch 5.7% (was 11%), code-cache upkeep
5% (was 12%).

Tried and rejected: a 128 MB code arena (−2.2% on the busy desktop, but +64–128 MB of resident memory in the cases that
need it); dropping a segment's TLB entries eagerly on `mtsr` instead of comparing a generation on every access (neutral:
the NanoKernel rewrites all 16 segment registers 280 times a second, so the scan costs what the compare saved).

## What was checked

`tools/run_ppc_core_tests.py` (1,013,888,544 checks; one assertion about the old register-copy bookkeeping and one about the
hop budget were updated), its link, sub-word-store, FP-fast and raw-file-read sweeps, `tools/run_nw68_tests.py` (143,361), and the guest
tests `tools/shears/test.sh install|extfs` and `tools/vms/test.sh control|nvram|display|settings|manager` on the final build, each
booting a clone of the disk. `tools/vms/test.sh embedded` needs the display awake (it screenshots the library window) and was not
re-run on the final build; run it with the screen on before committing.

## What the profile says is left (ranked by host time, busy desktop)

1. **The 68k layer, ≈30% (≈47% of a boot).** About 120 ns per 68k dispatch: the page-translation probe for the code
   fetch and for each operand, `nw68_prepare`/`nw68_finish` per instruction (every 68k instruction is two C calls and a
   third for block progress), snapshot/restore of the NanoKernel's registers around each dispatch, and the PPC
   interpreter loop's own checks between dispatches. Ideas, in order of expected value: translate the common 68k
   register/ALU instructions inline instead of calling `prepare`/`finish`; run consecutive native blocks inside one
   dispatch (decode state stays in the 68k structure, one snapshot at the start and one restore at the end); skip
   `compatible_nk()`'s eight loads per dispatch with a generation check.
2. **Generated PPC code, ≈25–32%.** Guest registers live in memory: every ALU instruction loads its operands and stores its
   result (four of them are cached in host registers within a block). 1–4 instruction blocks are 58% of executions, so the
   per-block exit (≈20 instructions for a linked exit, 65 words of code each) is a large share. Ideas: keep more guest
   registers in host registers across a block and write back at exits; split memory-access slow paths out of line so the
   hot code is denser (a load is 94 words, about a third of them executed); superblocks across unconditional branches.
3. **Block-to-block dispatch, ≈7%.** Still 36 M slow exits in 40 s of desktop: indirect target cache misses (13 M; the cache
   is invalidated by each of the 11,000 NanoKernel address-space switches in 40 s, which rewrite all 16 segment registers
   alternating the VSID's low bit), exits into the 68k handler table (8 M, by design), hop budget (20 M; raising it from 31
   to 255 was neutral in A/B).
4. **Code cache upkeep, ≈5% (a bad run: 12%).** ≈1 KB of host code per translated block is the root cause: the
   working set of a Finder window does not fit in 64 MB, so blocks are compiled, evicted and compiled again. Smaller
   blocks help everything above.
5. **Idle desktop, ≈45% of a core.** Not the emulator spinning: the guest itself runs ≈9 million instructions a second
   with the machine "idle" (7,000 NanoKernel traps a second, an exception path entered 14,000 times a second), and sleeps
   only about 100 times a second. Worth looking at in the guest (which task, and what the shared-folder volume costs it).
6. Smaller: `nw_jit_helper_system` for `rfi`, `twi`, VRSAVE and `mfspr` of the remaining SPRs (≈20 M calls in 40 s),
   `stmw`/`lmw` (4 M each), `sraw`/`srawi` (12 M in a busy desktop), `lvx`/`stvx`, write-protect toggles (10 M links
   made before the epoch change; 4 M now).
