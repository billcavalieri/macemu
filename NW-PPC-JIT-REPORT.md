# New World PPC JIT: correctness, performance, and design review

Reviewed September 30, 2026, at repository HEAD `b60b1545`.

## Assessment

The New World PPC JIT is a substantial ARM64 implementation with broad opcode coverage, working desktop boots, translation caches, selective register marshalling, native chaining, and NEON acceleration. Its main remaining weakness is the consistency of architectural state at faults and transitions between execution paths.

This review reproduced three groups of correctness defects: failed integer loads changing their destination, host memory helpers losing fault metadata and a conditional store changing CR0 on failure, and an FPSCR rounding-mode change failing to affect subsequent arithmetic. A separate source review found that live VERIFY execution can mutate the same memory, devices, and CPU state used by its reference interpreter. That prevents VERIFY from serving as a reliable correctness certificate today.

The performance changes are technically useful, especially the two-way DTLB, MSR synchronization, GPR caching, and NEON helpers. Current logs show low reported DTLB miss rates, substantial chaining, and repeated code-arena recycling. They do not establish a controlled application speedup, real movie FPS, or acceptable worst-case event latency.

Recommended order: establish precise fault semantics and trustworthy validation; repair the test target; measure Release workloads and cache-maintenance latency; then extend register residency, inlining, and chaining under a shared execution contract.

## Scope and evidence

The review covered the ARM64 New World path, its KPX CPU integration, relevant interpreter semantics, the MMU/JIT harness, recent Git history, and saved boot/VERIFY logs. It is not an exhaustive proof of every instruction encoding.

Primary implementation:

- [nw_jit.cpp](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp): 13,111 lines; helpers, opcode policy, caches, statistics, ARM64 emission, and compaction.
- [nw_jit.h](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/include/nw_jit.h): 596 lines; CPU shadow state, callback interfaces, and public contracts.
- [ppc-cpu.cpp](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp): live dispatch, marshalling, memory callbacks, exceptions, and chaining.
- [ppc-execute.cpp](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-execute.cpp): independent KPX instruction execution, including FPSCR control.
- [mmu_harness.cpp](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/tests/mmu_harness.cpp): 6,860 lines of checks across MMU, devices, JIT, and related subsystems.
- [JIT_OPS.md](/Users/bcavalieri/Documents/GitHub/macemu/JIT_OPS.md): generated September 21; support checklist, not a conformance result.

New tests used the freshly compiled current `nw_jit.o` in isolated native executables. Faults were induced with a bounded RAM fixture or callbacks returning the documented fault status. Small temporary platform shims supplied linking dependencies. These tests exercise generated ARM64 code and its helpers; they do not exercise an entire running guest, MMIO device model, or all live exception delivery.

## Current architecture

| Area | Current implementation | Consequence |
|---|---|---|
| Translation | Direct PPC-to-ARM64 emission plus C/C++ helpers | Broad coverage, but repeated semantic and fault handling across paths |
| Blocks | Maximum 32 guest instructions; one guest code page; cuts at unsupported forms and execution-class boundaries | Simple exception/class handling; short blocks and dispatch overhead remain important |
| Cache identity | Physical page, guest PC, packed IR/DR/PR mode, endian | Remapping can select a different translation without indiscriminate code-cache flushing |
| Instruction classes | Integer, scalar FP, VMX; FP/VEC availability checked at entry and successor transitions | Necessary for guest unavailable exceptions and correct task-state saving |
| Code storage | 64 MiB arena, sixteen 4 MiB banks; a spare 64 MiB arena for compaction | Recycling preserves useful translations, with copying and executable-pointer lifetime obligations |
| Metadata | 262,144 entries, 56 bytes each; bounded 16-slot hash probing | 14 MiB metadata; overall free slots do not guarantee absence of local collisions |
| Translation lookups | 1,024-set, two-way DTLB; 256-entry direct-mapped ITLB; generation/context checks | Useful fast paths; permission and translation changes must invalidate the right assumptions |
| GPR handling | Selective masks; up to four guest GPRs cached in ARM64 x21–x24; write-through shadow updates | Saves repeated reads; helper calls drop the cache; stores and marshalling still cost time |
| FP/VMX handling | FP masks; VMX blocks copy all 32 vector registers | VMX copy-in/out moves about 1 KiB per dispatch before helper traffic |
| Chaining | Native epilogue tail calls capped at four; a separate C successor loop capped at eight | Fewer returns to the main executor, but two sets of transition logic must agree |
| Default acceleration | AltiVec inline emission enabled unless explicitly disabled, subject to a startup check; scalar FP inline emission opt-in | Old comments and summary labels do not always describe current execution |

