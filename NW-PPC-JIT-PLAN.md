# New World PPC JIT implementation plan

Started October 2, 2026, from the work sequence in [the PPC JIT report](NW-PPC-JIT-REPORT.md). The report's September 30 findings are the review baseline, not the current implementation status.

## Execution order

| Stage | Work and completion gate | Current status |
|---|---|---|
| 1a: memory exits | Precise PC, logical EA, width and direction; failed accesses preserve architectural outputs; successful prefixes remain; completed SMC stores exit after committing | Initial implementation and generated-code regressions pass; broader multi-access and live exception delivery audits remain |
| 1b: FP environment | Guest rounding across interpreter/JIT/helper transitions, FPSCR writes and summaries, host environment restoration; independent expected-value and interpreter tests | Rounding/environment ownership and FPSCR controls implemented; independent helper/native/KPX tests pass; broader arithmetic conformance remains |
| 1c: DEC/events | Sign-transition requests, delivery cancellation, EE/exception/DEC-write/nap behavior; common pending-event guards for both chain paths | Edge-triggered DEC, sampled privileged accesses and shared chain-event guard implemented; deterministic real-CPU checks pass; private 300-second desktop/ExtFS/IRQ regression passes; long nap and maintenance-latency stress remain |
| 2: independent validation | Isolated CPU, RAM, translation and reservation effects; devices execute once; capture and minimize replay; test production selective masks and both chain paths | Independent KPX fixture and isolated scalar/FP/multiple-access live replay implemented; captures replay without a VM; privileged/atomic/VMX observations, selective masks and chains remain |
| 3: measurement | Accurate retired-instruction and dispatch definitions, helper and maintenance latency; repeated serial Release interpreter/JIT workloads with identical cloned media | Pending |
| 4: cache and transitions | Page ownership, executable-address lifetime/epochs, bounded collection and a common successor policy; SMC/remap/compaction and latency stress | Pending |
| 5: acceleration | Measured vector masks/residency, selected helpers, guarded branches and local optimizations; independent equivalence plus application improvement | Pending |
| 6: cross-engine integration | Shared code-write notifications, publication/lifetime and fault conventions; PPC/68k writes invalidate both engines | Code-publication notifications now reach both New World engines; broader cross-engine stress remains |

Correctness and independent validation precede new optimizations. Existing PPC and host 68k JIT defaults remain enabled; diagnostic overrides and explicit preference disables retain their meanings. Candidate builds and guest tests use private build directories and cloned VM media.

## First memory milestone

All memory helper exits now use a common record: the emitter-maintained faulting `pc`, exit cause, logical `fault_ea`, `fault_width` and `fault_st`. Live DSI recovery uses the recorded subaccess width instead of reconstructing it from an incomplete opcode list. Physical fast-path helpers receive the original logical address as well as PA.

Failed atomic stores preserve CR0 and the shadow reservation. A completed conditional store that modifies executing code commits EQ/SO and releases the reservation, then exits on SMC. FP/vector callback outputs are staged privately until success. `lmw` retains successful earlier register loads while preserving the destination whose access failed. RAM bounds arithmetic uses widened values so a high address cannot wrap into the fixture's valid range.

Regression testing also found an ARM64 encoding that compared W8 although the fault status was in W10. All eleven affected update checks now encode their intended register explicitly. Store-update SMC tests cover each supported store form.

The live vector-store callback previously reused scalar store callbacks and could stop after its first word on SMC. It now completes the probed aligned 16-byte store before invalidating and reporting the exit. Broader string/multiple/cache-line partial-completion semantics remain an audit item; this milestone does not certify all multi-access instructions.

Release qualification exposed a separate AltiVec startup-probe ABI defect. Its generated function now preserves x19, which is callee-saved under AAPCS64. The probe mapping is scoped so every return releases it and restores the enclosing compiler's writable-code state. Acceleration remains enabled during normal regression runs.

## Evidence and remaining checks

The initial 48-form callback-failure matrix produced 468 failed assertions before the fix. It runs each form at three block positions with both DSI and delegated-I/O exits, deliberately poisons failed load outputs, and checks precise metadata, registers, successful prefixes and suppressed suffixes. These assertions use explicit expected state rather than the JIT's shared helper oracle.

