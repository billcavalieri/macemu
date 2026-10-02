# Finish the New World host 68k JIT

Planning baseline: 2026-09-30. This is an implementation plan, not a report of completed native 68k translation.

## Intended result

Build a direct 68k-to-ARM64 translator for the 68k execution model provided by SheepShaver's New World NanoKernel. It should accelerate ordinary guest 68k execution, preserve the existing PPC/68k transitions and interrupt behavior, and run through the normal app and preferences without a special launcher.

All instructions and services supported by the current NanoKernel must retain their behavior. Common integer instructions should execute directly in host blocks. Complex system services and uncommon instructions may use the real NanoKernel implementation through a precisely specified exit. No skipped instructions, forced interrupt completion, altered guest device behavior, or disabled features count as completion.

Confirm the exact emulated 68k model and legal instruction/addressing-mode set from the active ROM before claiming a particular 680x0 implementation. A new 68k MMU or floating-point unit is not implied by this project; preserve the model and services the guest already receives.

## What exists today

- `SheepShaver/src/nw_68k_jit.cpp` is 553 lines. It has opcode/PC histograms, a 4,096-slot cache, a 4 KiB executable arena, one-instruction stop machinery, a generic fallback stub, and 11 ranked-opcode stubs.
- The generic stub calls `nw_68k_one()`, which executes the NanoKernel handler. Ranked stubs call `nw_68k_ranked()`, whose opcode argument is currently unused, and ultimately use `powerpc_cpu::nw_68k_jit_step()` and the existing PPC JIT. These are useful bridge experiments, but they are not direct 68k instruction emitters.
- Selection happens in `sheepshaver_glue.cpp`'s `Execute68k` bridge through `jit68k_host`, default false. The normal PPC execution loop records a histogram, but does not select this host 68k executor for every guest 68k dispatch.
- `sync_pull()`/`sync_push()` copy D0-D7, A0-A6, r24 and r25. They are not a complete native execution state contract: A7, CCR, other NanoKernel state, and prefetch/dispatch bookkeeping need explicit treatment.
- The cache uses `nw_la_to_pa(r24)` and `(r25 >> 13) & 1`. `nw_la_to_pa()` is a host Mac-address mapping helper, not a general translation/protection probe. The interpretation of r25 as a raw SR must be verified. There are no 68k-specific invalidation entry points in the current header.
- Global execution/stop state needs an explicit ownership and reentry contract before the main guest loop uses the new executor.

The histogram saved at `~/Library/Logs/SheepShaver/jit68k-hist.txt` on September 30 at 12:32 reports 272,877,538 table hits; its top 16 exact opcodes account for 22%. Its hot list includes RTS, MOVE/MOVEA, MOVEQ, MOVEM, JSR, LINK, conditional branches, TST and CMP. The separate `m68` list covers a different instrumentation point. Neither list is a trustworthy direct-native coverage metric yet. Use these as prioritization hints, then collect a normalized baseline.

## Architecture to build

Keep `nw_68k_jit.cpp` as the dispatcher and runtime coordinator. Separate the decoder, state adapter, memory bridge, cache, and ARM64 emitter as complexity grows. Proposed modules:

| Module | Responsibility |
|---|---|
| `nw_68k_jit.{cpp,h}` | Selection, execution budget, modes, exits, statistics |
| `nw_68k_state.{cpp,h}` | Complete NanoKernel import/export and canonical state |
| `nw_68k_decode.{cpp,h}` | Opcode legality, lengths, extension words, effective addresses |
| `nw_68k_memory.{cpp,h}` | Guest translation, ordered accesses, fault/exception exits |
| `nw_68k_cache.{cpp,h}` | Physical-page dependencies, invalidation, code lifetime |
| `nw_68k_emit_a64.cpp` | Direct host instruction generation and helper calls |
| `kpx_cpu/tests/nw_68k_harness.cpp` | Differential instruction and integration tests |