The code arena plus metadata account for roughly 78 MiB of address space; allocating the spare increases that to roughly 142 MiB, before other state. These are allocation sizes, not measured resident memory.

The older PPC/dyngen path is separate from this New World MMU JIT. Its performance or correctness should not be used to certify this implementation.

## Correctness findings

Priorities below describe engineering urgency. P1 means fix before expanding the affected acceleration path; P2 means a material validation or design issue.

### 1. P1: failed D-form integer loads overwrite the destination

**Reproduced in generated ARM64 code:** `lwz`, `lwzu`, `lbz`, and `lbzu` replace the destination with zero when the load faults.

The emitters store the helper result into the guest destination before checking `cpu->fault`. The update forms guard the base-register update, but that does not protect the destination. See [emit_call_lwz](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:8778) and [emit_call_lb](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:8803).

The probe starts with `RD=0xdeadbeef`, executes a successful prefix, then loads from an inaccessible address. All four forms leave `RD=0`. The faulting PC and prefix state are retained and the suffix does not run, so the failure is specifically the premature destination commit.

| Form | RD after fault | Expected preservation |
|---|---:|---|
| `lwz`, `lwzu`, `lbz`, `lbzu` | `00000000` | Failed |
| `lwzx`, `lbzx` | `deadbeef` | Passed these probes |
| `lha`, `lhau`, `lhz`, `lhzu` | `deadbeef` | Passed these probes |

The halfword repair in `b60b1545` has the correct sequence: preserve effective address and result, check the fault, then write RD and update RA. See [emit_call_lh](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:9430). This validates that repair while showing the same invariant is still missing elsewhere.

**Impact:** live DSI handling can commit the successful prefix together with the corrupted destination before entering the guest handler. A retry may eventually replace RD again, but the exception handler has already observed incorrect state.

**Required change:** establish one load-commit pattern for every addressing form. Cover byte/halfword/word, indexed/update forms, destination/base/source aliasing where architecturally valid, and faults at every block position. Multi-access instructions need their own specified partial-completion rules.

Evidence: [probe source](/tmp/g8/ppc-report-probes.cpp), [results](/tmp/g8/ppc-report-probes.log).

### 2. P1: host helpers lose fault address/direction; `stwcx.` commits CR0 on failure

**Reproduced:** the host-callback branches of `lwarx`, `stwcx.`, `lfd`, and `stfd` set `cpu->fault`, but do not populate the effective address. Store callbacks also fail to mark the access as a store.

The callbacks correctly receive `EA=0xdead0040` and report failure. The resulting shadow state records:

```text
lwarx  fault=1 recorded_ea=00000000 store=0
stwcx. fault=1 recorded_ea=00000000 store=0
lfd    fault=1 recorded_ea=00000000 store=0
stfd   fault=1 recorded_ea=00000000 store=0
```

For `stwcx.`, CR changes from `f1234567` to `01234567` despite the fault. The helper clears the reservation and writes CR0 without first returning on failure. The KPX interpreter returns on failed translation before its CR0 update.

Relevant helpers: [lwarx/stwcx](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:1747), [lfd/stfd](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:639).

**Impact:** [live DSI dispatch](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:3612) re-translates `jc.fault_ea` using `jc.fault_st`. The missing fields can therefore select an incorrect address or access direction, or fall through to a different recovery path. The tests establish the metadata loss; they do not assert that every such fault delivers the same wrong guest exception.

Compile-time address probes do not eliminate this problem. Cached blocks and addresses computed by preceding instructions can reach a failing runtime access.

**Required change:** use a complete fault record containing faulting PC, EA, access width, load/store direction, and cause. Populate it consistently before returning. Audit FP, vector, string/multiple, and atomic helpers against the same contract. Carry width explicitly: the live DSI path currently reconstructs it from a partial opcode classification.

The live reservation resides in the real CPU callbacks and can survive between blocks. Resetting the shadow reservation at dispatch entry alone is not evidence of a cross-block reservation bug. Reservation granularity, intervening writes, context changes, and fault behavior still need independent tests.

