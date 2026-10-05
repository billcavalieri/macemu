# SheepShaver NW JIT — opcode checklist

Generated **2026-10-05** from `powerpc_ii_table` in `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp` (345 names) against `nw_jit_op_supported` / `nw_jit_op_ends_block` in `SheepShaver/src/nw_jit.cpp`.

Regenerate: `python3 tools/gen_jit_ops_md.py` (ports `nw_jit_op_supported` against `powerpc_ii_table`).

## Status legend

| Mark | Meaning |
|------|---------|
| `[x]` | **accepted** — sampled encodings pass the allowlist, including applicable OE/Rc combinations; independent conformance is tracked above |
| `[~]` | **partial** — sampled form rejected (OE, Rc, RA, BO, AA) or an explicit execution/form caveat remains |
| `[ ]` | **todo** — not in `nw_jit_op_supported` (falls to interpreter / `skip_unsup`) |
| `[-]` | **exclude** — do not mill |

**Wave** follows the mill-everything plan (W1 hot partials → W7 AltiVec FP → W8 sweep).

The generated opcode marks describe dispatch coverage. Acceptance does not establish architectural correctness, independent verification, direct ARM64 emission, or unrestricted chaining. The separate qualification ledger records that work.

<!-- PPC-JIT-REVISIT-BEGIN -->
## Revisit and qualification checklist

Updated October 3, 2026. Generated rows below describe accepted encodings; this ledger tracks implementation, independent correctness and production restrictions separately. Regeneration preserves this section. The implementation evidence is in [NW-PPC-JIT-PLAN.md](NW-PPC-JIT-PLAN.md).

- [x] **P1 — OE encoding inventory:** the generator now probes OE=0/1 × Rc=0/1 for every applicable XO form. Its Python support model agrees with the actual C allowlist for 6,555 sampled encodings across 345 names. This is policy parity, not ISA conformance or exhaustive encoding validation.
- [x] **P1 — Missing overflow forms:** implemented `nego`, `addmeo`, `addzeo`, `subfmeo`, `subfzeo` and their record forms. Literal result/XER CA/OV/sticky SO/CR0 checks cover incoming flags, r0 sources, source/destination aliases, KPX/local-C/ARM64/private replay and mixed production native/C chains with pending DEC stops. Debug and Release initially pass 673,364 core checks; four MMU runs each pass 34,585. The subsequent vector snapshot includes these changes and passes the cloned-guest integration run below; guest comparison counts are not per-opcode coverage.
- [ ] **Shared instruction metadata:** replace the separately maintained Python/C policies with one descriptor for encoding constraints, masks, execution class, effects and generated coverage/tests. Thirty-five VMX fixed-zero constraints are now shared, with automatic C/Python policy comparison; the whole-ISA descriptor remains open. Keep accepted names and architectural qualification separate.
- [x] **P2 — Selected integer VMX and vector memory replay:** 41 named integer/permutation/VSCR forms and 12 vector memory/shift-mask forms now use private state and ordered observations. Fixed missing sticky SAT updates in `vsumsws`, `vmsumshs` and `vpkswss`. KPX/local-C/ARM64/replay comparisons cover all 15 operand-alias topologies, adversarial lanes, SAT/CR state, logical aliases, permission/SMC exits, destructive devices, malformed tapes and selected integer/vector chain transitions. Debug/Release with vector inlining off/on each pass 6,749,555 core checks plus three 205-check captures; four MMU runs each pass 34,585. The cloned guest completes 300 desktop seconds with 549.3 million PPC block comparisons and zero mismatches, 16 mouse targets, 20 interrupts and a shared-file write. Per-opcode qualification comes from the host fixtures, not the aggregate guest count.
- [x] **P2 — Remaining integer VMX delegation:** replaced live-CPU delegation for 83 more integer names (90 forms including record variants) with private, staged register execution. All 131 integer/permutation/VSCR forms now pass KPX/local-C/ARM64/replay comparisons across 15 alias topologies and 16 lane patterns. Added 28 literal ISA boundary cases, all 2,048 decoder-slot classification checks and adversarial host-callback isolation checks. Four Debug/Release/vector-inline configurations each pass 37,923,491 core checks and three 205-check captures; four MMU runs each pass 34,585. ASan/UBSan private-kernel stress passes 132,480 executions. The cloned guest completes 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-file write with 538.2 million PPC block comparisons and zero mismatches; this aggregate count is integration evidence.
- [ ] **P2 — Broader VMX qualification:** all recognized integer/vector-FP calculations now execute privately. Fixed-zero fields, full immediate domains, selected vector-unavailable/program/DSI entry and pending-event priorities now have independent evidence. Selected instruction-fetch faults and context changes now have evidence. Broaden operand/undefined-input profiles, other exception priorities, and additional fault/chain combinations. Dispatch/replay coverage does not certify all VMX encodings or CPU profiles.
- [x] **P2 — Private vector FP execution:** replaced live delegation for 22 names / 26 forms; repaired KPX vector RN/environment, NJ, NaNs, signed zero, fused operations and saturating conversions. All forms pass alias/host-RN/scalar-RN/NJ/SAT comparisons, supplemented by 26 literal FP cases, sampled independent estimate accuracy/monotonicity, callback isolation and mixed production chains. Four Debug/Release/vector-inline configurations each pass 57,864,241 core checks, three 205-check captures and 34,585 MMU checks. ASan/UBSan passes 168,960 FP plus 132,480 integer kernel executions. The cloned guest completes 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-file write with 544.3 million PPC comparisons and zero mismatches; the aggregate count is integration evidence.
- [x] **P2 — VMX encoding/immediate and availability boundaries:** reject decoder-invalid/malformed primary-opcode-4 words before acceleration; share 20 fixed-zero constraints with New World decode and the generator. Added 396 full-domain immediate cases, 106 single-bit malformed forms and 336 actual CPU-loop exception fixtures with prefix/chain and unavailable-before-data-access checks. Four host configurations each pass 59,985,581 core checks, three 205-check captures, 196,608 C/Python policy probes and 34,599 MMU checks. Cloned-guest repetition passes 300 desktop seconds, 543.7 million PPC comparisons with zero misses, a shared-folder write, 16 mouse targets and 20 injected interrupts. The first attempt's failed scripted file action is retained in the plan.
- [x] **P2 — Further VMX exception/event priorities:** illegal primary-opcode-4 encodings now take program exceptions with VEC disabled; every DSI increments the exception counter so isolated replay cannot execute a cached suffix at the handler vector. Expanded to 1,200 synchronous fixtures, 360 pending-event/DEC-write boundaries, 144 streaming/VRSAVE controls and two DSI-vector/next-PC alias regressions. Four configurations pass 60,263,131 core checks and 34,600 MMU checks each. The cloned guest passes 300 desktop seconds with 544.2 million PPC comparisons and zero misses, 16 mouse targets, all 20 injected interrupts and a shared-folder write.
- [x] **P2 — Instruction fetch and opcode-31 VMX boundaries:** invalidate instruction translations when IR or PR changes through interpreter/native MSR writes, returns and exception entry. Added 2,160 fetch-fault fixtures, 60 warmed-cache transitions, 1,488 malformed opcode-31 fixtures and 1,176 valid recognition controls. Fifteen additional shared encoding constraints reject malformed AltiVec memory/streaming forms in New World decode, policy, local C and emission. Four configurations pass 61,015,602 core checks, 393,216 policy probes and 34,664 MMU checks each. The cloned guest passes 300 desktop seconds with 537 million PPC comparisons and zero misses, 16 mouse targets, all 20 injected interrupts and a shared-folder write.
- [ ] **P2 — Further vector FP conformance/acceleration:** broaden finite/exceptional operand cross-products and undefined-input/exception-priority profiles; qualify hardware estimate profiles and independent NEON FP paths. Conversion immediate domains and selected fixed-zero boundaries now have evidence. Current ARM64 FP blocks call the portable kernel; qualify and measure any new inline path before optimizing masks/residency.
- [x] **P3 — Current `vslo`/`vsro` block qualification:** removed the special block-ending restriction, retaining private alias-safe helpers. Added 23,040 dependent-block cases over every control byte and D/A/B alias partition, 128 cold-builder boundary cases, 128 precise DSI cases and 256 production chain/event cases. Four configurations each pass 74,346,035 core checks and 34,664 MMU checks. Separate current ON and VERIFY guests each complete 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-folder write; VERIFY has 540.2 million PPC comparisons and zero misses. Removal is qualified by current-code evidence; it is not a claim of a historical causal repair.
- [ ] **Historical octet-shift lock attribution:** the September 19 snapshot with only the restriction removed reaches Finder; its restricted control does not within the same observation period. The original unrestricted-block lock is not reproduced or causally explained. Preserve that question separately from the now-qualified current implementation; see the plan for source diffs, hashes, screenshots and limits of this single comparison.
- [x] **P4 — Selected privileged/system observations:** typed services now support private replay for SPR/MSR, segment/BAT, `rfi`, traps/`sc`, TB/DEC, TLB/cache/synchronization operations and aligned `dcbz`. Independent operands, privilege, PC/MSR/exception state and instruction boundaries are checked; external services execute once. Added 2,244 literal system cases, 30 whole-line/cache-owner cases, nine malformed-tape guards and a version-3 trap capture. Four configurations each pass 75,043,992 core checks and 34,664 MMU checks. Final ON/VERIFY guests each complete 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared-folder write; VERIFY has 521.4 million PPC comparisons and zero misses. Independent hardware/MMU service and broader CPU-profile conformance remain open.
- [x] **P5 — Current branch successor qualification:** conditional and LR/CTR transfers now use actual-PC successors on both native and C paths, preserving frame-pop, state masks, availability, MMU/cache and pending-event gates. Added 81,920 literal successor cases, 96 address/link replay cases, 72 mixed-state/gate cases and 2,000 bounded indirect loops. Three stale-vector negative controls reproduce corrupted pixel stores; three repaired controls pass. Host and cloned ON/VERIFY qualification is recorded with P4 above; original full-guest incident attribution is separate. No measured performance gain is claimed.
- [ ] **Historical branch graphics incident attribution:** current negative controls reproduce the stale-vector-copy mechanism documented in `1cef33b4`; they do not replay the original full guest black-frame incident. Preserve exact historical reproduction and causal attribution separately from qualified current chaining.
- [x] **P6 — Selected scalar conversions, comparisons and bit-preserving moves:** repaired `fctiw`/`fctiwz` rounding/range, FR/FI, invalid/inexact causes and enabled-invalid result suppression; qualified ordered/unordered NaN comparison status and selected precise/imprecise exception publication. Literal fixtures cover 319,488 conversion, 36,864 comparison, 1,040 move/select and 1,656 production entry/commit cases. Eight Debug/Release × FP/vector-inline configurations each pass 89,316,271 core checks, five capture replays and 34,664 MMU checks; policy and kernel sanitizer checks pass. Latest ON/VERIFY clones each pass 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared write; VERIFY passes 508.7 million PPC comparisons with zero mismatches. Final evidence and retained failed qualification runs are in the plan.
- [x] **P6 — Raw file-read code publication:** completed `Sys_read` bytes now invalidate both PPC and host-68k code owners for direct guest buffers. Eight real-file ON/VERIFY cases cover full/short reads, page crossings, untouched neighbors and EOF/host-buffer controls; 124 checks pass and the old backend fails 12 publication assertions. This qualifies the selected synchronous raw-read path.
- [x] **P6 — Round to single (`frsp`):** implemented exact NaN projection/quieting and VE suppression, all RN modes, FR/FI/sticky causes/FPRF, enabled +/-192 exponent adjustments and precise block stops. Independent numeric/integer-significand engines pass 248,832 literal cases including 82,944 production cases. Eight host configurations each pass 139,445,268 core checks, all five captures, policy/sanitizer checks and 34,664 MMU checks. Same-binary ON/VERIFY clones each pass 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared write; VERIFY passes 520 million PPC comparisons with zero mismatches and clean final Finder graphics. Generated ARM64 uses the complete kernel in both FP-inline settings. Broader arithmetic and performance remain separate.
- [x] **P6 — Basic arithmetic special results:** repaired all eight existing add/subtract/multiply/divide forms for full-payload A/B NaN priority, signaling/generated invalid causes, VE/ZE destination/FPRF suppression, finite/zero ZX and infinity/zero controls, exact special FR/FI and signed zeros. Valid finite FPRF with VE and selected FE/pre-existing-status instruction stops now agree across reference/helper/inline paths. Independent fixtures cover 2,525,184 engine cases including 841,728 production cases; the bounded old-helper control fails 1,736 assertions. Eight host configurations each pass 648,133,797 core checks, five captures, policy/sanitizer suites and 34,664 MMU checks. Same-binary ON/VERIFY clones each pass 300 desktop seconds, 16 cursor targets, 20 ordered interrupts and a shared write; VERIFY has 524.7 million PPC comparisons and zero mismatches, with clean Finder graphics. Finite FR/FI/OX/UX/XX/adjusted results remain open; no performance gain is claimed.
- [x] **P6 — Finite multiply rounding/status:** exact 106-bit native product and independent normalized numeric/FMA-residual reference now round `fmul`/`fmuls` once, replace FR/FI, record XX/OX/UX, model pre-rounding tininess, gradual/saturated disabled results and enabled +/-1536 or +/-192 exponent adjustments, and stop at selected FE exceptions. Finite single operands must be representable as singles; other inputs retain the prior undefined extension profile. Offline rational literals pass 1,815,552 engine cases including 605,184 production cases, plus 4,512 ASan/UBSan kernel executions. Eight host configurations each pass 1,013,886,526 core checks, five captures, policy/sanitizer suites and 34,664 MMU checks. Same-binary ON-repeat/VERIFY clones pass 300 desktop seconds, 16 cursor targets, 20 ordered interrupts and a shared write; VERIFY has 522.1 million PPC comparisons with zero mismatches. The first ON single-name confirmation failure, repeated full-name observations and clean final Finder screens are recorded in the plan. Generated multiply calls the complete kernel in both FP-inline settings; performance and other arithmetic remain separate.
- [ ] **P6 — Remaining scalar FP conformance:** complete finite add/subtract/divide sticky causes/FR/FI and enabled overflow/underflow results, fused final-rounding/NaN/exception behavior and estimate profiles. Selected conversion/comparison, `frsp`, basic special results and finite multiply have separate milestones; the remaining finite/fused policy is incomplete.
- [ ] **P6 — Basic arithmetic reference/native repair:** complete finite add/subtract/divide in both precisions: single rounding, FR/FI replacement and new OX/UX/XX, adjusted overflow/underflow results and selected FE block stops. NaN/invalid/ZX, result suppression and valid FPRF have a separate special-result milestone; finite multiply is qualified above. Repair reference and helper/inline paths together; enabling legacy flag tracking alone is insufficient.
- [ ] **P6 — Optional square-root CPU support:** `fsqrt`/`fsqrts` currently have no KPX decoder entry or JIT implementation. Eight current rejection/illegal-instruction controls pass. Define the intended optional instruction profile, implement the decoder/reference/native paths and qualify NaNs, negative inputs, signed zero, rounding/status/enables and precise instruction stops before accepting these opcodes.
- [ ] **Source publication incident and broader host writers:** capture the full originally cached source and writer/publication trace for exact attribution of the retained cached-NOP/current-branch incident. Fresh-source capture replay passes but does not reproduce the old block. Audit remaining raw host/DMA writers and noncontiguous buffer policy separately from the repaired `Sys_read` path.
- [ ] **P6 — Wide memory/invalid forms:** audit scalar, FP, vector and cache-line accesses across page/bank boundaries, noncontiguous physical mappings and partial fault/SMC completion. Keep valid encoding expansion separate from illegal/update-register/64-bit forms outside the modeled CPU; check their fallback/exception policy.
- [x] **Strings:** all four supported string forms have isolated replay, protected/noncontiguous page-boundary and SMC-prefix coverage. Broader architectural invalid-form policy remains in the memory audit.
- [x] **Aligned single-CPU atomics:** `lwarx`/`stwcx.` have typed translation observations, physical aliases, private reservations, device/ROM handling and selected native/C chain evidence.
- [ ] **Atomic remaining scope:** independently audit alignment, reservation granules and invalidation by DMA/other agents, physical-bus faults and the intended CPU model. Do not claim SMP coherency from the existing single-CPU tests.
- [ ] **Independent validation infrastructure:** independently replay translations, capture complete originally cached source, automatically reduce failures, and broaden production selective-mask/chain exception combinations.
- [ ] **External-control exclusions:** revisit `eciwx`/`ecowx` only with an EAR/external-control device model and permission/alignment/exception tests or measured guest demand. KPX currently uses no-ops and JIT fallback; simply allowlisting them is not an implementation. Keep `invalid` excluded with correct illegal-instruction delivery.
- [ ] **Performance and cache qualification:** correct retirement/dispatch accounting; measure repeated serial Release interpreter/JIT workloads with identical cloned media; qualify remap/SMC/compaction/lifetime and long event/nap stress. Prioritize inlining and additional chains using measured helper/dispatch costs.