Reuse proven ARM64 encoding and executable-memory utilities where practical. Avoid introducing a second PPC translation layer or coupling the new translator to the full PPC emitter's internal register allocation. Extract only genuinely shared utilities, with existing PPC tests still passing.

Define a small decoded instruction representation: opcode PC, length, operation, width, source/destination effective addresses, flag inputs/outputs, memory accesses, and exit properties. Begin with eager flags and one-instruction compilation; register caching and lazy flags come later.

## Phase 2 — Establish the execution contract and correctness infrastructure

This phase is the prerequisite for native instruction work.

1. **Make the reference and tests usable.** Repair the MMUTests target's existing link dependencies and resolve its four known baseline assertion failures according to current device/cache semantics. Give the 68k harness a standalone build/run command. Do not suppress failing assertions to produce a green baseline.
2. **Normalize profiling.** Count once at a canonical 68k instruction boundary. Separate boot, idle Finder, Control Strip, representative 68k app work, QuickTime, file operations, and mixed PPC/68k callbacks. Track actual native instructions, NanoKernel exits by reason, compiled blocks, time, invalidation and cache churn. Replace the bounded lossy PC table or report dropped samples. Persist useful results periodically, including after a crash.
3. **Specify the NanoKernel ABI.** Map D0-D7, A0-A7, actual opcode PC, r24's prefetch position, r27's prefetched word, r29 dispatch target, SR/CCR representation, PPC CR/XER bits, interrupt/wakeup bits, stack selection, run mode, return trampoline and scratch-register obligations. Document and test round trips. Keep the guest stack distinct from the ARM64 host stack.
4. **Specify exits and nesting.** Use explicit outcomes such as continue, NanoKernel fallback, memory fault, asynchronous event, execution return and invalidated code. Scope runtime state to the CPU/execution frame. Preserve the outer frame across nested EMUL_OP, `Execute68k` and PPC callbacks.
5. **Define precise memory/fault behavior.** Use the same translation/protection context as the NanoKernel's 68k operand/prefetch loads. Distinguish that from fetching the PPC handler itself. Do not use unchecked `ReadMacInt*` or `nw_la_to_pa()` as the native memory contract. Define the NanoKernel/PPC exception continuation required for every fault exit; an arbitrary synthetic PPC PC is insufficient.
6. **Implement cache correctness.** Use verified opcode PC, physical code-page dependencies, validated execution context and page generations. Register native 68k code pages with invalidation independently of whether the PPC JIT has code on them. Connect guest stores, host writes/copies, cache-control operations, mapping/context changes, reset and shutdown. Unlink branches before invalid code can execute.
7. **Harden executable memory.** Ensure write protection is restored on every emitter exit, allocation is bounded, instructions are published only after cache synchronization, and executable storage is not recycled beneath an active frame or chained branch.

**Gate:** state import/export and nested returns match the reference; code modification/remapping cannot execute a stale entry; injected prefetch/data faults have the same observable continuation as NanoKernel execution; the reference test baseline is green.

## Phase 3 — Execute real native instructions, one at a time

Implement complete register/immediate forms first: NOP, MOVEQ, MOVE/MOVEA, ADD/SUB/CMP and their immediate/quick/address forms, AND/OR/EOR, CLR/TST/NOT/NEG, EXT, SWAP and EXG as appropriate to the verified model.

Compile instruction families with size and addressing constraints, rather than more exact-opcode stubs. Validate byte/word writes preserving unaffected register bits, sign extension, address-register semantics, and X/N/Z/V/C behavior. Keep flags eager so every native exit has canonical state.

Use three modes with defined semantics: off/reference, native on, and verify. Keep the `jit68k` Apple DR flag separate from `jit68k_host`. A test-only allowlist can introduce families individually without redefining their semantics.

**Gate:** deterministic boundary vectors and seeded random states agree with the NanoKernel for every enabled form; mixed native/fallback sequences round-trip exactly; counters demonstrate actual 68k-to-ARM64 execution.