Evidence: [atomic probe](/tmp/g8/ppc-report-atomic.cpp), [atomic results](/tmp/g8/ppc-report-atomic.log), [FP-memory probe/results](/tmp/g8/ppc-report-fp.log).

### 3. P1: FPSCR rounding control does not reach subsequent arithmetic

**Reproduced using the default scalar FP helper path:**

```text
mtfsfi 7,2
fadds f3,f1,f2       ; f1=16777216, f2=1
```

The guest FPSCR ends with RN=2, but the host remains in nearest rounding. The result is `16777216`; upward rounding requires `16777218`. RN=2 specifies rounding toward positive infinity. [PowerPC Programming Environments manual, FPSCR RN table](https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf)

[nw_jit_helper_mtfsfi](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:6182) replaces a nibble without applying the guest rounding mode or the interpreter's FPSCR summary handling. [KPX execute_mtfsfi](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-execute.cpp:1235) performs those steps.

**Required change:** define ownership of the FP environment across interpreter entry, JIT entry, FPSCR writes, helper calls, and host callbacks. Apply guest RN consistently and preserve the host environment when crossing domains. Audit `mtfsf`, `mtfsb0/1`, exception summaries, record forms, and scalar inline emission under that policy.

Broader FP conformance requires signed-zero, NaN, denormal, conversion, fused-operation, sticky-exception, FR/FI, and enabled-exception cases. This review did not prove every one of those paths incorrect; it proved one rounding-control failure and identified the larger validation requirement.

Evidence: [FP probe source](/tmp/g8/ppc-report-fp.cpp), [FP results](/tmp/g8/ppc-report-fp.log).

### 4. P1 validation defect: live VERIFY is not isolated from its reference

**Confirmed by source inspection:** VERIFY prepares a register shadow, but sets `jc.host=this`, the real CPU. It runs compiled code and then executes the reference interpreter against that same live machine.

Consequently:

- Store callbacks write real guest memory before reference execution.
- MMIO callbacks can perform real reads and writes twice. An MMIO address is allowed as a one-instruction block; that boundary does not isolate its effects.
- SPR/MMU callbacks can change real translation state before the reference executes the instruction.
- `jit_host_lfd` writes the real CPU FPR as well as the shadow output.
- Reservation callbacks change real reservation state.

See [shadow binding](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:3359), [memory/MMIO callbacks](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:1977), [lfd callback](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:2566), and [reference execution](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:3694).

The shadow contains a limited store log, but this integration compares registers rather than using that log to roll back or independently compare all memory effects.

There is also a coverage mismatch: VERIFY uses full register masks and disables ON-mode native tails/C successor execution. It therefore does not validate selective marshalling and chained execution as they actually run in production.

A saved September 30 VERIFY boot reported five mismatches at `50310520`, `50310594`, `503105f8`, `50310708`, and `50312710`. That run predates the halfword fix and encountered guest failure. These mismatches need minimized replay; the verifier's isolation defect prevents attributing them all to emitted arithmetic. [Saved VERIFY log](/tmp/g8/boot-regression/verify2/guest.log)

**Required change:** compare two executions starting from equivalent CPU, memory, translation, reservation, and device state. Use transactional RAM snapshots and deterministic callback records, or cloned machine state for offline replay. Device effects must occur once in the live machine. Separately exercise the ON-mode masks and chain transitions in an integration harness.

### 5. P2: timer/event behavior needs an architectural audit

Native successor lookup checks pending asynchronous exceptions; the C successor loop does not have the same pending-event check at its head. Both paths have finite hop caps, so this is a bounded-latency inconsistency rather than proof of an unbounded interrupt lockout.

[tick_decrementer](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp:1260) samples every 256 executor calls, whose instruction count changes with JIT chaining. It catches up using elapsed time, but timer/device service can be delayed by a long execution group or cache maintenance.

There is a further source-level discrepancy: `catch_up_timebase()` reasserts `dec_pending_` whenever DEC remains negative, after `take_dec()` clears it. The architectural request is tied to a sign transition and is canceled on delivery. Treat this as a correctness audit item with an independent timer test, rather than using recurring DEC requests to guarantee device wakeups. [PowerPC manual, DEC and decrementer exception sections](https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf)