### Host performance (measured; added October 4, 2026)

Origin: a host `sample` of the Release app during an iTunes window raise (SheepForce fill hook on, drawing already accelerated) showed the emulation thread 31.8% idle, SheepForce hooks 1.4% and Metal about 0.1% of the thread. Of its active time: JIT<->interpreter state sync (`nw_pull_gpr`/`nw_commit_gpr`) 16.9%, JIT-generated guest code 15.2%, dispatch/chaining 14.5%, MMU translation 13.7%, code invalidation 8.8%, clock reads 8.4%, 68k layer 7.4%. The remaining iTunes "items appear one by one" lag is therefore JIT overhead, not drawing. Recipe: run the app, `sample <pid> 25 1 -file out.txt`, then aggregate self-time per thread from the call graph (the emulation thread is the one in `emul_thread_main` -> `powerpc_cpu::execute`).

- [x] **Decrementer sampling at chain exits:** `powerpc_cpu::jit_events_pending()` read the host clock (`sample_decrementer()` -> `tb_host_ticks()` -> `GetTicks_usec()`) on every native chain exit (`jit_host_chain`) and every C hop; that was 975 samples, 5.8% of the emulation thread. It now samples every 16th call and not at all while a decrementer request is already pending (`jit_dec_div_`, `kJitDecSamplePeriod` in `ppc-cpu.cpp`). The delta added to DEC is the full elapsed host time, so DEC values are exact; every guest `mfspr DEC` still samples fresh (`ppc-cpu.cpp` line ~1116), and only the notice of an expiry edge can arrive a few block exits later (the interpreter path already samples every 256 instructions). Evidence: the PPC core suite (`tools/run_ppc_core_tests.py` on a full Release build log) exits 0 with output identical to the pre-change baseline; a same-workload boot profile of the old and new Release builds shows clock reads falling from 4.3% to 0.7% of the emulation thread's active time (635 -> 104 samples). Not established: a wall-clock speedup for iTunes (needs the controlled serial off/on trials below) or behaviour under heavy DEC-driven preemption beyond the existing tests.
- [x] **State sync loops at chain exits (partial):** `nw_commit_gpr`/`nw_pull_gpr` and the FPR pair walked all 32 registers with a test per register at every chain exit; they now visit only the set bits (`__builtin_ctz`), with the same loads, stores and ascending order (`ppc-cpu.cpp`). Evidence: the PPC core suite exits 0 with output identical to the baseline; in same-workload boot profiles the sync samples relative to the dispatch samples (both scale with chain exits) fell from 0.96 to 0.35. Still open: the work itself (committing every live register and pulling the successor's at each exit) remains; the larger win is keeping chained blocks' registers live across the exit or committing only what the successor does not use, which needs an exception/SMC argument at the exit and replay coverage.
- [x] **Tried and reverted: skipping the per-hop GPR commit (October 5, 2026):** `jit_host_chain` copies every live JIT register back to the interpreter state on each native chain hop (`nw_commit_gpr`, 452 of 2,420 `jit_host_chain` samples in a boot profile, about 3% of the emulation thread), although `nw_jit_try()` commits all live registers once when the chain returns on every path. An audit found no reader of a stale GPR between hops: `nw_pull_gpr` loads only non-live registers, `jit_host_vmx` commits and pulls all 32 around its own work, `take_program`/`take_sc`/`take_exception`/`mtspr_guest` read no GPR, `programint_kcall_fast` runs after a commit, and `nw_68k_reference_chain` takes the live copy. It was implemented with a poison mode (overwrite the interpreter's copy of each live GPR instead of committing). The PPC core suite passed in default, poison and legacy modes with output identical to the baseline; a deliberately wrong poison (the non-live registers) failed 1,411 checks, so the tests do notice stale data; three poisoned live boots reached a correct Finder with the self-test passing. It was reverted because the same-binary A/B of CPU time to the late-boot line showed no gain: 38.59 s with the per-hop commit against 38.42 s without (-0.4%, within noise). Lesson recorded here: a sampling profile overstated this cost roughly tenfold (the work mostly moves to the final commit), so confirm every candidate with a CPU-time A/B before keeping it. The remaining state-sync ideas (dirty-register tracking in the emitted code, keeping registers live across chained blocks) would need emitter changes and are not expected to be worth their risk.
- [ ] **Dispatch and lookup per chain exit:** `jit_host_chain` also does `nw_jit_itlb_lookup`, `nw_aline_dispatch_pa`, `nw_jit_cache_get` and `nw_68k_reference_chain` for every exit (dispatch category 9.9% of the thread). Cache the successor resolution on the exiting block when MSR IR/DR/PR, the ITLB entry and the page generation are unchanged.
- [x] **MMU translation for byte/halfword loads:** `jit_host_lh`/`jit_host_lb` (reached from `nw_jit_helper_lh/lb`) re-ran `ppc32_mmu::translate` on every access because, unlike `lwz`, they never filled the data TLB and `nw_jit_helper_lh/lb` had no host-line hit path (203 + 118 translate samples in the iTunes profile). They now fill the table exactly as `jit_host_lwz` does (no framebuffer pages, DR on, writable flag, BAT provenance) and the helpers try `dtlb_host_line` first; a halfword at page offset 0xfff, which crosses the host line, still takes the translating path. Stores are unchanged (SMC, framebuffer damage and invalidation stay on the existing path). Test fallout, fixed: the PPC core harness rebuilds BATs directly on the MMU between iterations (`mmu.reset(); mmu.set_dbat(...)`), which relied on byte/halfword loads never populating the table; it now calls `nw_jit_dtlb_flush_src(NW_JIT_DTLB_FL_BAT)` after those six BAT setups, as a guest `mtdbat` would announce. Every production BAT, SDR1, segment and `tlbie`/`tlbia` path already notifies the table (`ppc-cpu.cpp` mtspr paths, `execute_mtsr/mtsrin`, `jit_host_tlbie/tlbia`, `nw_jit_mtsr_note`). Not yet done: the same treatment for stores and for the remaining translate callers (`guest_data_probe` from the 68k layer, `guest_fetch`).
- [x] **Per-page index for code invalidation:** `nw_jit_invalidate_page_src` scanned all 262,144 cache slots whenever a store or `icbi` hit a page flagged as possibly holding translated code. Instrumented over a ~100 s Release boot: 170 million invalidate calls, 23,651 of which scanned (17,053 from JIT stores, 5,641 from `icbi`), 7.5 s in total (~320 us per scan) to kill ~5.6 blocks per scan. `g_page_slots` now keeps, per physical page, the slots created for that page (`page_slots_add` from `put`); an invalidation walks that list, tombstones the slots still live for the page, clears the list, and clears the page bit as before (`nw_jit.cpp`). The lists are append-only and lazily validated, so slots tombstoned or reused elsewhere (bank invalidation, compaction, eviction) are just skipped, they are compacted when they grow past a threshold, and they are cleared by `nw_jit_reset`, `nw_jit_set_code_pages` and `nw_jit_invalidate_all_src`. Pages outside the configured RAM/ROM ranges keep the old full scan. Evidence: building with `-DNW_JIT_INDEX_CHECK=1` cross-checks every indexed invalidation against the full scan and aborts on any live entry the index missed; that build ran in 10+ live boots (about 23,000 checks each) with zero failures; eight alternating boots each of the pre-change build and the new build showed 0/8 and 0/8 early-boot panics (the pre-existing SysError #2 flake did appear once in an earlier standalone boot of the checking build, same signature as before); the PPC core suite exits 0 with output identical to the baseline and `run_nw68_tests.py` reports 143361 passed. Measured: invalidation fell from 4.8% to 1.4% of the emulation thread's active boot time (the rest is the per-call bit tests of 170 million calls). Caveats: `ppc_core_harness` never configures RAM pages (it uses the fallback scan), so the index is validated by the live checking build and not by that harness; `mmu_harness.cpp` configures them but has no runner in `tools/`. Open: the per-call cost of the calls that find no code (an inline bit test in `jit_host_stb/sth` would skip the call) and the code/data-on-one-page thrash behind the 17,000 store invalidations per boot, which line-granular invalidation would remove.
- [x] **Measured result (October 4-5, 2026):** CPU time (user+sys via `ps cputime`) consumed from launch to the late-boot RAVE self-test line. Same binary, `NW_JIT_LEGACY=all` against the default, three alternating runs each, nothing else running. After the clock, sync, load, index and xlate changes plus the presented-hash change: legacy 48.2 / 48.1 / 47.3 s (mean 47.8), default 40.4 / 39.8 / 39.8 s (mean 40.0), 16.4% less CPU. After the inline MMU accessors as well (which apply to both modes): legacy 46.4 / 46.7 / 46.4 s (mean 46.5), default 38.9 / 39.2 / 39.2 s (mean 39.1), 15.9% less than legacy in the same session and about 18% less than the original code's 47.8 s. Earlier steps, for the record: four changes 47.8 -> 41.8 s (-12.4%) against the pre-change build (ddrel0); three changes 48.5 -> 41.9 s (noisier, other work overlapped). This measures boot, which includes idle waits excluded by CPU time but not iTunes redraw; it is not an input-to-visible-repaint measurement. A host profile of an iTunes raise on the first three changes showed clock reads 8.4% -> 1.1% and `nw_pull_gpr` 1,097 -> 470 samples of the emulation thread, byte/halfword loads gone from the translate callers, and `jit_host_stb` invalidation as the largest item (1,001 samples) before the page index.
- [x] **68k layer translation costs (October 5, 2026; found chasing a startup pause):** the New World 68k host JIT (`nw_68k_jit.cpp`) was 65-70% of the emulation thread during the Open Transport stretch of startup, and its guest-memory previews and commits were a quarter of that: `ppc32_mmu::translate` 12%, `nw_pa_kind` 4.5%, `nw68_page_translate` 5%. Changes, all in the 68k bus and the data TLB, all disabled together by `NW_JIT_LEGACY=xlate`: (1) store previews and `data_write` hit a DTLB entry only if it was marked by `data_write` after a full recorded (permission-checked, C-setting) store translation (`nw_jit_dtlb_mark_store_rec`/`nw_jit_dtlb_store_rec`; the WRITE flag alone is not enough because load fills set it without a permission check), so a hit proves both permission and the changed bit; (2) the commit's per-dispatch reference-bit translation (4-8 million `guest_data_probe` calls per 1.5 s, one per read page of every dispatch) is skipped for pages whose live DTLB entry already carries a recorded read (`nw_jit_dtlb_take_rec`; a refill, tlbie, segment or BAT change clears it with the entry); (3) `plain_memory` asks the bank table once instead of twice and `probe` no longer re-checks every byte of an access that stays on one page; (4) `compatible_nk` takes the two handler pages from the ITLB; (5) `nw_pa_kind` itself has a 256-entry page cache (banks are page-granular; `nw_banks_set` clears it and a sub-page bank disables it). Measured, same binary, `NW_JIT_LEGACY=xlate` against default, CPU seconds to the late-boot RAVE self-test line, three alternating pairs: 36.0 / 36.9 / 36.4 against 29.8 / 29.8 / 29.9 (-18%); the original tree was about 37.3. A 12-boot A/B (6 each way, 80 s) had 0 panics and the RAVE self-test reached every time. Gates: `run_nw68_tests.py` 143,361 passed; PPC core suite exit 0, output identical to the baseline; 68k verify mode (`NW_JIT68K_MODE=verify`) boot with no mismatch. Wall clock in a headless boot with window captures: icons and Finder 20% earlier (Finder desktop at about +40 s against +50 s). The startup pause itself only shrank from about 10 s to 9 s: it is long stretches of File Manager cache code (68k list walks over cached blocks, key compares and 512-byte BlockMove per block) running at 3-5 million dispatches per second, about 200 ns per dispatch. Remaining per-dispatch cost is spread thin (probe 6.8%, read_code 4.5%, finish/prepare 8%, run_block 4%+memcmp, cached_block copy 3-4%); the next steps are a fused cached-lookup-and-run entry that skips the second hash and compare, and a per-page `plain` memo in the dispatch page cache.
- [x] **Profiling tools left in tree for boot work (`NW_GUEST_PROF`):** `NW_GUEST_PROF_MS=<ms>` sets the report period; each period prints idle share, hottest PPC regions and blocks, the 68k pc (r24) histogram, the A-line trap histogram and the busiest (trap, caller pc) pairs, 68k dispatch and compile/invalidate counters, and `TRAPNAME` lines naming the file each Open/HFSDispatch call uses (epoch-stamped, so a screen recording can be lined up). Mode 2 also dumps instruction words of hot PPC blocks and the bytes of hot 68k blocks (`GPROF68W`) for offline disassembly (capstone has an M68K mode), and `NW_DUMP68K=<hex start>,<hex length>` dumps one guest range once.
- [ ] **Controlled performance qualification:** repeat serial Release off/on trials with identical cloned media, workload and warm-up, measuring input-to-visible-repaint median/p95 and emulation-thread profile shares, before claiming any speedup. Re-profile with the recipe above after every change in this list.

<!-- PPC-JIT-REVISIT-END -->

## Summary

| Status | Count |
|--------|------:|
| accepted | 310 |
| partial | 32 |
| todo | 0 |
| exclude | 3 |
| **total** | **345** |

| Family | Count |
|--------|------:|
| altivec | 154 |
| integer | 72 |
| fp | 50 |
| mem | 40 |
| control | 15 |
| spr | 13 |
| none | 1 |

## Checklist

| | Op | Family | Wave | Encoding | Notes |
|---|----|--------|------|----------|-------|
| [x] | `lvebx` | altivec | — | `X_form` 31/7 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V16QIm, RA_or_0, RB)`; X_form prim=31 xo=7 CFLOW_NORMAL; in allowlist |
| [x] | `lvehx` | altivec | — | `X_form` 31/39 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V8HIm, RA_or_0, RB)`; X_form prim=31 xo=39 CFLOW_NORMAL; in allowlist |
| [x] | `lvewx` | altivec | — | `X_form` 31/71 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V4SI, RA_or_0, RB)`; X_form prim=31 xo=71 CFLOW_NORMAL; in allowlist |
| [x] | `lvsl` | altivec | — | `X_form` 31/6 | kpx: `EXECUTE_1(vector_load_for_shift, 1)`; X_form prim=31 xo=6 CFLOW_NORMAL; in allowlist |
| [x] | `lvsr` | altivec | — | `X_form` 31/38 | kpx: `EXECUTE_1(vector_load_for_shift, 0)`; X_form prim=31 xo=38 CFLOW_NORMAL; in allowlist |
| [~] | `lvx` | altivec | W6 | `X_form` 31/103 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=103 CFLOW_NORMAL; hint ignored (lvxl same path).; in allowlist |
| [~] | `lvxl` | altivec | W6 | `X_form` 31/359 | kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=359 CFLOW_NORMAL; hint ignored.; in allowlist |
| [x] | `stvebx` | altivec | — | `X_form` 31/135 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V16QIm, RA_or_0, RB)`; X_form prim=31 xo=135 CFLOW_NORMAL; in allowlist |
| [x] | `stvehx` | altivec | — | `X_form` 31/167 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V8HIm, RA_or_0, RB)`; X_form prim=31 xo=167 CFLOW_NORMAL; in allowlist |
| [x] | `stvewx` | altivec | — | `X_form` 31/199 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V4SI, RA_or_0, RB)`; X_form prim=31 xo=199 CFLOW_NORMAL; in allowlist |
| [~] | `stvx` | altivec | W6 | `X_form` 31/231 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=231 CFLOW_NORMAL; hint ignored.; in allowlist |
| [~] | `stvxl` | altivec | W6 | `X_form` 31/487 | kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=487 CFLOW_NORMAL; hint ignored.; in allowlist |
| [x] | `vaddcuw` | altivec | — | `VX_form` 4/384 | kpx: `EXECUTE_VECTOR_ARITH(addcuw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=384 CFLOW_NORMAL; in allowlist |
| [x] | `vaddfp` | altivec | — | `VX_form` 4/10 | kpx: `EXECUTE_VECTOR_ARITH(fadds, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=10 CFLOW_NORMAL; in allowlist |
| [x] | `vaddsbs` | altivec | — | `VX_form` 4/768 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI_SAT<int8>, V16QI_SAT<int8>, V16QI_SAT<int8>, NONE)`; VX_form prim=4 xo=768 CFLOW_NORMAL; in allowlist |
| [x] | `vaddshs` | altivec | — | `VX_form` 4/832 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, NONE)`; VX_form prim=4 xo=832 CFLOW_NORMAL; in allowlist |
| [x] | `vaddsws` | altivec | — | `VX_form` 4/896 | kpx: `EXECUTE_VECTOR_ARITH(add_64, V4SI_SAT<int32>, V4SI_SAT<int32>, V4SI_SAT<int32>, NONE)`; VX_form prim=4 xo=896 CFLOW_NORMAL; in allowlist |
| [x] | `vaddubm` | altivec | — | `VX_form` 4/0 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `vaddubs` | altivec | — | `VX_form` 4/512 | kpx: `EXECUTE_VECTOR_ARITH(add, V16QI_SAT<uint8>, V16QI_SAT<uint8>, V16QI_SAT<uint8>, NONE)`; VX_form prim=4 xo=512 CFLOW_NORMAL; in allowlist |
| [x] | `vadduhm` | altivec | — | `VX_form` 4/64 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=64 CFLOW_NORMAL; in allowlist |
| [x] | `vadduhs` | altivec | — | `VX_form` 4/576 | kpx: `EXECUTE_VECTOR_ARITH(add, V8HI_SAT<uint16>, V8HI_SAT<uint16>, V8HI_SAT<uint16>, NONE)`; VX_form prim=4 xo=576 CFLOW_NORMAL; in allowlist |
| [x] | `vadduwm` | altivec | — | `VX_form` 4/128 | kpx: `EXECUTE_VECTOR_ARITH(add, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=128 CFLOW_NORMAL; in allowlist |
| [x] | `vadduws` | altivec | — | `VX_form` 4/640 | kpx: `EXECUTE_VECTOR_ARITH(add_64, V4SI_SAT<uint32>, V4SI_SAT<uint32>, V4SI_SAT<uint32>, NONE)`; VX_form prim=4 xo=640 CFLOW_NORMAL; in allowlist |
| [x] | `vand` | altivec | — | `VX_form` 4/1028 | kpx: `EXECUTE_VECTOR_ARITH(and_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1028 CFLOW_NORMAL; in allowlist |
| [x] | `vandc` | altivec | — | `VX_form` 4/1092 | kpx: `EXECUTE_VECTOR_ARITH(andc_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1092 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsb` | altivec | — | `VX_form` 4/1282 | kpx: `EXECUTE_VECTOR_ARITH(avgsb, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1282 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsh` | altivec | — | `VX_form` 4/1346 | kpx: `EXECUTE_VECTOR_ARITH(avgsh, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1346 CFLOW_NORMAL; in allowlist |
| [x] | `vavgsw` | altivec | — | `VX_form` 4/1410 | kpx: `EXECUTE_VECTOR_ARITH(avgsw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1410 CFLOW_NORMAL; in allowlist |
| [x] | `vavgub` | altivec | — | `VX_form` 4/1026 | kpx: `EXECUTE_VECTOR_ARITH(avgub, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1026 CFLOW_NORMAL; in allowlist |
| [x] | `vavguh` | altivec | — | `VX_form` 4/1090 | kpx: `EXECUTE_VECTOR_ARITH(avguh, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1090 CFLOW_NORMAL; in allowlist |
| [x] | `vavguw` | altivec | — | `VX_form` 4/1154 | kpx: `EXECUTE_VECTOR_ARITH(avguw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1154 CFLOW_NORMAL; in allowlist |
| [x] | `vcfsx` | altivec | — | `VX_form` 4/842 | kpx: `EXECUTE_VECTOR_ARITH(cvt_si2fp<int32>, V4SF, UIMM, V4SIs, NONE)`; VX_form prim=4 xo=842 CFLOW_NORMAL; in allowlist |
| [x] | `vcfux` | altivec | — | `VX_form` 4/778 | kpx: `EXECUTE_VECTOR_ARITH(cvt_si2fp<uint32>, V4SF, UIMM, V4SI, NONE)`; VX_form prim=4 xo=778 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpbfp` | altivec | — | `VXR_form` 4/966 | kpx: `EXECUTE_VECTOR_COMPARE(cmpbfp, V4SI, V4SF, V4SF, 0)`; VXR_form prim=4 xo=966 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpeqfp` | altivec | — | `VXR_form` 4/198 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=198 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequb` | altivec | — | `VXR_form` 4/6 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint8>, V16QI, V16QI, V16QI, 1)`; VXR_form prim=4 xo=6 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequh` | altivec | — | `VXR_form` 4/70 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint16>, V8HI, V8HI, V8HI, 1)`; VXR_form prim=4 xo=70 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpequw` | altivec | — | `VXR_form` 4/134 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_eq<uint32>, V4SI, V4SI, V4SI, 1)`; VXR_form prim=4 xo=134 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgefp` | altivec | — | `VXR_form` 4/454 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_ge<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=454 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtfp` | altivec | — | `VXR_form` 4/710 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<float>, V4SI, V4SF, V4SF, 1)`; VXR_form prim=4 xo=710 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsb` | altivec | — | `VXR_form` 4/774 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int8>, V16QI, V16QIs, V16QIs, 1)`; VXR_form prim=4 xo=774 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsh` | altivec | — | `VXR_form` 4/838 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int16>, V8HI, V8HIs, V8HIs, 1)`; VXR_form prim=4 xo=838 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtsw` | altivec | — | `VXR_form` 4/902 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<int32>, V4SI, V4SIs, V4SIs, 1)`; VXR_form prim=4 xo=902 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtub` | altivec | — | `VXR_form` 4/518 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint8>, V16QI, V16QI, V16QI, 1)`; VXR_form prim=4 xo=518 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtuh` | altivec | — | `VXR_form` 4/582 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint16>, V8HI, V8HI, V8HI, 1)`; VXR_form prim=4 xo=582 CFLOW_NORMAL; in allowlist |
| [x] | `vcmpgtuw` | altivec | — | `VXR_form` 4/646 | kpx: `EXECUTE_VECTOR_COMPARE(cmp_gt<uint32>, V4SI, V4SI, V4SI, 1)`; VXR_form prim=4 xo=646 CFLOW_NORMAL; in allowlist |
| [x] | `vctsxs` | altivec | — | `VX_form` 4/970 | kpx: `EXECUTE_VECTOR_ARITH(cvt_fp2si, V4SI_SAT<int32>, UIMM, V4SF, NONE)`; VX_form prim=4 xo=970 CFLOW_NORMAL; in allowlist |
| [x] | `vctuxs` | altivec | — | `VX_form` 4/906 | kpx: `EXECUTE_VECTOR_ARITH(cvt_fp2si, V4SI_SAT<uint32>, UIMM, V4SF, NONE)`; VX_form prim=4 xo=906 CFLOW_NORMAL; in allowlist |
| [x] | `vexptefp` | altivec | — | `VX_form` 4/394 | kpx: `EXECUTE_VECTOR_ARITH(exp2, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=394 CFLOW_NORMAL; in allowlist |
| [x] | `vlogefp` | altivec | — | `VX_form` 4/458 | kpx: `EXECUTE_VECTOR_ARITH(log2, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=458 CFLOW_NORMAL; in allowlist |
| [x] | `vmaddfp` | altivec | — | `VA_form` 4/46 | kpx: `EXECUTE_VECTOR_ARITH(vmaddfp, V4SF, V4SF, V4SF, V4SF)`; VA_form prim=4 xo=46 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxfp` | altivec | — | `VX_form` 4/1034 | kpx: `EXECUTE_VECTOR_ARITH(max<float>, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=1034 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsb` | altivec | — | `VX_form` 4/258 | kpx: `EXECUTE_VECTOR_ARITH(max<int8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=258 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsh` | altivec | — | `VX_form` 4/322 | kpx: `EXECUTE_VECTOR_ARITH(max<int16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=322 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxsw` | altivec | — | `VX_form` 4/386 | kpx: `EXECUTE_VECTOR_ARITH(max<int32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=386 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxub` | altivec | — | `VX_form` 4/2 | kpx: `EXECUTE_VECTOR_ARITH(max<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=2 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxuh` | altivec | — | `VX_form` 4/66 | kpx: `EXECUTE_VECTOR_ARITH(max<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=66 CFLOW_NORMAL; in allowlist |
| [x] | `vmaxuw` | altivec | — | `VX_form` 4/130 | kpx: `EXECUTE_VECTOR_ARITH(max<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=130 CFLOW_NORMAL; in allowlist |
| [x] | `vmhaddshs` | altivec | — | `VA_form` 4/32 | kpx: `EXECUTE_VECTOR_ARITH(mhraddsh<0>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>)`; VA_form prim=4 xo=32 CFLOW_NORMAL; in allowlist |
| [x] | `vmhraddshs` | altivec | — | `VA_form` 4/33 | kpx: `EXECUTE_VECTOR_ARITH(mhraddsh<0x4000>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>)`; VA_form prim=4 xo=33 CFLOW_NORMAL; in allowlist |
| [x] | `vminfp` | altivec | — | `VX_form` 4/1098 | kpx: `EXECUTE_VECTOR_ARITH(min<float>, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=1098 CFLOW_NORMAL; in allowlist |
| [x] | `vminsb` | altivec | — | `VX_form` 4/770 | kpx: `EXECUTE_VECTOR_ARITH(min<int8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=770 CFLOW_NORMAL; in allowlist |
| [x] | `vminsh` | altivec | — | `VX_form` 4/834 | kpx: `EXECUTE_VECTOR_ARITH(min<int16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=834 CFLOW_NORMAL; in allowlist |
| [x] | `vminsw` | altivec | — | `VX_form` 4/898 | kpx: `EXECUTE_VECTOR_ARITH(min<int32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=898 CFLOW_NORMAL; in allowlist |
| [x] | `vminub` | altivec | — | `VX_form` 4/514 | kpx: `EXECUTE_VECTOR_ARITH(min<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=514 CFLOW_NORMAL; in allowlist |
| [x] | `vminuh` | altivec | — | `VX_form` 4/578 | kpx: `EXECUTE_VECTOR_ARITH(min<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=578 CFLOW_NORMAL; in allowlist |
| [x] | `vminuw` | altivec | — | `VX_form` 4/642 | kpx: `EXECUTE_VECTOR_ARITH(min<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=642 CFLOW_NORMAL; in allowlist |
| [x] | `vmladduhm` | altivec | — | `VA_form` 4/34 | kpx: `EXECUTE_VECTOR_ARITH(mladduh, V8HI, V8HI, V8HI, V8HI)`; VA_form prim=4 xo=34 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghb` | altivec | — | `VX_form` 4/12 | kpx: `EXECUTE_VECTOR_MERGE(V16QIm, V16QIm, V16QIm, 0)`; VX_form prim=4 xo=12 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghh` | altivec | — | `VX_form` 4/76 | kpx: `EXECUTE_VECTOR_MERGE(V8HIm, V8HIm, V8HIm, 0)`; VX_form prim=4 xo=76 CFLOW_NORMAL; in allowlist |
| [x] | `vmrghw` | altivec | — | `VX_form` 4/140 | kpx: `EXECUTE_VECTOR_MERGE(V4SI, V4SI, V4SI, 0)`; VX_form prim=4 xo=140 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglb` | altivec | — | `VX_form` 4/268 | kpx: `EXECUTE_VECTOR_MERGE(V16QIm, V16QIm, V16QIm, 1)`; VX_form prim=4 xo=268 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglh` | altivec | — | `VX_form` 4/332 | kpx: `EXECUTE_VECTOR_MERGE(V8HIm, V8HIm, V8HIm, 1)`; VX_form prim=4 xo=332 CFLOW_NORMAL; in allowlist |
| [x] | `vmrglw` | altivec | — | `VX_form` 4/396 | kpx: `EXECUTE_VECTOR_MERGE(V4SI, V4SI, V4SI, 1)`; VX_form prim=4 xo=396 CFLOW_NORMAL; in allowlist |
| [x] | `vmsummbm` | altivec | — | `VA_form` 4/37 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul, V4SI, V16QI_SAT<int8>, V16QI_SAT<uint8>, V4SI)`; VA_form prim=4 xo=37 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumshm` | altivec | — | `VA_form` 4/40 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul, V4SI, V8HI_SAT<int16>, V8HI_SAT<int16>, V4SI)`; VA_form prim=4 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumshs` | altivec | — | `VA_form` 4/41 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(smul_64, V4SI_SAT<int32>, V8HI_SAT<int16>, V8HI_SAT<int16>, V4SIs)`; VA_form prim=4 xo=41 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumubm` | altivec | — | `VA_form` 4/36 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI, V16QI, V16QI, V4SI)`; VA_form prim=4 xo=36 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumuhm` | altivec | — | `VA_form` 4/38 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI, V8HI, V8HI, V4SI)`; VA_form prim=4 xo=38 CFLOW_NORMAL; in allowlist |
| [x] | `vmsumuhs` | altivec | — | `VA_form` 4/39 | kpx: `EXECUTE_VECTOR_ARITH_MIXED(mul, V4SI_SAT<uint32>, V8HI, V8HI, V4SI)`; VA_form prim=4 xo=39 CFLOW_NORMAL; in allowlist |
| [x] | `vmulesb` | altivec | — | `VX_form` 4/776 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, smul, V8HIm, V16QIm_SAT<int8>, V16QIm_SAT<int8>, NONE)`; VX_form prim=4 xo=776 CFLOW_NORMAL; in allowlist |
| [x] | `vmulesh` | altivec | — | `VX_form` 4/840 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, smul, V4SI, V8HIm_SAT<int16>, V8HIm_SAT<int16>, NONE)`; VX_form prim=4 xo=840 CFLOW_NORMAL; in allowlist |
| [x] | `vmuleub` | altivec | — | `VX_form` 4/520 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, mul, V8HIm, V16QIm, V16QIm, NONE)`; VX_form prim=4 xo=520 CFLOW_NORMAL; in allowlist |
| [x] | `vmuleuh` | altivec | — | `VX_form` 4/584 | kpx: `EXECUTE_VECTOR_ARITH_ODD(0, mul, V4SI, V8HIm, V8HIm, NONE)`; VX_form prim=4 xo=584 CFLOW_NORMAL; in allowlist |
| [x] | `vmulosb` | altivec | — | `VX_form` 4/264 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, smul, V8HIm, V16QIm_SAT<int8>, V16QIm_SAT<int8>, NONE)`; VX_form prim=4 xo=264 CFLOW_NORMAL; in allowlist |
| [x] | `vmulosh` | altivec | — | `VX_form` 4/328 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, smul, V4SI, V8HIm_SAT<int16>, V8HIm_SAT<int16>, NONE)`; VX_form prim=4 xo=328 CFLOW_NORMAL; in allowlist |
| [x] | `vmuloub` | altivec | — | `VX_form` 4/8 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, mul, V8HIm, V16QIm, V16QIm, NONE)`; VX_form prim=4 xo=8 CFLOW_NORMAL; in allowlist |
| [x] | `vmulouh` | altivec | — | `VX_form` 4/72 | kpx: `EXECUTE_VECTOR_ARITH_ODD(1, mul, V4SI, V8HIm, V8HIm, NONE)`; VX_form prim=4 xo=72 CFLOW_NORMAL; in allowlist |
| [x] | `vnmsubfp` | altivec | — | `VA_form` 4/47 | kpx: `EXECUTE_VECTOR_ARITH(vnmsubfp, V4SF, V4SF, V4SF, V4SF)`; VA_form prim=4 xo=47 CFLOW_NORMAL; in allowlist |
| [x] | `vnor` | altivec | — | `VX_form` 4/1284 | kpx: `EXECUTE_VECTOR_ARITH(nor_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1284 CFLOW_NORMAL; in allowlist |
| [x] | `vor` | altivec | — | `VX_form` 4/1156 | kpx: `EXECUTE_VECTOR_ARITH(or_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1156 CFLOW_NORMAL; in allowlist |
| [x] | `vperm` | altivec | — | `VA_form` 4/43 | kpx: `EXECUTE_0(vector_permute)`; VA_form prim=4 xo=43 CFLOW_NORMAL; in allowlist |
| [x] | `vpkpx` | altivec | — | `VX_form` 4/782 | kpx: `EXECUTE_0(vector_pack_pixel)`; VX_form prim=4 xo=782 CFLOW_NORMAL; in allowlist |
| [x] | `vpkshss` | altivec | — | `VX_form` 4/398 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_SAT<int8>, V8HIm, V8HIm)`; VX_form prim=4 xo=398 CFLOW_NORMAL; in allowlist |
| [x] | `vpkshus` | altivec | — | `VX_form` 4/270 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_SAT<uint8>, V8HIm, V8HIm)`; VX_form prim=4 xo=270 CFLOW_NORMAL; in allowlist |
| [x] | `vpkswss` | altivec | — | `VX_form` 4/462 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_SAT<int16>, V4SI, V4SI)`; VX_form prim=4 xo=462 CFLOW_NORMAL; in allowlist |
| [x] | `vpkswus` | altivec | — | `VX_form` 4/334 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_SAT<uint16>, V4SI, V4SI)`; VX_form prim=4 xo=334 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuhum` | altivec | — | `VX_form` 4/14 | kpx: `EXECUTE_VECTOR_PACK(V16QIm, V8HIm, V8HIm)`; VX_form prim=4 xo=14 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuhus` | altivec | — | `VX_form` 4/142 | kpx: `EXECUTE_VECTOR_PACK(V16QIm_USAT<uint8>, V8HIm, V8HIm)`; VX_form prim=4 xo=142 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuwum` | altivec | — | `VX_form` 4/78 | kpx: `EXECUTE_VECTOR_PACK(V8HIm, V4SI, V4SI)`; VX_form prim=4 xo=78 CFLOW_NORMAL; in allowlist |
| [x] | `vpkuwus` | altivec | — | `VX_form` 4/206 | kpx: `EXECUTE_VECTOR_PACK(V8HIm_USAT<uint16>, V4SI, V4SI)`; VX_form prim=4 xo=206 CFLOW_NORMAL; in allowlist |
| [x] | `vrefp` | altivec | — | `VX_form` 4/266 | kpx: `EXECUTE_VECTOR_ARITH(fres, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=266 CFLOW_NORMAL; in allowlist |
| [x] | `vrfim` | altivec | — | `VX_form` 4/714 | kpx: `EXECUTE_VECTOR_ARITH(frsim, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=714 CFLOW_NORMAL; in allowlist |
| [x] | `vrfin` | altivec | — | `VX_form` 4/522 | kpx: `EXECUTE_VECTOR_ARITH(frsin, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=522 CFLOW_NORMAL; in allowlist |
| [x] | `vrfip` | altivec | — | `VX_form` 4/650 | kpx: `EXECUTE_VECTOR_ARITH(frsip, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=650 CFLOW_NORMAL; in allowlist |
| [x] | `vrfiz` | altivec | — | `VX_form` 4/586 | kpx: `EXECUTE_VECTOR_ARITH(frsiz, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=586 CFLOW_NORMAL; in allowlist |
| [x] | `vrlb` | altivec | — | `VX_form` 4/4 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=4 CFLOW_NORMAL; in allowlist |
| [x] | `vrlh` | altivec | — | `VX_form` 4/68 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=68 CFLOW_NORMAL; in allowlist |
| [x] | `vrlw` | altivec | — | `VX_form` 4/132 | kpx: `EXECUTE_VECTOR_ARITH(vrl<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=132 CFLOW_NORMAL; in allowlist |
| [x] | `vrsqrtefp` | altivec | — | `VX_form` 4/330 | kpx: `EXECUTE_VECTOR_ARITH(frsqrte, V4SF, NONE, V4SF, NONE)`; VX_form prim=4 xo=330 CFLOW_NORMAL; in allowlist |
| [x] | `vsel` | altivec | — | `VA_form` 4/42 | kpx: `EXECUTE_VECTOR_ARITH(vsel, V4SI, V4SI, V4SI, V4SI)`; VA_form prim=4 xo=42 CFLOW_NORMAL; in allowlist |
| [x] | `vsl` | altivec | — | `VX_form` 4/452 | kpx: `EXECUTE_1(vector_shift, -1)`; VX_form prim=4 xo=452 CFLOW_NORMAL; in allowlist |
| [x] | `vslb` | altivec | — | `VX_form` 4/260 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=260 CFLOW_NORMAL; in allowlist |
| [x] | `vsldoi` | altivec | — | `VA_form` 4/44 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(-1, V16QIm, V16QIm, V16QIm, SHB)`; VA_form prim=4 xo=44 CFLOW_NORMAL; in allowlist |
| [x] | `vslh` | altivec | — | `VX_form` 4/324 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=324 CFLOW_NORMAL; in allowlist |
| [x] | `vslo` | altivec | — | `VX_form` 4/1036 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(-1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1036 CFLOW_NORMAL; in allowlist |
| [x] | `vslw` | altivec | — | `VX_form` 4/388 | kpx: `EXECUTE_VECTOR_ARITH(vsl<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=388 CFLOW_NORMAL; in allowlist |
| [x] | `vspltb` | altivec | — | `VX_form` 4/524 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V16QI, V16QIm, false)`; VX_form prim=4 xo=524 CFLOW_NORMAL; in allowlist |
| [x] | `vsplth` | altivec | — | `VX_form` 4/588 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V8HI, V8HIm, false)`; VX_form prim=4 xo=588 CFLOW_NORMAL; in allowlist |
| [x] | `vspltisb` | altivec | — | `VX_form` 4/780 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V16QI, UIMM, true)`; VX_form prim=4 xo=780 CFLOW_NORMAL; in allowlist |
| [x] | `vspltish` | altivec | — | `VX_form` 4/844 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V8HI, UIMM, true)`; VX_form prim=4 xo=844 CFLOW_NORMAL; in allowlist |
| [x] | `vspltisw` | altivec | — | `VX_form` 4/908 | kpx: `EXECUTE_VECTOR_SPLAT(sign_extend_5_32, V4SI, UIMM, true)`; VX_form prim=4 xo=908 CFLOW_NORMAL; in allowlist |
| [x] | `vspltw` | altivec | — | `VX_form` 4/652 | kpx: `EXECUTE_VECTOR_SPLAT(nop, V4SI, V4SI, false)`; VX_form prim=4 xo=652 CFLOW_NORMAL; in allowlist |
| [x] | `vsr` | altivec | — | `VX_form` 4/708 | kpx: `EXECUTE_1(vector_shift, +1)`; VX_form prim=4 xo=708 CFLOW_NORMAL; in allowlist |
| [x] | `vsrab` | altivec | — | `VX_form` 4/772 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int8>, V16QI, V16QIs, V16QI, NONE)`; VX_form prim=4 xo=772 CFLOW_NORMAL; in allowlist |
| [x] | `vsrah` | altivec | — | `VX_form` 4/836 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int16>, V8HI, V8HIs, V8HI, NONE)`; VX_form prim=4 xo=836 CFLOW_NORMAL; in allowlist |
| [x] | `vsraw` | altivec | — | `VX_form` 4/900 | kpx: `EXECUTE_VECTOR_ARITH(vsr<int32>, V4SI, V4SIs, V4SIs, NONE)`; VX_form prim=4 xo=900 CFLOW_NORMAL; in allowlist |
| [x] | `vsrb` | altivec | — | `VX_form` 4/516 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint8>, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=516 CFLOW_NORMAL; in allowlist |
| [x] | `vsrh` | altivec | — | `VX_form` 4/580 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint16>, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=580 CFLOW_NORMAL; in allowlist |
| [x] | `vsro` | altivec | — | `VX_form` 4/1100 | kpx: `EXECUTE_VECTOR_SHIFT_OCTET(+1, V16QIm, V16QIm, NONE, SHBO)`; VX_form prim=4 xo=1100 CFLOW_NORMAL; in allowlist |
| [x] | `vsrw` | altivec | — | `VX_form` 4/644 | kpx: `EXECUTE_VECTOR_ARITH(vsr<uint32>, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=644 CFLOW_NORMAL; in allowlist |
| [x] | `vsubcuw` | altivec | — | `VX_form` 4/1408 | kpx: `EXECUTE_VECTOR_ARITH(subcuw, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1408 CFLOW_NORMAL; in allowlist |
| [x] | `vsubfp` | altivec | — | `VX_form` 4/74 | kpx: `EXECUTE_VECTOR_ARITH(fsubs, V4SF, V4SF, V4SF, NONE)`; VX_form prim=4 xo=74 CFLOW_NORMAL; in allowlist |
| [x] | `vsubsbs` | altivec | — | `VX_form` 4/1792 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI_SAT<int8>, V16QI_SAT<int8>, V16QI_SAT<int8>, NONE)`; VX_form prim=4 xo=1792 CFLOW_NORMAL; in allowlist |
| [x] | `vsubshs` | altivec | — | `VX_form` 4/1856 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI_SAT<int16>, V8HI_SAT<int16>, V8HI_SAT<int16>, NONE)`; VX_form prim=4 xo=1856 CFLOW_NORMAL; in allowlist |
| [x] | `vsubsws` | altivec | — | `VX_form` 4/1920 | kpx: `EXECUTE_VECTOR_ARITH(sub_64, V4SI_SAT<int32>, V4SI_SAT<int32>, V4SI_SAT<int32>, NONE)`; VX_form prim=4 xo=1920 CFLOW_NORMAL; in allowlist |
| [x] | `vsububm` | altivec | — | `VX_form` 4/1024 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI, V16QI, V16QI, NONE)`; VX_form prim=4 xo=1024 CFLOW_NORMAL; in allowlist |
| [x] | `vsububs` | altivec | — | `VX_form` 4/1536 | kpx: `EXECUTE_VECTOR_ARITH(sub, V16QI_SAT<uint8>, V16QI_SAT<uint8>, V16QI_SAT<uint8>, NONE)`; VX_form prim=4 xo=1536 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuhm` | altivec | — | `VX_form` 4/1088 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI, V8HI, V8HI, NONE)`; VX_form prim=4 xo=1088 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuhs` | altivec | — | `VX_form` 4/1600 | kpx: `EXECUTE_VECTOR_ARITH(sub, V8HI_SAT<uint16>, V8HI_SAT<uint16>, V8HI_SAT<uint16>, NONE)`; VX_form prim=4 xo=1600 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuwm` | altivec | — | `VX_form` 4/1152 | kpx: `EXECUTE_VECTOR_ARITH(sub, V4SI, V4SI, V4SI, NONE)`; VX_form prim=4 xo=1152 CFLOW_NORMAL; in allowlist |
| [x] | `vsubuws` | altivec | — | `VX_form` 4/1664 | kpx: `EXECUTE_VECTOR_ARITH(sub_64, V4SI_SAT<uint32>, V4SI_SAT<uint32>, V4SI_SAT<uint32>, NONE)`; VX_form prim=4 xo=1664 CFLOW_NORMAL; in allowlist |
| [x] | `vsum2sws` | altivec | — | `VX_form` 4/1672 | kpx: `EXECUTE_VECTOR_SUM(2, V4SI_SAT<int32>, V4SIs, V4SIs)`; VX_form prim=4 xo=1672 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4sbs` | altivec | — | `VX_form` 4/1800 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<int32>, V16QIs, V4SIs)`; VX_form prim=4 xo=1800 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4shs` | altivec | — | `VX_form` 4/1608 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<int32>, V8HIs, V4SIs)`; VX_form prim=4 xo=1608 CFLOW_NORMAL; in allowlist |
| [x] | `vsum4ubs` | altivec | — | `VX_form` 4/1544 | kpx: `EXECUTE_VECTOR_SUM(4, V4SI_SAT<uint32>, V16QI, V4SI)`; VX_form prim=4 xo=1544 CFLOW_NORMAL; in allowlist |
| [x] | `vsumsws` | altivec | — | `VX_form` 4/1928 | kpx: `EXECUTE_VECTOR_SUM(1, V4SI_SAT<int32>, V4SIs, V4SIs)`; VX_form prim=4 xo=1928 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhpx` | altivec | — | `VX_form` 4/846 | kpx: `EXECUTE_1(vector_unpack_pixel, 0)`; VX_form prim=4 xo=846 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhsb` | altivec | — | `VX_form` 4/526 | kpx: `EXECUTE_VECTOR_UNPACK(0, V8HIms, V16QIms)`; VX_form prim=4 xo=526 CFLOW_NORMAL; in allowlist |
| [x] | `vupkhsh` | altivec | — | `VX_form` 4/590 | kpx: `EXECUTE_VECTOR_UNPACK(0, V4SIs, V8HIms)`; VX_form prim=4 xo=590 CFLOW_NORMAL; in allowlist |
| [x] | `vupklpx` | altivec | — | `VX_form` 4/974 | kpx: `EXECUTE_1(vector_unpack_pixel, 1)`; VX_form prim=4 xo=974 CFLOW_NORMAL; in allowlist |
| [x] | `vupklsb` | altivec | — | `VX_form` 4/654 | kpx: `EXECUTE_VECTOR_UNPACK(1, V8HIms, V16QIms)`; VX_form prim=4 xo=654 CFLOW_NORMAL; in allowlist |
| [x] | `vupklsh` | altivec | — | `VX_form` 4/718 | kpx: `EXECUTE_VECTOR_UNPACK(1, V4SIs, V8HIms)`; VX_form prim=4 xo=718 CFLOW_NORMAL; in allowlist |
| [x] | `vxor` | altivec | — | `VX_form` 4/1220 | kpx: `EXECUTE_VECTOR_ARITH(xor_64, V2DI, V2DI, V2DI, NONE)`; VX_form prim=4 xo=1220 CFLOW_NORMAL; in allowlist |
| [x] | `b` | control | — | `I_form` 18/0 | kpx: `EXECUTE_BRANCH(PC, immediate_value<BO_MAKE(0,0,0,0)>, LI, AA_BIT_G, LK_BIT_G)`; I_form prim=18 xo=0 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bc` | control | — | `B_form` 16/0 | kpx: `EXECUTE_BRANCH(PC, operand_BO, BD, AA_BIT_G, LK_BIT_G)`; B_form prim=16 xo=0 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bcctr` | control | — | `XL_form` 19/528 | kpx: `EXECUTE_BRANCH(CTR, operand_BO, ZERO, AA_BIT_0, LK_BIT_G)`; XL_form prim=19 xo=528 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `bclr` | control | — | `XL_form` 19/16 | kpx: `EXECUTE_BRANCH(LR, operand_BO, ZERO, AA_BIT_0, LK_BIT_G)`; XL_form prim=19 xo=16 CFLOW_BRANCH; **ends_block**; in allowlist |
| [x] | `crand` | control | — | `XL_form` 19/257 | kpx: `EXECUTE_CR_OP(and)`; XL_form prim=19 xo=257 CFLOW_NORMAL; in allowlist |
| [x] | `crandc` | control | — | `XL_form` 19/129 | kpx: `EXECUTE_CR_OP(andc)`; XL_form prim=19 xo=129 CFLOW_NORMAL; in allowlist |
| [x] | `creqv` | control | — | `XL_form` 19/289 | kpx: `EXECUTE_CR_OP(eqv)`; XL_form prim=19 xo=289 CFLOW_NORMAL; in allowlist |
| [x] | `crnand` | control | — | `XL_form` 19/225 | kpx: `EXECUTE_CR_OP(nand)`; XL_form prim=19 xo=225 CFLOW_NORMAL; in allowlist |
| [x] | `crnor` | control | — | `XL_form` 19/33 | kpx: `EXECUTE_CR_OP(nor)`; XL_form prim=19 xo=33 CFLOW_NORMAL; in allowlist |
| [x] | `cror` | control | — | `XL_form` 19/449 | kpx: `EXECUTE_CR_OP(or)`; XL_form prim=19 xo=449 CFLOW_NORMAL; in allowlist |
| [x] | `crorc` | control | — | `XL_form` 19/417 | kpx: `EXECUTE_CR_OP(orc)`; XL_form prim=19 xo=417 CFLOW_NORMAL; in allowlist |
| [x] | `crxor` | control | — | `XL_form` 19/193 | kpx: `EXECUTE_CR_OP(xor)`; XL_form prim=19 xo=193 CFLOW_NORMAL; in allowlist |
| [x] | `isync` | control | — | `X_form` 19/150 | kpx: `EXECUTE_0(isync)`; X_form prim=19 xo=150 CFLOW_NORMAL; **ends_block**; in allowlist |
| [~] | `rfi` | control | — | `XL_form` 19/50 | kpx: `EXECUTE_0(rfi)`; XL_form prim=19 xo=50 CFLOW_JUMP; Typed system return; private PC/MSR restore; ends block.; **ends_block**; in allowlist |
| [x] | `sc` | control | — | `SC_form` 17/0 | kpx: `EXECUTE_0(syscall)`; SC_form prim=17 xo=0 CFLOW_NORMAL; **ends_block**; in allowlist |
| [x] | `fabs` | fp | — | `X_form` 63/264 | kpx: `EXECUTE_FP_ARITH(double, fabs, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=264 CFLOW_NORMAL; in allowlist |
| [x] | `fadd` | fp | — | `A_form` 63/21 | kpx: `EXECUTE_FP_ARITH(double, fadd, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=21 CFLOW_NORMAL; in allowlist |
| [x] | `fadds` | fp | — | `A_form` 59/21 | kpx: `EXECUTE_FP_ARITH(float, fadd, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=21 CFLOW_NORMAL; in allowlist |
| [x] | `fcmpo` | fp | — | `X_form` 63/32 | kpx: `EXECUTE_1(fp_compare, true)`; X_form prim=63 xo=32 CFLOW_NORMAL; in allowlist |
| [x] | `fcmpu` | fp | — | `X_form` 63/0 | kpx: `EXECUTE_1(fp_compare, false)`; X_form prim=63 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `fctiw` | fp | — | `X_form` 63/14 | kpx: `EXECUTE_2(fp_int_convert, operand_FPSCR_RN, RC_BIT_G)`; X_form prim=63 xo=14 CFLOW_NORMAL; in allowlist |
| [x] | `fctiwz` | fp | — | `X_form` 63/15 | kpx: `EXECUTE_2(fp_int_convert, operand_ONE, RC_BIT_G)`; X_form prim=63 xo=15 CFLOW_NORMAL; in allowlist |
| [x] | `fdiv` | fp | — | `A_form` 63/18 | kpx: `EXECUTE_FP_ARITH(double, fdiv, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=18 CFLOW_NORMAL; in allowlist |
| [x] | `fdivs` | fp | — | `A_form` 59/18 | kpx: `EXECUTE_FP_ARITH(float, fdiv, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=18 CFLOW_NORMAL; in allowlist |
| [x] | `fmadd` | fp | — | `A_form` 63/29 | kpx: `EXECUTE_FP_ARITH(double, fmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=29 CFLOW_NORMAL; in allowlist |
| [x] | `fmadds` | fp | — | `A_form` 59/29 | kpx: `EXECUTE_FP_ARITH(float, fmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=29 CFLOW_NORMAL; in allowlist |
| [x] | `fmr` | fp | — | `X_form` 63/72 | kpx: `EXECUTE_FP_ARITH(double, fnop, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=72 CFLOW_NORMAL; in allowlist |
| [x] | `fmsub` | fp | — | `A_form` 63/28 | kpx: `EXECUTE_FP_ARITH(double, fmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `fmsubs` | fp | — | `A_form` 59/28 | kpx: `EXECUTE_FP_ARITH(float, fmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `fmul` | fp | — | `A_form` 63/25 | kpx: `EXECUTE_FP_ARITH(double, fmul, RD, RA, RC, NONE, RC_BIT_G, true)`; A_form prim=63 xo=25 CFLOW_NORMAL; in allowlist |
| [x] | `fmuls` | fp | — | `A_form` 59/25 | kpx: `EXECUTE_FP_ARITH(float, fmul, RD, RA, RC, NONE, RC_BIT_G, true)`; A_form prim=59 xo=25 CFLOW_NORMAL; in allowlist |
| [x] | `fnabs` | fp | — | `X_form` 63/136 | kpx: `EXECUTE_FP_ARITH(double, fnabs, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=136 CFLOW_NORMAL; in allowlist |
| [x] | `fneg` | fp | — | `X_form` 63/40 | kpx: `EXECUTE_FP_ARITH(double, fneg, RD, RB, NONE, NONE, RC_BIT_G, false)`; X_form prim=63 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `fnmadd` | fp | — | `A_form` 63/31 | kpx: `EXECUTE_FP_ARITH(double, fnmadd, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=31 CFLOW_NORMAL; in allowlist |
| [x] | `fnmadds` | fp | — | `A_form` 59/31 | kpx: `EXECUTE_FP_ARITH(double, fnmadds, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=31 CFLOW_NORMAL; in allowlist |
| [x] | `fnmsub` | fp | — | `A_form` 63/30 | kpx: `EXECUTE_FP_ARITH(double, fnmsub, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=63 xo=30 CFLOW_NORMAL; in allowlist |
| [x] | `fnmsubs` | fp | — | `A_form` 59/30 | kpx: `EXECUTE_FP_ARITH(double, fnmsubs, RD, RA, RC, RB, RC_BIT_G, true)`; A_form prim=59 xo=30 CFLOW_NORMAL; in allowlist |
| [x] | `fres` | fp | — | `A_form` 59/24 | kpx: `EXECUTE_FP_ARITH(double, fres, RD, RB, NONE, NONE, RC_BIT_G, true)`; A_form prim=59 xo=24 CFLOW_NORMAL; in allowlist |
| [x] | `frsp` | fp | — | `X_form` 63/12 | kpx: `EXECUTE_1(fp_round, RC_BIT_G)`; X_form prim=63 xo=12 CFLOW_NORMAL; in allowlist |
| [x] | `frsqrte` | fp | — | `A_form` 63/26 | kpx: `EXECUTE_FP_ARITH(double, frsqrte, RD, RB, NONE, NONE, RC_BIT_G, true)`; A_form prim=63 xo=26 CFLOW_NORMAL; in allowlist |
| [x] | `fsel` | fp | — | `A_form` 63/23 | kpx: `EXECUTE_FP_ARITH(double, fsel, RD, RA, RC, RB, RC_BIT_G, false)`; A_form prim=63 xo=23 CFLOW_NORMAL; in allowlist |
| [x] | `fsub` | fp | — | `A_form` 63/20 | kpx: `EXECUTE_FP_ARITH(double, fsub, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=63 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `fsubs` | fp | — | `A_form` 59/20 | kpx: `EXECUTE_FP_ARITH(float, fsub, RD, RA, RB, NONE, RC_BIT_G, true)`; A_form prim=59 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `lfd` | fp | — | `D_form` 50/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, true, true, false)`; D_form prim=50 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lfdu` | fp | — | `D_form` 51/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, true, true, true)`; D_form prim=51 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lfdux` | fp | — | `X_form` 31/631 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, true, true, true)`; X_form prim=31 xo=631 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lfdx` | fp | — | `X_form` 31/599 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, true, true, false)`; X_form prim=31 xo=599 CFLOW_NORMAL; in allowlist |
| [x] | `lfs` | fp | — | `D_form` 48/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, true, false, false)`; D_form prim=48 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lfsu` | fp | — | `D_form` 49/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, true, false, true)`; D_form prim=49 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lfsux` | fp | — | `X_form` 31/567 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, true, false, true)`; X_form prim=31 xo=567 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lfsx` | fp | — | `X_form` 31/535 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, true, false, false)`; X_form prim=31 xo=535 CFLOW_NORMAL; in allowlist |
| [x] | `mcrfs` | fp | — | `X_form` 63/64 | kpx: `EXECUTE_0(mcrfs)`; X_form prim=63 xo=64 CFLOW_NORMAL; in allowlist |
| [x] | `mffs` | fp | — | `X_form` 63/583 | kpx: `EXECUTE_1(mffs, RC_BIT_G)`; X_form prim=63 xo=583 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsb0` | fp | — | `X_form` 63/70 | kpx: `EXECUTE_2(mtfsb, immediate_value<0>, RC_BIT_G)`; X_form prim=63 xo=70 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsb1` | fp | — | `X_form` 63/38 | kpx: `EXECUTE_2(mtfsb, immediate_value<1>, RC_BIT_G)`; X_form prim=63 xo=38 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsf` | fp | — | `XFL_form` 63/711 | kpx: `EXECUTE_3(mtfsf, operand_FM, operand_fp_dw_RB, RC_BIT_G)`; XFL_form prim=63 xo=711 CFLOW_NORMAL; in allowlist |
| [x] | `mtfsfi` | fp | — | `X_form` 63/134 | kpx: `EXECUTE_2(mtfsfi, operand_IMM, RC_BIT_G)`; X_form prim=63 xo=134 CFLOW_NORMAL; in allowlist |
| [x] | `stfd` | fp | — | `D_form` 54/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, false, true, false)`; D_form prim=54 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stfdu` | fp | — | `D_form` 55/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, false, true, true)`; D_form prim=55 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `stfdux` | fp | — | `X_form` 31/759 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, false, true, true)`; X_form prim=31 xo=759 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stfdx` | fp | — | `X_form` 31/727 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, false, true, false)`; X_form prim=31 xo=727 CFLOW_NORMAL; in allowlist |
| [x] | `stfs` | fp | — | `D_form` 52/0 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, D, false, false, false)`; D_form prim=52 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stfsu` | fp | — | `D_form` 53/0 | kpx: `EXECUTE_FP_LOADSTORE(RA, D, false, false, true)`; D_form prim=53 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `stfsux` | fp | — | `X_form` 31/695 | kpx: `EXECUTE_FP_LOADSTORE(RA, RB, false, false, true)`; X_form prim=31 xo=695 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stfsx` | fp | — | `X_form` 31/663 | kpx: `EXECUTE_FP_LOADSTORE(RA_or_0, RB, false, false, false)`; X_form prim=31 xo=663 CFLOW_NORMAL; in allowlist |
| [x] | `add` | integer | — | `XO_form` 31/266 | kpx: `EXECUTE_ADDITION(RA, RB, NONE, CA_BIT_0, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=266 CFLOW_NORMAL; in allowlist |
| [x] | `addc` | integer | — | `XO_form` 31/10 | kpx: `EXECUTE_ADDITION(RA, RB, NONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=10 CFLOW_NORMAL; in allowlist |
| [x] | `adde` | integer | — | `XO_form` 31/138 | kpx: `EXECUTE_ADDITION(RA, RB, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=138 CFLOW_NORMAL; in allowlist |
| [x] | `addi` | integer | — | `D_form` 14/0 | kpx: `EXECUTE_ADDITION(RA_or_0, SIMM, NONE, CA_BIT_0, OE_BIT_0, RC_BIT_0)`; D_form prim=14 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addic` | integer | — | `D_form` 12/0 | kpx: `EXECUTE_ADDITION(RA, SIMM, NONE, CA_BIT_1, OE_BIT_0, RC_BIT_0)`; D_form prim=12 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addic.` | integer | — | `D_form` 13/0 | kpx: `EXECUTE_ADDITION(RA, SIMM, NONE, CA_BIT_1, OE_BIT_0, RC_BIT_1)`; D_form prim=13 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addis` | integer | — | `D_form` 15/0 | kpx: `EXECUTE_ADDITION(RA_or_0, SIMM_shifted, NONE, CA_BIT_0, OE_BIT_0, RC_BIT_0)`; D_form prim=15 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `addme` | integer | — | `XO_form` 31/234 | kpx: `EXECUTE_ADDITION(RA, MINUS_ONE, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=234 CFLOW_NORMAL; in allowlist |
| [x] | `addze` | integer | — | `XO_form` 31/202 | kpx: `EXECUTE_ADDITION(RA, ZERO, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=202 CFLOW_NORMAL; in allowlist |
| [x] | `and` | integer | — | `X_form` 31/28 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=28 CFLOW_NORMAL; in allowlist |
| [x] | `andc` | integer | — | `X_form` 31/60 | kpx: `EXECUTE_GENERIC_ARITH(andc, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=60 CFLOW_NORMAL; in allowlist |
| [x] | `andi.` | integer | — | `D_form` 28/0 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_1)`; D_form prim=28 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `andis.` | integer | — | `D_form` 29/0 | kpx: `EXECUTE_GENERIC_ARITH(and, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_1)`; D_form prim=29 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `cmp` | integer | — | `X_form` 31/0 | kpx: `EXECUTE_COMPARE(RB, int32)`; X_form prim=31 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpi` | integer | — | `D_form` 11/0 | kpx: `EXECUTE_COMPARE(SIMM, int32)`; D_form prim=11 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpl` | integer | — | `X_form` 31/32 | kpx: `EXECUTE_COMPARE(RB, uint32)`; X_form prim=31 xo=32 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [~] | `cmpli` | integer | — | `D_form` 10/0 | kpx: `EXECUTE_COMPARE(UIMM, uint32)`; D_form prim=10 xo=0 CFLOW_NORMAL; L=0 only.; allowlist ok=L=0; fail=L=1; in allowlist |
| [x] | `cntlzw` | integer | — | `X_form` 31/26 | kpx: `EXECUTE_GENERIC_ARITH(cntlzw, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=26 CFLOW_NORMAL; in allowlist |
| [x] | `dcba` | integer | — | `X_form` 31/758 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=758 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbf` | integer | — | `X_form` 31/86 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=86 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbi` | integer | — | `X_form` 31/470 | kpx: `EXECUTE_0(dcbi)`; X_form prim=31 xo=470 CFLOW_NORMAL; in allowlist |
| [x] | `dcbst` | integer | — | `X_form` 31/54 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=54 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbt` | integer | — | `X_form` 31/278 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=278 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbtst` | integer | — | `X_form` 31/246 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=246 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dcbz` | integer | — | `X_form` 31/1014 | kpx: `EXECUTE_2(dcbz, operand_RA_or_0, operand_RB)`; X_form prim=31 xo=1014 CFLOW_NORMAL; in allowlist |
| [x] | `divw` | integer | — | `XO_form` 31/491 | kpx: `EXECUTE_3(divide, true, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=491 CFLOW_NORMAL; in allowlist |
| [x] | `divwu` | integer | — | `XO_form` 31/459 | kpx: `EXECUTE_3(divide, false, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=459 CFLOW_NORMAL; in allowlist |
| [x] | `dss` | integer | — | `X_form` 31/822 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=822 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dst` | integer | — | `X_form` 31/342 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=342 CFLOW_NORMAL; kpx nop.; in allowlist |
| [x] | `dstst` | integer | — | `X_form` 31/374 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=374 CFLOW_NORMAL; kpx nop.; in allowlist |
| [-] | `eciwx` | integer | exclude | `X_form` 31/310 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=310 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported` |
| [-] | `ecowx` | integer | exclude | `X_form` 31/438 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=438 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported` |
| [x] | `eieio` | integer | — | `X_form` 31/854 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=854 CFLOW_NORMAL; in allowlist |
| [x] | `eqv` | integer | — | `X_form` 31/284 | kpx: `EXECUTE_GENERIC_ARITH(eqv, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=284 CFLOW_NORMAL; in allowlist |
| [x] | `extsb` | integer | — | `X_form` 31/954 | kpx: `EXECUTE_GENERIC_ARITH(sign_extend_8_32, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=954 CFLOW_NORMAL; in allowlist |
| [x] | `extsh` | integer | — | `X_form` 31/922 | kpx: `EXECUTE_GENERIC_ARITH(sign_extend_16_32, RA, RS, NONE, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=922 CFLOW_NORMAL; in allowlist |
| [~] | `icbi` | integer | — | `X_form` 31/982 | kpx: `EXECUTE_2(icbi, operand_RA_or_0, operand_RB)`; X_form prim=31 xo=982 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `mfvscr` | integer | — | `VX_form` 4/1540 | kpx: `EXECUTE_0(mfvscr)`; VX_form prim=4 xo=1540 CFLOW_NORMAL; in allowlist |
| [x] | `mtvscr` | integer | — | `VX_form` 4/1604 | kpx: `EXECUTE_0(mtvscr)`; VX_form prim=4 xo=1604 CFLOW_NORMAL; in allowlist |
| [x] | `mulhw` | integer | — | `XO_form` 31/75 | kpx: `EXECUTE_4(multiply, true, true, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=75 CFLOW_NORMAL; in allowlist |
| [x] | `mulhwu` | integer | — | `XO_form` 31/11 | kpx: `EXECUTE_4(multiply, true, false, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=11 CFLOW_NORMAL; in allowlist |
| [x] | `mulli` | integer | — | `D_form` 7/0 | kpx: `EXECUTE_GENERIC_ARITH(smul, RD, RA, SIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=7 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `mullw` | integer | — | `XO_form` 31/235 | kpx: `EXECUTE_4(multiply, false, true, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=235 CFLOW_NORMAL; in allowlist |
| [x] | `nand` | integer | — | `X_form` 31/476 | kpx: `EXECUTE_GENERIC_ARITH(nand, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=476 CFLOW_NORMAL; in allowlist |
| [x] | `neg` | integer | — | `XO_form` 31/104 | kpx: `EXECUTE_GENERIC_ARITH(neg, RD, RA, NONE, NONE, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=104 CFLOW_NORMAL; in allowlist |
| [x] | `nor` | integer | — | `XO_form` 31/124 | kpx: `EXECUTE_GENERIC_ARITH(nor, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=124 CFLOW_NORMAL; in allowlist |
| [x] | `or` | integer | — | `XO_form` 31/444 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=444 CFLOW_NORMAL; in allowlist |
| [x] | `orc` | integer | — | `XO_form` 31/412 | kpx: `EXECUTE_GENERIC_ARITH(orc, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; XO_form prim=31 xo=412 CFLOW_NORMAL; in allowlist |
| [x] | `ori` | integer | — | `D_form` 24/0 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=24 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `oris` | integer | — | `D_form` 25/0 | kpx: `EXECUTE_GENERIC_ARITH(or, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=25 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwimi` | integer | — | `M_form` 20/0 | kpx: `EXECUTE_3(rlwimi, operand_SH, operand_MASK, RC_BIT_G)`; M_form prim=20 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwinm` | integer | — | `M_form` 21/0 | kpx: `EXECUTE_GENERIC_ARITH(ppc_rlwinm, RA, RS, SH, MASK, OE_BIT_0, RC_BIT_G)`; M_form prim=21 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `rlwnm` | integer | — | `M_form` 23/0 | kpx: `EXECUTE_GENERIC_ARITH(ppc_rlwnm, RA, RS, RB, MASK, OE_BIT_0, RC_BIT_G)`; M_form prim=23 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `slw` | integer | — | `X_form` 31/24 | kpx: `EXECUTE_SHIFT(shll, RA, RS, RB, andi<0x3f>, CA_BIT_0, RC_BIT_G)`; X_form prim=31 xo=24 CFLOW_NORMAL; in allowlist |
| [x] | `sraw` | integer | — | `X_form` 31/792 | kpx: `EXECUTE_SHIFT(shra, RA, RS, RB, andi<0x3f>, CA_BIT_1, RC_BIT_G)`; X_form prim=31 xo=792 CFLOW_NORMAL; in allowlist |
| [x] | `srawi` | integer | — | `X_form` 31/824 | kpx: `EXECUTE_SHIFT(shra, RA, RS, SH, andi<0x1f>, CA_BIT_1, RC_BIT_G)`; X_form prim=31 xo=824 CFLOW_NORMAL; in allowlist |
| [x] | `srw` | integer | — | `X_form` 31/536 | kpx: `EXECUTE_SHIFT(shrl, RA, RS, RB, andi<0x3f>, CA_BIT_0, RC_BIT_G)`; X_form prim=31 xo=536 CFLOW_NORMAL; in allowlist |
| [x] | `subf` | integer | — | `XO_form` 31/40 | kpx: `EXECUTE_ADDITION(RA_compl, RB, ONE, CA_BIT_0, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=40 CFLOW_NORMAL; in allowlist |
| [x] | `subfc` | integer | — | `XO_form` 31/8 | kpx: `EXECUTE_ADDITION(RA_compl, RB, ONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=8 CFLOW_NORMAL; in allowlist |
| [x] | `subfe` | integer | — | `XO_form` 31/136 | kpx: `EXECUTE_ADDITION(RA_compl, RB, XER_CA, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=136 CFLOW_NORMAL; in allowlist |
| [x] | `subfic` | integer | — | `D_form` 8/0 | kpx: `EXECUTE_ADDITION(RA_compl, SIMM, ONE, CA_BIT_1, OE_BIT_0, RC_BIT_0)`; D_form prim=8 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `subfme` | integer | — | `XO_form` 31/232 | kpx: `EXECUTE_ADDITION(RA_compl, XER_CA, MINUS_ONE, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=232 CFLOW_NORMAL; in allowlist |
| [x] | `subfze` | integer | — | `XO_form` 31/200 | kpx: `EXECUTE_ADDITION(RA_compl, XER_CA, ZERO, CA_BIT_1, OE_BIT_G, RC_BIT_G)`; XO_form prim=31 xo=200 CFLOW_NORMAL; in allowlist |
| [x] | `sync` | integer | — | `X_form` 31/598 | kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=598 CFLOW_NORMAL; in allowlist |
| [~] | `tlbia` | integer | — | `X_form` 31/370 | kpx: `EXECUTE_0(tlbia)`; X_form prim=31 xo=370 CFLOW_NORMAL; ends_block.; in allowlist |
| [~] | `tlbie` | integer | — | `X_form` 31/306 | kpx: `EXECUTE_0(tlbie)`; X_form prim=31 xo=306 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `tlbsync` | integer | — | `X_form` 31/566 | kpx: `EXECUTE_0(tlbsync)`; X_form prim=31 xo=566 CFLOW_NORMAL; in allowlist |
| [x] | `tw` | integer | — | `X_form` 31/4 | kpx: `EXECUTE_0(trap)`; X_form prim=31 xo=4 CFLOW_TRAP; in allowlist |
| [x] | `twi` | integer | — | `D_form` 3/0 | kpx: `EXECUTE_0(trap)`; D_form prim=3 xo=0 CFLOW_TRAP; in allowlist |
| [x] | `xor` | integer | — | `X_form` 31/316 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, RB, NONE, OE_BIT_0, RC_BIT_G)`; X_form prim=31 xo=316 CFLOW_NORMAL; in allowlist |
| [x] | `xori` | integer | — | `D_form` 26/0 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, UIMM, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=26 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `xoris` | integer | — | `D_form` 27/0 | kpx: `EXECUTE_GENERIC_ARITH(xor, RA, RS, UIMM_shifted, NONE, OE_BIT_0, RC_BIT_0)`; D_form prim=27 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lbz` | mem | — | `D_form` 34/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 1, false, false)`; D_form prim=34 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lbzu` | mem | — | `D_form` 35/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 1, true, false)`; D_form prim=35 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lbzux` | mem | — | `X_form` 31/119 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 1, true, false)`; X_form prim=31 xo=119 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lbzx` | mem | — | `X_form` 31/87 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 1, false, false)`; X_form prim=31 xo=87 CFLOW_NORMAL; in allowlist |
| [x] | `lha` | mem | — | `D_form` 42/0 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA_or_0, D, true, 2, false, false)`; D_form prim=42 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lhau` | mem | — | `D_form` 43/0 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA, D, true, 2, true, false)`; D_form prim=43 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lhaux` | mem | — | `X_form` 31/375 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA, RB, true, 2, true, false)`; X_form prim=31 xo=375 CFLOW_NORMAL; in allowlist |
| [x] | `lhax` | mem | — | `X_form` 31/343 | kpx: `EXECUTE_LOADSTORE(sign_extend_16_32, RA_or_0, RB, true, 2, false, false)`; X_form prim=31 xo=343 CFLOW_NORMAL; in allowlist |
| [x] | `lhbrx` | mem | — | `X_form` 31/790 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 2, false, true)`; X_form prim=31 xo=790 CFLOW_NORMAL; in allowlist |
| [x] | `lhz` | mem | — | `D_form` 40/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 2, false, false)`; D_form prim=40 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lhzu` | mem | — | `D_form` 41/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 2, true, false)`; D_form prim=41 xo=0 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [~] | `lhzux` | mem | — | `X_form` 31/311 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 2, true, false)`; X_form prim=31 xo=311 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lhzx` | mem | — | `X_form` 31/279 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 2, false, false)`; X_form prim=31 xo=279 CFLOW_NORMAL; in allowlist |
| [x] | `lmw` | mem | — | `D_form` 46/0 | kpx: `EXECUTE_LOADSTORE_MULTIPLE(RA_or_0, D, true)`; D_form prim=46 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lswi` | mem | — | `X_form` 31/597 | kpx: `EXECUTE_LOAD_STRING(RA_or_0, true, NB)`; X_form prim=31 xo=597 CFLOW_NORMAL; in allowlist |
| [x] | `lswx` | mem | — | `X_form` 31/533 | kpx: `EXECUTE_LOAD_STRING(RA_or_0, false, XER_COUNT)`; X_form prim=31 xo=533 CFLOW_NORMAL; in allowlist |
| [x] | `lwarx` | mem | — | `X_form` 31/20 | kpx: `EXECUTE_1(lwarx, operand_RA_or_0)`; X_form prim=31 xo=20 CFLOW_NORMAL; in allowlist |
| [x] | `lwbrx` | mem | — | `X_form` 31/534 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 4, false, true)`; X_form prim=31 xo=534 CFLOW_NORMAL; in allowlist |
| [x] | `lwz` | mem | — | `D_form` 32/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, true, 4, false, false)`; D_form prim=32 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `lwzu` | mem | — | `D_form` 33/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, true, 4, true, false)`; D_form prim=33 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `lwzux` | mem | — | `X_form` 31/55 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, true, 4, true, false)`; X_form prim=31 xo=55 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `lwzx` | mem | — | `X_form` 31/23 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, true, 4, false, false)`; X_form prim=31 xo=23 CFLOW_NORMAL; in allowlist |
| [x] | `stb` | mem | — | `D_form` 38/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 1, false, false)`; D_form prim=38 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stbu` | mem | — | `D_form` 39/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 1, true, false)`; D_form prim=39 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stbux` | mem | — | `X_form` 31/247 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 1, true, false)`; X_form prim=31 xo=247 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stbx` | mem | — | `X_form` 31/215 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 1, false, false)`; X_form prim=31 xo=215 CFLOW_NORMAL; in allowlist |
| [x] | `sth` | mem | — | `D_form` 44/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 2, false, false)`; D_form prim=44 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `sthbrx` | mem | — | `X_form` 31/918 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 2, false, true)`; X_form prim=31 xo=918 CFLOW_NORMAL; in allowlist |
| [x] | `sthu` | mem | — | `D_form` 45/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 2, true, false)`; D_form prim=45 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `sthux` | mem | — | `X_form` 31/439 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 2, true, false)`; X_form prim=31 xo=439 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `sthx` | mem | — | `X_form` 31/407 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 2, false, false)`; X_form prim=31 xo=407 CFLOW_NORMAL; in allowlist |
| [x] | `stmw` | mem | — | `D_form` 47/0 | kpx: `EXECUTE_LOADSTORE_MULTIPLE(RA_or_0, D, false)`; D_form prim=47 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stswi` | mem | — | `X_form` 31/725 | kpx: `EXECUTE_STORE_STRING(RA_or_0, true, NB)`; X_form prim=31 xo=725 CFLOW_NORMAL; in allowlist |
| [x] | `stswx` | mem | — | `X_form` 31/661 | kpx: `EXECUTE_STORE_STRING(RA_or_0, false, XER_COUNT)`; X_form prim=31 xo=661 CFLOW_NORMAL; in allowlist |
| [x] | `stw` | mem | — | `D_form` 36/0 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, D, false, 4, false, false)`; D_form prim=36 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `stwbrx` | mem | — | `X_form` 31/662 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 4, false, true)`; X_form prim=31 xo=662 CFLOW_NORMAL; in allowlist |
| [x] | `stwcx.` | mem | — | `X_form` 31/150 | kpx: `EXECUTE_1(stwcx, operand_RA_or_0)`; X_form prim=31 xo=150 CFLOW_NORMAL; in allowlist |
| [x] | `stwu` | mem | — | `D_form` 37/0 | kpx: `EXECUTE_LOADSTORE(nop, RA, D, false, 4, true, false)`; D_form prim=37 xo=0 CFLOW_NORMAL; in allowlist |
| [~] | `stwux` | mem | — | `X_form` 31/183 | kpx: `EXECUTE_LOADSTORE(nop, RA, RB, false, 4, true, false)`; X_form prim=31 xo=183 CFLOW_NORMAL; RA≠0 required.; allowlist ok=RA≠0; fail=RA=0; in allowlist |
| [x] | `stwx` | mem | — | `X_form` 31/151 | kpx: `EXECUTE_LOADSTORE(nop, RA_or_0, RB, false, 4, false, false)`; X_form prim=31 xo=151 CFLOW_NORMAL; in allowlist |
| [-] | `invalid` | none | exclude | `INVALID_form` 0/0 | kpx: `EXECUTE_0(illegal)`; INVALID_form prim=0 xo=0 CFLOW_TRAP; Not a real insn. Prim=6 skip noise is usually this class.; not in `nw_jit_op_supported` |
| [x] | `mcrf` | spr | — | `XL_form` 19/0 | kpx: `EXECUTE_0(mcrf)`; XL_form prim=19 xo=0 CFLOW_NORMAL; in allowlist |
| [x] | `mcrxr` | spr | — | `X_form` 31/512 | kpx: `EXECUTE_0(mcrxr)`; X_form prim=31 xo=512 CFLOW_NORMAL; in allowlist |
| [x] | `mfcr` | spr | — | `X_form` 31/19 | kpx: `EXECUTE_GENERIC_ARITH(nop, RD, CR, NONE, NONE, OE_BIT_0, RC_BIT_0)`; X_form prim=31 xo=19 CFLOW_NORMAL; in allowlist |
| [x] | `mfmsr` | spr | — | `X_form` 31/83 | kpx: `EXECUTE_0(mfmsr)`; X_form prim=31 xo=83 CFLOW_NORMAL; in allowlist |
| [~] | `mfspr` | spr | — | `XFX_form` 31/339 | kpx: `EXECUTE_1(mfspr, operand_SPR)`; XFX_form prim=31 xo=339 CFLOW_NORMAL; Typed system reads; block boundaries depend on SPR.; in allowlist |
| [x] | `mfsr` | spr | — | `X_form` 31/595 | kpx: `EXECUTE_0(mfsr)`; X_form prim=31 xo=595 CFLOW_NORMAL; in allowlist |
| [x] | `mfsrin` | spr | — | `X_form` 31/659 | kpx: `EXECUTE_0(mfsrin)`; X_form prim=31 xo=659 CFLOW_NORMAL; in allowlist |
| [~] | `mftb` | spr | — | `XFX_form` 31/371 | kpx: `EXECUTE_1(mftbr, operand_TBR)`; XFX_form prim=31 xo=371 CFLOW_NORMAL; TBL/TBU only.; in allowlist |
| [x] | `mtcrf` | spr | — | `XFX_form` 31/144 | kpx: `EXECUTE_0(mtcrf)`; XFX_form prim=31 xo=144 CFLOW_NORMAL; in allowlist |
| [~] | `mtmsr` | spr | — | `X_form` 31/146 | kpx: `EXECUTE_0(mtmsr)`; X_form prim=31 xo=146 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [x] | `mtspr` | spr | — | `XFX_form` 31/467 | kpx: `EXECUTE_1(mtspr, operand_SPR)`; XFX_form prim=31 xo=467 CFLOW_NORMAL; Typed system writes; non-user except VRSAVE ends block.; in allowlist |
| [~] | `mtsr` | spr | — | `X_form` 31/210 | kpx: `EXECUTE_0(mtsr)`; X_form prim=31 xo=210 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |
| [~] | `mtsrin` | spr | — | `X_form` 31/242 | kpx: `EXECUTE_0(mtsrin)`; X_form prim=31 xo=242 CFLOW_NORMAL; ends_block.; **ends_block**; in allowlist |

## Todo / partial by wave

### W6 (4)

- [~] `lvx` — kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=103 CFLOW_NORMAL; hint ignored (lvxl same path).; in allowlist
- [~] `lvxl` — kpx: `EXECUTE_VECTOR_LOADSTORE(load, V2DI, RA_or_0, RB)`; X_form prim=31 xo=359 CFLOW_NORMAL; hint ignored.; in allowlist
- [~] `stvx` — kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=231 CFLOW_NORMAL; hint ignored.; in allowlist
- [~] `stvxl` — kpx: `EXECUTE_VECTOR_LOADSTORE(store, V2DI, RA_or_0, RB)`; X_form prim=31 xo=487 CFLOW_NORMAL; hint ignored.; in allowlist

### exclude (3)

- [-] `eciwx` — kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=310 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported`
- [-] `ecowx` — kpx: `EXECUTE_0(nop)`; X_form prim=31 xo=438 CFLOW_NORMAL; External control; no device model.; not in `nw_jit_op_supported`
- [-] `invalid` — kpx: `EXECUTE_0(illegal)`; INVALID_form prim=0 xo=0 CFLOW_TRAP; Not a real insn. Prim=6 skip noise is usually this class.; not in `nw_jit_op_supported`