## Phase 4 — Effective addresses, memory operations and hot control flow

Implement shared effective-address decoding and memory access before duplicating it across instruction families:

- Register indirect, displacement, absolute, PC-relative, immediate, postincrement and predecrement; then the indexed/extension formats supported by the verified model.
- Byte/word/long accesses, guest byte order, page crossings, access permissions and supported alignment behavior.
- A7 byte-size stack adjustment, source/destination register aliasing and ordered effective-address side effects.
- Memory MOVE/MOVEA, arithmetic/logical/compare/test forms, LEA and PEA.
- BRA/Bcc, BSR/JSR/JMP, DBcc/Scc, RTS, LINK/UNLK and MOVEM, prioritizing measured workload frequency.

For faults, specify the commit point for each operation. The prior Happy Mac bug must have a regression here: an unsuccessful operand prefetch cannot silently advance the instruction stream. Do not generalize this into rolling back every instruction: multi-access and read-modify-write instructions need the same partial effects/restart rules as the NanoKernel model. Never repeat an already completed MMIO access merely to retry.

**Gate:** fault injection at each access/extension word agrees with the reference; stack/aliasing/MOVEM cases pass; modifying an opcode or extension word invalidates the right block; memory-to-control-flow mixed sequences work.

## Phase 5 — Cover ordinary guest 68k execution

Add a guarded interception at the canonical NanoKernel dispatch boundary in the normal PPC executor. Use the ROM's verified dispatch contract and run-mode checks; do not assume that any PPC PC inside the fixed table range is an instruction boundary. Cover startup and resumed 68k application execution, not only host `Execute68k` calls.

Use one dispatcher and state contract for both entry paths. Prevent recursion into the hook while the reference handler is running. Permit a bounded native run, then return to the regular CPU event/exception loop with a fully synchronized state.

Retain real NanoKernel behavior for A-line services, F-line handling, privileged/state-changing instructions, EMUL_OP, Mixed Mode and execution-return trampolines until an equivalent native implementation is verified. Flush/materialize all necessary state before those exits, including IRQ-related CR bits.

**Gate:** regular guest workloads record native executions; boot, Finder, Control Strip, shared-folder callbacks and PPC/68k transitions pass with JIT and devices enabled; nested callbacks neither lose outer state nor return to the wrong frame.

## Phase 6 — Finish semantic coverage and asynchronous behavior

Complete remaining supported integer families: shifts/rotates, bit operations, MUL/DIV, ADDX/SUBX/NEGX and cumulative-Z behavior, BCD and bitfields where the verified model supports them. Maintain an opcode/form matrix: direct native, exact NanoKernel service exit, guest-illegal, or implementation gap. Every legal form needs a test and an explicit execution policy.

Audit trace, privilege changes, exception entry/return, division overflow/zero, special status/control-register operations and supported F-line/FPU behavior. A-line dispatch remains real guest OS behavior; the JIT must not replace Toolbox services with dummy success.

Establish bounded interrupt and host-event polling. Preserve DEC/timebase, pending IRQs, interrupt masking, idle wakeups and special execution flags across both entry paths. Long loops must yield promptly without artificially acknowledging devices. Test an interrupt at each relevant instruction/exit boundary with guest state visible to the handler.

**Gate:** no unclassified legal forms; enabled families have zero differential mismatches; injected interrupts, faults and nested callbacks preserve reference behavior; mouse, audio, timers and disk activity continue during long native loops.

## Phase 7 — Make blocks efficient

Once instruction semantics and exits are stable:

1. Compile bounded basic blocks with exits at branches, services, unsafe accesses, context changes and event checks.
2. Initially stop at a code-page boundary. Support blocks depending on multiple pages only after all dependency generations and fault sites are tracked.
3. Keep guest registers in host registers across operations. Spill/materialize at helpers and exits according to the ABI contract.
4. Introduce lazy flags with producer/consumer tests and mandatory materialization at reference, service, exception and interrupt exits.
5. Link direct branches through guarded, invalidatable targets. Preserve an execution budget across chains.
6. Add safe RAM/ROM memory fast paths with translation/protection/generation guards; MMIO and uncertain cases retain ordered helpers.
7. Tune arena/cache capacity and hot-block promotion from measured compile cost, reuse and eviction statistics.