The expanded official Debug and Release targets each pass **7,070 checks with zero failures**, including SMC update behavior, logical/physical aliases, partial `lmw`, address-wrap rejection and complete RAM FP/vector stores. The Release run uses normal acceleration settings. Evidence:

- `/tmp/macemu-ppc-fault-baseline.log`: reproduced pre-fix failures.
- `/tmp/macemu-ppc-fault-fixed.log`: final Debug target results.
- `/tmp/macemu-ppc-stage1-release-tests.log`: final Release target results.
- `/tmp/macemu-ppc-stage1-core-sanitize.log`: 390,900 68k checks under ASan/UBSan, including 150,377 decoded-ROM comparisons; zero failures.
- `/tmp/g8/ppc-stage1-faults/results.json`: private guest integration passes with PPC/native 68k enabled and full 68k block/scalar comparison. It completes 300 workload seconds, actual share creation, all 20 requested interrupts and 16 scripted mouse targets. Finder and heartbeat are confirmed; guest system errors, unexpected CPU returns, block mismatches, unresolved interrupt stalls and mouse failures are zero. Maximum observed VBL service latency is 16,449 microseconds.

The integration run uses the Debug memory-fix candidate built before the separate AltiVec probe ABI/cleanup repair. Final Debug/Release host targets and the final Release app build pass after that repair. This heavily instrumented guest run takes 283.03 seconds to reach readiness and is correctness evidence, not a controlled performance comparison. Performance acceptance remains stage 3.

The host-side MMUTests target uses checked-in platform fixtures. It does not instantiate a complete running CPU/device machine for every new fault case. Live DAR/DSISR/exception-vector comparisons, faulted chain transitions and reservation/device side effects require additional integration coverage. Live PPC VERIFY is not accepted as a correctness certificate until stage 2 isolates its effects.

## FP environment and timer milestone

Control-register expectations follow the [PowerPC Programming Environments manual](https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf), notably FPSCR controls and instruction listings in chapters 2 and 8, and decrementer request/delivery semantics in chapters 2 and 6.

Scalar FP instructions now own a bounded host FP environment. The KPX interpreter and JIT arithmetic helpers select RN from guest FPSCR for the instruction and restore both host control and accrued exception flags on return. ARM64 saves FPCR/FPSR directly and disables host traps, flush-to-zero and default-NaN during guest arithmetic. The optional inline scalar path follows the same policy, restores host state before its classification helper, and retains double FPR operands until result rounding. Fused multiply-add emission remains fused. Integer blocks and host callbacks retain the caller's environment, and nested FP scopes restore their outer owner.

FPSCR writes recompute the derived FEX/VX summaries independently of the optional arithmetic-exception tracking build switch. Direct writes cannot manufacture those summary bits. `mtfsb1` raises sticky FX when a new exception cause is set; `mcrfs` recomputes summaries after clearing the copied causes. Independent KPX tests additionally found and fixed two `mtfsfi` defects: field immediates were not shifted into their destination, and field-zero writes incorrectly excluded FX. Record forms receive the resulting summary nibble.

DEC requests now arise on timer/sign transitions and remain pending until delivery. A still-negative DEC does not request another exception after delivery. Full 64-bit elapsed time detects a crossing even across an entire 32-bit counter wrap. DEC samples use the host clock, so software TB writes do not advance or rewind DEC. The interpreter sampling divider belongs to each CPU instance.

Live JIT DEC reads and writes now use a CPU-owned callback at the instruction, including privilege checks, elapsed-time sampling and requests from writes. Each access ends its block; intermediate writes cannot disappear inside a shadow or be reapplied by a later chain commit. Both native tail calls and the C successor loop use one pending-event guard for enabled external/DEC exceptions and actionable CPU special flags. The existing internal KPX JIT-exit flag remains distinct from a host/guest event.

The host suite exercises all four guest RN modes against all four host rounding modes, three FPSCR writer forms, positive/negative boundaries, integer conversions, division, and cancellation-sensitive fused multiply-add variants. It uses literal expected IEEE results, checks host callbacks after FP work and fault returns, and covers nested scopes, host FZ/DN controls, signaling-NaN comparisons, subnormal classification and explicit FPSCR/CR expectations. These tests run with scalar inlining both off and on.