## Accepted names (dispatch coverage)

Quick list of `[x]` names for scanning:

`add`, `addc`, `adde`, `addi`, `addic`, `addic.`, `addis`, `addme`, `addze`, `and`
`andc`, `andi.`, `andis.`, `b`, `bc`, `bcctr`, `bclr`, `cntlzw`, `crand`, `crandc`
`creqv`, `crnand`, `crnor`, `cror`, `crorc`, `crxor`, `dcba`, `dcbf`, `dcbi`, `dcbst`
`dcbt`, `dcbtst`, `dcbz`, `divw`, `divwu`, `dss`, `dst`, `dstst`, `eieio`, `eqv`
`extsb`, `extsh`, `fabs`, `fadd`, `fadds`, `fcmpo`, `fcmpu`, `fctiw`, `fctiwz`, `fdiv`
`fdivs`, `fmadd`, `fmadds`, `fmr`, `fmsub`, `fmsubs`, `fmul`, `fmuls`, `fnabs`, `fneg`
`fnmadd`, `fnmadds`, `fnmsub`, `fnmsubs`, `fres`, `frsp`, `frsqrte`, `fsel`, `fsub`, `fsubs`
`isync`, `lbz`, `lbzu`, `lbzx`, `lfd`, `lfdx`, `lfs`, `lfsx`, `lha`, `lhau`
`lhaux`, `lhax`, `lhbrx`, `lhz`, `lhzx`, `lmw`, `lswi`, `lswx`, `lvebx`, `lvehx`
`lvewx`, `lvsl`, `lvsr`, `lwarx`, `lwbrx`, `lwz`, `lwzu`, `lwzx`, `mcrf`, `mcrfs`
`mcrxr`, `mfcr`, `mffs`, `mfmsr`, `mfsr`, `mfsrin`, `mfvscr`, `mtcrf`, `mtfsb0`, `mtfsb1`
`mtfsf`, `mtfsfi`, `mtspr`, `mtvscr`, `mulhw`, `mulhwu`, `mulli`, `mullw`, `nand`, `neg`
`nor`, `or`, `orc`, `ori`, `oris`, `rlwimi`, `rlwinm`, `rlwnm`, `sc`, `slw`
`sraw`, `srawi`, `srw`, `stb`, `stbu`, `stbx`, `stfd`, `stfdx`, `stfs`, `stfsx`
`sth`, `sthbrx`, `sthu`, `sthx`, `stmw`, `stswi`, `stswx`, `stvebx`, `stvehx`, `stvewx`
`stw`, `stwbrx`, `stwcx.`, `stwu`, `stwx`, `subf`, `subfc`, `subfe`, `subfic`, `subfme`
`subfze`, `sync`, `tlbsync`, `tw`, `twi`, `vaddcuw`, `vaddfp`, `vaddsbs`, `vaddshs`, `vaddsws`
`vaddubm`, `vaddubs`, `vadduhm`, `vadduhs`, `vadduwm`, `vadduws`, `vand`, `vandc`, `vavgsb`, `vavgsh`
`vavgsw`, `vavgub`, `vavguh`, `vavguw`, `vcfsx`, `vcfux`, `vcmpbfp`, `vcmpeqfp`, `vcmpequb`, `vcmpequh`
`vcmpequw`, `vcmpgefp`, `vcmpgtfp`, `vcmpgtsb`, `vcmpgtsh`, `vcmpgtsw`, `vcmpgtub`, `vcmpgtuh`, `vcmpgtuw`, `vctsxs`
`vctuxs`, `vexptefp`, `vlogefp`, `vmaddfp`, `vmaxfp`, `vmaxsb`, `vmaxsh`, `vmaxsw`, `vmaxub`, `vmaxuh`
`vmaxuw`, `vmhaddshs`, `vmhraddshs`, `vminfp`, `vminsb`, `vminsh`, `vminsw`, `vminub`, `vminuh`, `vminuw`
`vmladduhm`, `vmrghb`, `vmrghh`, `vmrghw`, `vmrglb`, `vmrglh`, `vmrglw`, `vmsummbm`, `vmsumshm`, `vmsumshs`
`vmsumubm`, `vmsumuhm`, `vmsumuhs`, `vmulesb`, `vmulesh`, `vmuleub`, `vmuleuh`, `vmulosb`, `vmulosh`, `vmuloub`
`vmulouh`, `vnmsubfp`, `vnor`, `vor`, `vperm`, `vpkpx`, `vpkshss`, `vpkshus`, `vpkswss`, `vpkswus`
`vpkuhum`, `vpkuhus`, `vpkuwum`, `vpkuwus`, `vrefp`, `vrfim`, `vrfin`, `vrfip`, `vrfiz`, `vrlb`
`vrlh`, `vrlw`, `vrsqrtefp`, `vsel`, `vsl`, `vslb`, `vsldoi`, `vslh`, `vslo`, `vslw`
`vspltb`, `vsplth`, `vspltisb`, `vspltish`, `vspltisw`, `vspltw`, `vsr`, `vsrab`, `vsrah`, `vsraw`
`vsrb`, `vsrh`, `vsro`, `vsrw`, `vsubcuw`, `vsubfp`, `vsubsbs`, `vsubshs`, `vsubsws`, `vsububm`
`vsububs`, `vsubuhm`, `vsubuhs`, `vsubuwm`, `vsubuws`, `vsum2sws`, `vsum4sbs`, `vsum4shs`, `vsum4ubs`, `vsumsws`
`vupkhpx`, `vupkhsb`, `vupkhsh`, `vupklpx`, `vupklsb`, `vupklsh`, `vxor`, `xor`, `xori`, `xoris`

## Notes on reading encodings

- **prim/xo** come from the kpx decode table (`D_form` xo is often 0; real primary opcode is `prim`).
- **AltiVec VX/VXR**: table `xo` is the 11-bit vector opcode field (`op & 0x7ff`).
- **AltiVec VA**: table `xo` is the 6-bit VA opcode (`op & 0x3f`); allowlist also has a small VA set (vperm/vsldoi/…).
- **kpx** line is the interpreter execute template — use it as the semantic oracle when milling.
- Status is derived from a Python port of `nw_jit_op_supported`; keep the C function as source of truth.