**Required change:** share a deadline/pending-event policy between both chain paths. Model DEC requests separately from external interrupt and host-device wake events. Test EE transitions, exception entry/return, DEC writes, negative DEC after delivery, and nap/wakeup without an invented DEC event.

### 6. Further audit areas

These are concrete review targets, not additional reproduced failures:

| Area | Required checks |
|---|---|
| Self-modifying code | Every CPU/host/device write invalidates overlapping PPC and future 68k translations; a store into the currently executing block prevents execution of stale following instructions. Audit atomic stores: their callback invalidates the page but lacks the ordinary store callback's explicit current-page SMC exit. |
| Memory boundaries | Cross-page wide accesses, alignment exceptions, address wraparound, partial string/multiple accesses, permission faults, and load/update aliasing |
| Translation context | IR/DR/PR changes, SR/BAT/SDR1 changes, `tlbie/tlbia`, translated aliases, and cached permission checks |
| Register state | Selective GPR/FPR masks and transitions between integer/FP/VMX blocks, including helper clobbers and destination/source aliases |
| AltiVec | Big-endian lane order, permutations, saturating VSCR state, record forms, vector FP edge cases, and all valid aliases |
| Code lifetime | Compaction/eviction during nested execution, retained function pointers, executable cache publication, and future direct links |
| Shared state | One explicit owner for global caches, callbacks, and maintenance; synchronization or an owner-thread queue for host writes |

## Test status

The normal Debug `SheepShaver-MMUTests` Xcode build failed to link. Missing dependencies included platform preference functions/items, `GetTicks_usec`, and CPU execution methods referenced by the 68k JIT object. [Build log](/tmp/g8/ppc-report-unit-build.log)

For diagnosis, current Xcode-generated objects were relinked with temporary platform shims, excluding the unresolved preferences and 68k JIT objects. The resulting executable completed with **2,211 passed checks and four failed checks**. These totals include non-JIT subsystems. This is useful diagnostic evidence, not a successful build of the official target.

| Failure | Initial assessment |
|---|---|
| Harness line 1203: VBL IRQ expected after EOI | Likely stale expectation relative to current IACK/EOI handling; needs behavioral review |
| Line 1541: ADB GPIO poll-off expected IN_DATA | Unresolved device behavior/test disagreement |
| Line 1882: hard-coded device registration count | Likely stale count; verify intended registrations |
| Line 4137: cached function pointer equals its pre-wrap value | Conflicts with moving compaction; test semantic survival and fresh lookup, and document pointer lifetime |

The AltiVec sweep reported 115 cases run, eight memory cases skipped, zero failures; a second sweep reported eight run, zero failures. This does not establish full coverage of all vector names, aliases, memory faults, or FP edge cases.

Many helper-based native tests compare against `nw_jit_interp_one`, which can call the same semantic helper. Such tests validate emission and ABI handling but can miss a bug shared by both sides. Independent KPX execution and architectural expected values are also needed.

The September 21 opcode checklist reports 308 done, 34 partial, three excluded, and zero todo across 345 names. Its own definition says a checked name passes a representative allowlist test. A name can still have defective fault handling, unsupported encodings, or incomplete architectural state. Preserve that distinction when reporting completion.

## Performance changes already landed

| Change | Purpose and assessment | Evidence limit |
|---|---|---|
| Sept. 20, `0478f211` and `defc961d` | Larger cache, translation generations, execution-class gates, successor execution; improves reuse while preserving FP/VEC exceptions | More transitions require mask/context tests |
| Sept. 20, `1cef33b4` | ARM64 tail-call epilogues and vector marshalling; reduces main-loop returns | Tail calls still perform guarded lookup and state handling |
| Sept. 21, `a51da7b3` | Broader FP/VMX/system coverage and branch forms | Allowlisting is not full conformance |
| Sept. 21, `b25275e6` | Two-way DTLB and four cached GPRs | Commit reports miss rate 639/1000 to about 77/1000 on its 1024 boot; this is historical workload evidence, not a universal speedup |
| Sept. 22, `da009cdd` | Reuses valid DTLB lines and synchronizes shadow MSR on hops; addresses refill storms | Valuable correctness/performance interaction |
| Sept. 23, `602dfef8` | Debug-only statistics and realtime audio pull; scalar FP inline kept opt-in | Compare Release separately from instrumented Debug |
| Sept. 26, `964baa99` | Live-code compaction and cold eviction instead of discarding all translations | Adds maintenance latency and pointer-lifetime requirements |
| Sept. 28, `c7a78ed9` | NEON-backed AltiVec helpers, including common cursor operations | Helps helper execution too; no controlled application speedup measured here |
| Sept. 30, `b60b1545` | Halfword fault/retry correctness | Important stability repair; not a performance measurement |