`tools/run_ppc_core_tests.py --build-log <isolated-app-build.log>` links a private alternate entry point against the actual KPX decoder/instruction objects and freshly compiled CPU integration. It replaces only the CPU's host-clock reference with a deterministic test clock. The executable initializes no UI or guest media. Its independent expected-state cases cover RN, FPSCR summaries, DEC crossing/wrap/delivery, retained requests across positive writes and EE masking, external priority, rfi, CPU flags, TB writes and generated DEC privilege/fault exits. This is independent KPX execution coverage; it does not repair the separate live PPC VERIFY isolation defect.

Current host evidence:

- Debug and Release MMUTests, each with scalar inlining off and on: **34,585 passed, zero failures**. `/tmp/macemu-ppc-fp-dec-{debug,release}-tests.log` and `/tmp/macemu-ppc-fp-dec-inline-{debug,release}-tests.log`.
- Real KPX/CPU fixture: **6,420 passed, zero failures** in both Debug and Release. `/tmp/macemu-ppc-fp-dec-final-release-core-tests.log`, `/tmp/macemu-ppc-fp-dec-debug-core-tests.log`.
- 68k ASan/UBSan and ROM differential: **390,900 passed, zero failures**, including 150,377 decoded-ROM comparisons. `/tmp/macemu-ppc-fp-dec-nw68-sanitize.log`.
- Private Debug guest integration: `/tmp/g8/ppc-fp-dec-guest/results.json` passes. Finder is confirmed after 272.64 seconds, followed by all 300 workload seconds, actual share creation, 20/20 injected interrupt IACK/EOI sequences and 16 scripted mouse targets. The run executes 325,059,861 native **host 68k** instructions with full 68k comparison enabled. Guest errors, unexpected CPU returns, mismatches, IRQ stalls, unresolved stalls and mouse failures are zero; maximum observed VBL service latency is 18,095 microseconds. This instrumented run is correctness evidence, not a controlled performance result. Its app predates the final optional-inline fused-emission adjustment; that opt-in path is covered separately by the host matrices. The initial sandboxed attempt could not open `/dev/zero` and executed no guest instructions; it is excluded from qualification.

These changes close the reproduced RN-control failure and the repeated-negative-DEC request defect. They do not certify full floating-point arithmetic: all NaN payloads, invalid conversions, FR/FI, sticky arithmetic exceptions and enabled exceptions still need broader tests. Long idle/nap stress and precise event latency across cache maintenance remain follow-up gates. Existing PPC/68k defaults remain enabled. Live PPC VERIFY isolation is the next implementation stage; no controlled application-performance improvement is claimed by this milestone.

Final isolated Debug and Release app builds succeed: `/tmp/macemu-ppc-fp-dec-final-debug-app-build.log` and `/tmp/macemu-ppc-fp-dec-final-release-app-build-2.log`. Candidates are under `/tmp/macemu-nw68-preview-{debug,release}-dd/Build/Products/{Debug,Release}/SheepShaver.app`; the user's usual DerivedData launcher binary has not been replaced by this qualification workflow.


## Isolated live VERIFY milestone

VERIFY now executes the actual KPX interpreter first and keeps that result authoritative. It records ordered logical addresses, instruction PCs, widths, directions, returned values, stores and translation failures. The native shadow then consumes the observations with no live CPU, RAM pointer, device callback or populated DTLB. It compares store values and the complete observation sequence as well as registers and precise exits. Wrong addresses, order, direction, widths, missing accesses or store values fail comparison. Reference effects never run twice, including after a native replay failure.

This first allowlist covers supported scalar integer/FP calculations, branches, user LR/CTR/XER access, scalar integer/FP memory and multiple-register memory. Privileged/timer, reservation/atomic, string, delegated VMX and vector memory operations are explicitly counted as `skip_effect`, not as verified passes. Guarded native helpers also reject those operations during replay: a stale compiled block must not mutate shared translation generations or flush live caches simply because its newly fetched reference opcodes are safe. The native shadow still loads full register state and disables both chain paths. Production selective masks, chains, independent translation replay and broader system effects remain completion gates for stage 2.