**Gate:** optimization leaves differential results unchanged, stale chained targets cannot run, interrupt latency stays within the agreed budget, and useful workloads show a measured improvement over the existing PPC-JIT NanoKernel path.

## Phase 8 — Production qualification and normal-app rollout

Run a reproducible matrix using isolated copies of disk/CD/NVRAM state: host 68k JIT off/on; PPC JIT off/on where supported; cold/warm caches; boot and sustained 68k/mixed workloads. Test CD mounted, shared folders, Control Strip, sound/QuickTime, mouse capture/release, sleep/idle wakeup, shutdown/restart, and guest/host writes to executable pages.

Differential verification must not execute an MMIO operation or host callback twice in the live VM. Start with isolated memory/state fixtures; use recorded effects or paired cloned runs for external side effects and asynchronous behavior. Store a bounded failure trace with decoded instruction, pre/post state, memory effects, exit reason and seed.

Report Debug correctness and repeated Release timings separately. Proposed targets, to finalize after Phase 2 profiling:

- Zero new reference mismatches, crashes, unexplained guest hangs or missed interrupts.
- At least 90% direct-native coverage of ordinary dynamically executed integer instructions in the selected 68k workloads; report service exits separately and explain the remaining hot cases.
- A meaningful Release improvement on CPU-bound 68k workloads, no material regression on mixed workloads, and no worse input/audio responsiveness. Select numerical speed targets from the baseline rather than promising an arbitrary multiplier.
- A two-hour active workload soak and an overnight idle/event soak with retained heartbeat and exception diagnostics.
- Investigate any reproducible baseline desktop freeze before enabling this path by default; otherwise it obscures attribution of new hangs.

Keep the preference selectable during qualification. Enable it normally only after these gates pass, update preference descriptions and documentation, and retire obsolete ranked stubs/experimental state. A normal app launch must be sufficient; environment variables remain optional diagnostic controls.

## Suggested reviewable implementation sequence

1. Reference harness/build repairs and trustworthy profiling.
2. Complete NanoKernel state contract and scoped exit/reentry state.
3. Fault-aware memory bridge, code generations/invalidation and executable-memory lifecycle.
4. First direct register/immediate emitters with eager flags and verify mode.
5. Effective addresses and memory instruction families.
6. Calls, returns, branches, stack frames and MOVEM.
7. Normal guest dispatch integration and mixed-mode/event gates.
8. Remaining integer semantics, exceptional forms and coverage matrix.
9. Basic blocks, register retention and bounded chaining.
10. Lazy flags and guarded memory fast paths, justified by profiles.
11. Release benchmarks, active/idle soak, default preference and cleanup.

Each change should have a concrete before/after workload or regression test and should preserve a green baseline. Phase 2 resolves the largest design uncertainty; scheduling estimates become credible after the state/fault bridge and first native family are demonstrated.

## References

- Current implementation: `SheepShaver/src/nw_68k_jit.cpp`, `SheepShaver/src/include/nw_68k_jit.h`.
- Entry/state integration: `SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp`.
- Existing execution, MMU, exceptions and PPC JIT integration: `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp` and `.hpp`.
- Host Mac-address mapping: `SheepShaver/src/include/nw_boot_contract.h`.
- Shared invalidation infrastructure to audit/extend: `SheepShaver/src/nw_jit.cpp`.
- Architectural opcode/addressing/flag specification: [Motorola M68000 Family Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf) and [published errata](https://www.nxp.com/docs/en/reference-manual/M68000PRMER.pdf). The active NanoKernel remains the compatibility reference for its specific emulation model and exception behavior.