The recent host cursor delivery, ADB wake, key mapping, and framebuffer repairs affect responsiveness. They should be measured as device/display integration changes, separately from PPC instruction throughput.

### What the saved measurements show

Three saved Debug boot runs provide the following counters. Runs were roughly 100, 100, and 110 seconds; they were functional boot tests, with logging and competing host workload, not controlled benchmarks.

| Run | Dispatch-group counter | Reported instructions | Chain counter | DTLB miss rate | Wrap counter | Maximum occupancy |
|---|---:|---:|---:|---:|---:|---:|
| fixed1 | 477,586,006 | 3,212,803,132 | 374,607,004 | 2/1000 | 5 | 35,505 |
| fixed2 | 473,404,463 | 3,183,288,237 | 372,400,183 | 2/1000 | 5 | 35,554 |
| desktop-fixed | 527,628,829 | 3,410,480,321 | 464,508,619 | 4/1000 | 10 | 68,700 |

Sources: [fixed1](/tmp/g8/boot-regression/fixed1/home/Library/Logs/SheepShaver/jit-summary.txt), [fixed2](/tmp/g8/boot-regression/fixed2/home/Library/Logs/SheepShaver/jit-summary.txt), [desktop-fixed](/tmp/g8/boot-regression/desktop-fixed/home/Library/Logs/SheepShaver/jit-summary.txt).

Interpretation:

- The reported DTLB miss ratio is low in these workloads. This supports retaining the two-way/generation design, while measuring misses and helper costs in application workloads.
- Maximum entry occupancy is about 13.5–26.2% of capacity, while the code arena still recycles. Entry-count capacity and generated-code-byte pressure are different constraints.
- Compile-null and skip-IO counters are zero in these saved summaries. That does not certify all fallback paths or prove no device accesses occurred.
- The integer-divided `insns/block=6` is misleading. `nw_jit_note_exec_at()` increments the block counter once for a dispatch group, while its instruction argument can include successor blocks. The untruncated group averages are about 6.727, 6.724, and 6.464; they are not individual native-block sizes.
- Faulted blocks can contribute their planned length rather than precisely retired instructions. Successor/native-tail accounting also needs reconciliation before using these totals as a throughput denominator.
- The “codec” bucket classifies execution involving VMX, not verified QuickTime decoding. A framebuffer-hash FPS proxy can count desktop/cursor changes. Neither is real movie FPS.
- Old summary labels such as `NEEDS_NEON` and `BLOCKED` are static labels, not proof that the current helper still uses a scalar loop.

### Highest-value performance work

**Bound cache maintenance.** [Page invalidation](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:2092) first uses a page bitset, then scans all 262,144 entries if code may exist. Range invalidation repeats that scan per page. Add physical-page-to-block ownership or generation indexes so writes cost work proportional to affected translations.

[Arena maintenance](/Users/bcavalieri/Documents/GitHub/macemu/SheepShaver/src/nw_jit.cpp:12768) also scans the whole cache to count live bytes, select cold blocks, rebuild ownership, and age hits. Compaction copies tens of MiB. Current heuristics already avoid several known pathological repeated compactions; add counters and p95/p99/max latency for remaining maintenance. Incremental aging, per-bank live-byte totals, and bounded collection work are promising next steps.

**Reduce register marshalling and helper traffic.** After mask correctness is established, add vector read/write masks and retain dirty vector state across compatible successors. Extend GPR residency only where traces show repeated reloads. Current write-through caching is a useful conservative base. Measure helper calls, spill traffic, and generated bytes per retired guest instruction before choosing the next instruction to inline.

**Extend chaining under common guards.** Conditional/indirect successors are potential gains, but comments record glyph corruption and black framebuffer failures from earlier attempts. Prove class, live-register, MMU, SMC, and event-boundary semantics first. A fresh lookup or generation-guarded link is safer to reason about than an untracked executable pointer after compaction.