Captures are bounded to sixteen distinct failing PCs per process at `/tmp/nw-ppc-verify-<pid>-<capture>.txt`. They contain input/reference/native registers, reference instructions, cached first opcode, fault metadata and access observations, with no host addresses. `tools/run_ppc_core_tests.py --build-log <complete-isolated-app-build-log> --replay <capture>` regenerates native code and compares it against the captured reference without a VM. A stale cache entry may disappear on fresh compilation; that distinction is diagnostic evidence rather than an instruction-correctness failure being suppressed. Automatic reduction and capture of the complete originally cached source remain future work. The checked-in `ppc_verify_fctiwz.capture` is a manually reduced two-instruction regression and runs automatically with the core suite.

The new verifier exposed three concrete issues:

- `fctiw`/`fctiwz` used different upper FPR words in KPX and the JIT. The ISA leaves that word unspecified; both engines now choose signed extension consistently, including KPX's invalid-result branch. JIT NaN conversion explicitly produces the integer-indefinite word instead of invoking an undefined host float-to-integer cast. Defined low-word integer results are unchanged. This removes differences in subsequent full-width stores; it is a consistent emulation policy, not evidence of an ISA upper-word defect.
- `stmw` stopped at its first completed SMC store. It now completes the instruction's remaining stores, preserving a later DSI's precise subaccess if one occurs, and then exits before the following instruction.
- Host `MakeExecutable`/`FlushCodeCache` publication reached only the legacy KPX cache. CPU range invalidation now translates the published instruction pages without PTE access recording and notifies both New World JITs. Guest icbi uses the same bridge with its own source label. Empty ranges and unrelated pages remain intact.

Final host evidence:

- Real KPX/native CPU fixture: **6,939 passed, zero failed**, both Debug and Release. Destructive device accesses run exactly once; a poisoned live DTLB is untouched; 26 integer/FP memory pairs run through identity and translated aliases; DSI/partial lmw/stmw/SMC, host publication, stale forbidden helpers, conversion bounds and NaNs are checked independently. `/tmp/macemu-ppc-verify-final-{debug,release}-core.log`.
- Capture replay: **203 passed, zero failed** for both the captured boot FP block and its reduced regression. The original boot replay failed two checks before the representation fix. `/tmp/macemu-ppc-verify-first-replay.log`, `/tmp/macemu-ppc-verify-boot-replay-fixed.log`, `/tmp/macemu-ppc-verify-minimal-replay-fixed.log`. Three live cache-discrepancy captures pass after fresh compilation, supporting the cache-invalidation diagnosis.
- Debug and Release MMUTests, with optional scalar inlining off and on: **34,585 passed, zero failed** each. `/tmp/macemu-ppc-verify-final-{debug,release}-mmu{,-inline}.log`.
- Isolated Debug/Release apps build successfully. `/tmp/macemu-ppc-verify-final-{debug,release}-build.log`.

Live integration uses cloned disk/NVRAM and PPC VERIFY with host 68k ON. The initial isolated run reaches the desktop but records 42,577 PPC misses; conversion-representation repair reduces that to 128 stale-cache comparisons. After the publication bridge, `/tmp/g8/ppc-isolated-verify-publish/results.json` reports **429,700,000 PPC block comparisons and zero misses**, Finder after 238.58 seconds and all 180 desktop workload seconds, five scripted mouse targets, heartbeat, no guest errors/CPU returns/IRQ stalls/mouse failures. Maximum observed VBL service latency is 87,652 microseconds. These are instrumented correctness runs, not serial Release performance benchmarks. The JSON's `ppc_verified` field counts blocks; its `native` field (219,139,762) counts host 68k instructions.

The successful guest app includes the conversion, complete-stmw and publication fixes. Its snapshot precedes the final stale-helper replay guards and explicit invalid-conversion representation hardening; those final changes pass the independent Debug/Release host suites. The 68k implementation is unchanged in this milestone, and the earlier ASan/UBSan qualification remains its regression evidence. The usual user DerivedData launcher binary has not been replaced; final candidates remain in the private Debug/Release build directories.