**Profile idle behavior.** Billions of counted instructions can include guest polling loops. Measure host CPU while idle and inspect guest wait behavior. The existing idle recognizer uses particular guest PCs; a verified loop/wakeup policy should preserve interrupt semantics across supported ROM/OS combinations.

## Design changes recommended

### A. Centralize faults and instruction commits

Introduce a common result/fault protocol, for example:

```cpp
struct GuestFault {
    uint32_t pc, ea;
    uint8_t width;
    bool is_store;
    FaultCause cause;
};
```

An operation should produce a value or a fault before committing that operation's architectural outputs. Preserve completed preceding instructions, resume at the specified faulting PC, and distinguish a completed SMC store from a failed memory operation. For string/multiple accesses, encode their architectural partial-progress behavior explicitly rather than imposing whole-block rollback.

This change addresses the reproduced defects structurally and reduces duplicated DSI reconstruction.

### B. Make verification independent and replayable

Create a differential runner with isolated CPU state, RAM changes, reservations, translation state, and deterministic helper inputs. Capture failing blocks with initial state and external effects so they can be replayed and minimized without booting the VM.

Use separate tests for semantic arithmetic, native emission/ABI, and live execution transitions. Keep an independent reference implementation or expected-value cases for shared-helper instructions. Add tests that use ON-mode selective masks and both chain paths.

### C. Use one execution-transition policy

Define a shared chain context containing current mapping/mode, register residency, exception metadata, code generation, and execution deadline. Apply one successor-eligibility and exit policy to native tails and C hops.

This reduces divergence in FP/VEC gates, MSR synchronization, DEC handling, live register marshalling, and asynchronous event checks. A time/retired-instruction budget also makes latency easier to bound than independently increasing hop caps.

### D. Make code ownership and lifetime explicit

Track translations by physical page and stable block identity. Keep executable addresses relocatable implementation details. Invalidate PPC and future 68k translations through shared page-write notifications. Require fresh address lookup or generation validation after maintenance.

Give compilation/compaction an owner-thread and execution-epoch contract so code cannot move while an active or reentrant frame depends on it. A test should prove a retained translation still executes correctly after movement through a fresh lookup, rather than require the old address to remain valid.

The current use of `MAP_JIT`, write-protection toggles, and instruction-cache synchronization is aligned with Apple silicon JIT requirements. Encapsulate allocation/publication in scoped code-writer operations and audit every exit path and future shared arena design. [Apple JIT guidance](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)

### E. Reduce semantic-policy duplication

Split the 13,111-line file along actual responsibilities: decode/policy, CPU/memory helpers, ARM64 emission, code cache, validation, and telemetry. Extract common emitter operations before introducing more optimization machinery.

A declarative instruction descriptor should provide encoding constraints, register masks, memory width/direction, execution class, privilege, block-ending behavior, and potential effects. Generate support documentation and test enumeration from that metadata. The checklist generator currently ports support logic into Python, creating another place for policy to drift.

Keep executable reference semantics independent enough to detect common implementation errors. Shared metadata should not turn every test into the same algorithm running twice.

A small decoded-operation representation could then support local constant/address folding and safer register allocation. Its value should be demonstrated on measured hot blocks before expanding it.

## Proposed work sequence and completion gates

| Stage | Work | Evidence required before proceeding |
|---|---|---|
| 1. Architectural correctness | Fix integer load commits, complete memory-fault metadata, atomic CR0 failure handling, FP environment, and DEC/event semantics | Expected-value tests; independent interpreter comparisons; precise PC/DAR/direction/register preservation; successful-prefix and suppressed-suffix checks |
| 2. Validation infrastructure | Repair official MMUTests linkage; resolve four baseline failures; isolate differential effects; add replay capture | Official target builds and passes without diagnostic shims; deterministic mismatches; memory/device effects occur once; ON masks and chaining tested |
| 3. Measurement | Correct counter definitions; instrument helper and maintenance costs; controlled Release A/B runs | Repeated boot, idle, UI, integer, FP, VMX, and movie/audio workload results with configuration and variance |
| 4. Cache/transition design | Page ownership, stable block identity, bounded maintenance, shared chain policy | SMC/remap/compaction stress passes; bounded event latency; improved measured maintenance distribution |
| 5. Targeted acceleration | Vector masks/residency, selected helper inlining, tested branch links and local optimizations | Independent equivalence tests plus workload improvement; unchanged display/audio/input behavior |
| 6. 68k integration | Shared code-page notifications, fault conventions, publication/lifetime policy | Cross-engine writes invalidate both engines; fallback/exception transitions preserve state |

Benchmark JIT ON against the interpreter in Release, using the same ROM, cloned starting disk, preferences, device set, and workload. Use one VM at a time, bounded instrumentation, warm/cold cases, and at least several repetitions. Record time to desktop, completed workload time, real presented movie frames, audio underruns, input latency, host CPU/energy, retired guest instructions, helper calls, code bytes, cache pressure, and maintenance latency.

Long-run acceptance should include shared folders, CD/image devices, display mode/depth changes, movie/audio activity, sustained input, sleep/idle wake, and translation/SMC stress. Functional boot success alone cannot close the findings above.

## Relation to the reported freezes

The halfword-fault fix addressed a specific Happy Mac failure and passes the targeted fault-preservation probe. The new findings are independent defects that warrant fixes, but this review does not establish that any one caused the earlier 4:04 PM desktop freeze.

The saved freeze log showed host activity continuing after guest progress stopped, with an interrupt left in service. A CPU-thread stack and complete guest exception state were not captured at the freeze. That evidence cannot distinguish a blocked guest CPU, stale execution state, an interrupt-handler problem, or another integration failure.

For the next failure, capture a bounded flight recorder: host monotonic time, guest PC/MSR/CR, fault record, IRQ pending/in-service/EOI state, DEC request/delivery, chain exit reason, and maintenance start/end. Obtain CPU-thread stacks while the VM is still stuck. Those records can turn a symptom into a reproducible cause without high-volume continuous logging.

## Reproduction notes

Official target attempted:

```sh
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver.xcodeproj \
  -scheme SheepShaver-MMUTests -configuration Debug \
  -derivedDataPath /tmp/macemu-unit-fault-dd \
  CODE_SIGNING_ALLOWED=NO build
```

Current JIT object used by the diagnostic probes:

```text
/tmp/macemu-unit-fault-dd/Build/Intermediates.noindex/SheepShaver.build/Debug/SheepShaver-MMUTests.build/Objects-normal/arm64/nw_jit.o
```

Probe sources and outputs are linked in the findings above. The linking fixture is [ppc-report-platform.cpp](/tmp/g8/ppc-report-platform.cpp); it supplies a real monotonic clock and small preference/logging/framebuffer stubs. The framebuffer stub is irrelevant to these RAM/fault/FP cases but means the probes cannot validate framebuffer integration. The diagnostic harness used [harness-platform.cpp](/tmp/g8/harness-platform.cpp) and [unit-fixed-objects.list](/tmp/g8/unit-fixed-objects.list). Its complete output is [ppc-report-harness.log](/tmp/g8/ppc-report-harness.log).

Typical isolated probe link command, from the repository root:

```sh
clang++ -std=c++11 -I SheepShaver/src/include \
  /tmp/g8/ppc-report-probes.cpp \
  /tmp/macemu-unit-fault-dd/Build/Intermediates.noindex/SheepShaver.build/Debug/SheepShaver-MMUTests.build/Objects-normal/arm64/nw_jit.o \
  /tmp/g8/ppc-report-platform.o -Wl,-dead_strip \
  -o /tmp/g8/ppc-report-probes
```

The probes print observed state and exit normally; the incorrect outputs are findings, not assertions contributing to the harness pass/fail count. Temporary `/tmp` evidence can be removed by the OS, so the essential values and conditions are preserved in this report. Production source was not changed during the review.

## Follow-up: October 2, 2026

The D-form byte/word destination-commit defect in finding 1 has been corrected as a dependency of New World host 68k qualification. lwz/lwzu/lbz/lbzu preserve the helper result and update effective address, check the fault, and commit RD/RA only on success. Generated-code tests inject faults at all three positions of a mixed block, using the NK's high-numbered r24/r27 prefetch registers; they assert retained prefix state, untouched destination/base, precise fault PC/address and no suffix execution. The full MMU/device/PPC target passes 2,305 checks (`/tmp/macemu-nw68-fault-mmu.log`). Findings 2–5 remain open. Live PPC VERIFY still reproduces the five documented bootstrap mismatches and cannot certify correctness because its reference shares mutable machine state.
