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

### PPC plan work: first memory milestone

Implementation has begun in [NW-PPC-JIT-PLAN.md](NW-PPC-JIT-PLAN.md). The reproduced metadata and failed-conditional-store defects in finding 2 are corrected: memory exits now carry logical EA, width and direction alongside the emitter-maintained faulting PC and exit cause. Atomic, FP and vector callback paths use the same record as scalar paths; live DSI recovery consumes its width directly. Physical fast-path helpers retain the original EA. Failed `stwcx.` preserves CR0 and the shadow reservation, while completed SMC stores commit their outputs before exiting.

Failed FP/vector callback outputs are staged privately, `lmw` preserves the failed destination and its completed prefix, and RAM bounds checks reject arithmetic wraparound. Additional SMC checks found eleven emitters comparing W8 instead of the loaded W10 fault status; those encodings are corrected. The vector-store callback completes all sixteen probed bytes before reporting SMC, and the atomic callback now requests an exit after writing the executing code page.

The initial expected-state matrix reproduced 468 failed assertions across 48 forms, three block positions and two failure causes. Expanded official Debug and Release targets now each pass **7,070 checks, zero failures**, including logical/physical address distinction, partial `lmw`, wraparound and committed SMC stores. Release testing additionally exposed an AltiVec startup probe that failed to preserve callee-saved x19; its generated ABI and mapping/write-protection cleanup are repaired. Normal acceleration remains enabled. The 68k ASan/UBSan suite also passes all **390,900 checks**, with 150,377 decoded-ROM comparisons.

This closes the reproduced helper-state failures, not the complete architectural memory audit. Full live DAR/DSISR delivery, atomic device/reservation behavior, all multi-access partial-completion cases and faulted chain transitions still need independent integration coverage. Findings 3–5 and the live VERIFY isolation defect remain open. Test logs and the private guest regression are indexed in the implementation plan.


### PPC plan work: FP ownership and DEC events

The reproduced guest-RN failure in finding 3 is corrected. Scalar arithmetic now applies guest RN inside an instruction-owned FP environment and restores host control/status before returning or calling host code. KPX FPSCR writes no longer leak guest rounding onto the host thread. FEX/VX summaries and `mtfsb`/`mcrfs` handling are corrected; independent tests also found and fixed KPX `mtfsfi` immediate placement and FX field-zero writes. Optional inline single arithmetic keeps full FPR operands until result rounding and retains fused multiply-add operations.

DEC no longer reasserts a request simply because its current value is negative. Timer crossings, delayed full-counter wraps, delivery cancellation, write transitions and TB/DEC clock ownership are explicit. Live JIT DEC accesses are sampled and privilege-checked by the CPU at the instruction boundary. Both chaining paths use the same pending-event guard. These changes replace the repeated-request behavior described in finding 5; they do not certify the larger memory/cache maintenance latency contract.

Independent Debug/Release host matrices, real KPX instruction/CPU tests and private guest evidence are indexed in the [implementation plan](NW-PPC-JIT-PLAN.md). Full FP arithmetic conformance, live VERIFY isolation, all faulted transitions and controlled performance measurement remain open. The historical review and earlier test totals above are retained as dated evidence.


### October 2 update: isolated verification and code publication

Live PPC VERIFY now keeps KPX authoritative and replays an ordered access record against a private native shadow. Supported scalar/FP/multiple-access blocks no longer repeat RAM/device/translation effects. Store contents, sequence and fault metadata are checked alongside registers. Unrecorded privileged, timer, atomic, string and VMX/vector effects are explicitly excluded, and guarded helpers fail closed during replay even if a stale native block contains them. Full-state masks and unchained execution remain; this is not completion of every stage-2 validation gate.

The verifier exposed inconsistent integer-conversion representations, incomplete `stmw` on SMC, and host code publication that invalidated only the legacy KPX cache. Both engines now use signed integer-word extension consistently, with explicit NaN handling in the JIT; `stmw` completes its instruction before SMC exit; `MakeExecutable`/`FlushCodeCache` invalidation reaches both New World JITs through translated physical instruction pages. The conversion upper word is architecturally unspecified, so its repair is an emulation consistency policy rather than an ISA correctness claim.

Final Debug/Release real-CPU suites each pass 6,939 checks plus the 203-check reduced capture replay. The 34,585-check MMU/native suites pass in both configurations with scalar inlining off and on. The live comparison sequence improves from 42,577 misses, to 128 stale-cache misses after conversion repair, to zero misses across 429.7 million PPC block comparisons after publication repair. Finder and the full 180-second desktop workload complete with five mouse targets and no guest/CPU/mouse/IRQ errors. Its 87,652-microsecond maximum VBL latency is diagnostic-run evidence, not performance acceptance. The final helper guards/invalid-result hardening have host coverage and postdate the successful guest snapshot.

Bounded captures replay without booting a VM. The manually reduced `ppc_verify_fctiwz.capture` is now an automatic regression. Complete cached-source capture, automatic reduction, isolated privileged/atomic/vector observations, production masks/chains, independent translation replay and controlled Release performance measurements remain open. Commands, logs, candidate-build limitations and updated stage status are in [the implementation ledger](NW-PPC-JIT-PLAN.md#isolated-live-verify-milestone).


## October 2 string effects and production transition checks

The next stage-2 milestone extends isolated live replay to all four supported string transfer forms. Reference and native loads agree on ordinary transaction widths and independently translate subaccesses across page boundaries. A protected/noncontiguous next mapping no longer becomes an unchecked contiguous host read. Native string stores complete after SMC while preserving a later DSI's committed prefix and fault metadata; verification and captures identify the last actual code-page store when ordinary stores follow it.

Independent production dispatcher tests now exercise selective GPR/FPR copying, both native tails and C successors, FP class gates, pending DEC/CPU-event stops, and actual chained string DSI delivery. These tests use literal expected state and the live CPU exception machinery. They do not turn full-state/no-chain live VERIFY into a certificate for every production transition. Privileged/atomic/vector observations, full cached-source capture and automatic reduction, broader mask/chain combinations and the later measurement/cache stages remain open.

Debug and Release real-CPU suites each pass 50,144 checks, plus the 203-check reduced capture replay. Four official MMU runs each pass 34,585 checks; both isolated app builds succeed. Detailed limits and artifact paths are recorded in `NW-PPC-JIT-PLAN.md`.

The final private Debug integration passes 300 seconds of Finder/file work, an actual shared-folder write, 16 mouse targets and 545.2 million PPC block comparisons with zero misses. It records no positive guest system errors, unexpected CPU returns, unresolved IRQ stalls or mouse failures (`/tmp/g8/ppc-string-verify-guest/results.json`). These are instrumented correctness results, with no forced interrupts in this run; they do not establish a Release performance gain. The normal launcher binary has not been replaced.


## October 2 atomic observations and reservation ownership

Live verification now covers `lwarx`/`stwcx.` with typed translation observations and private physical-address reservation state. It checks store permissions even when a conditional store writes nothing, computes reservation success independently, retains reservations across block boundaries and compares final reservation state. Version-2 captures preserve those observations; the replay reader remains compatible with version 1.

The production callbacks had bypassed device/ROM decoding. A new destructive-device fixture reproduces a host crash in the prior raw-read path. The corrected callbacks perform decoded device transactions once, reject an unmapped PA before dereferencing it, and ignore physical ROM writes while retaining the reference's conditional-result policy. Independent production tail/C-chain tests cover physical aliases, failed reservations, protected-store DSI and a pending-event stop with an outstanding reservation.

Debug and Release core suites each pass 51,404 checks; both offline captures each pass 205 checks in each configuration. Four official MMU configurations each pass 34,585 checks. This qualifies the current aligned single-CPU emulation policy; hardware reservation granules, DMA/multiprocessor coherency, alignment behavior and independent MMU translation replay remain open. Detailed artifacts and completion gates are in `NW-PPC-JIT-PLAN.md`.

The final private Debug guest run completes 300 seconds of desktop/file work, a shared-folder write, 16 mouse targets and all 20 injected interrupts. It records 545.5 million PPC block comparisons with zero misses and no guest system errors, unexpected CPU returns, script errors, mouse failures or unresolved IRQ stalls (`/tmp/g8/ppc-atomic-verify-guest/results.json`). The maximum observed VBL service latency is 71,015 microseconds. This is instrumented integration evidence; controlled Release performance measurement and the remaining PPC plan stages are still pending. The usual launcher binary has not been replaced.

## October 2 opcode inventory and overflow-enabled integer forms

The opcode checklist now separates dispatch acceptance from the outstanding qualification/restriction ledger. Its generator preserves that ledger and tests applicable XO-form OE/Rc combinations. This exposes previously hidden native coverage gaps for `nego`, `addmeo`, `addzeo`, `subfmeo` and `subfzeo`, including their record forms; the old representative-name labels had marked all five complete. C and Python support policies agree across 6,555 sampled encodings, but this parity does not establish ISA conformance.

All five forms now execute through production native dispatch and private verification. OV is replaced only for OE, SO remains sticky, carry forms retain incoming CA for their arithmetic, and CR0 records the resulting SO. Independent literal boundaries and mixed native/C production chains pass: Debug and Release each report 673,364 core checks plus two 205-check captures; four official MMU configurations each pass 34,585 checks. Both isolated apps build successfully. No new guest or performance result is claimed for this snapshot. The full remaining revisit queue and evidence paths are recorded in `JIT_OPS.md` and `NW-PPC-JIT-PLAN.md`.

## Selected VMX revisit milestone

Independent integer-vector comparisons reproduced missing sticky VSCR SAT updates in the native helpers for `vsumsws`, `vmsumshs` and `vpkswss`. All three now set SAT when their result saturates. Literal outputs and SAT set/retain/clear sequences agree with KPX and the [AltiVec specification](https://www.nxp.com/docs/en/reference-manual/ALTIVECPEM.pdf); a one-instruction captured alias failure is now an automatic offline regression.

Isolated live replay now covers 41 named integer/permutation/VSCR forms and 12 vector memory/shift-mask forms. Full-vector accesses record a typed 16-byte translation probe plus their four actual word transactions. Replay owns private vector state and consumes observations; destructive devices execute once. Lane order, operand aliases, selected fault/SMC exits, malformed records and integer/vector/integer transitions are qualified on both production successor paths.

Debug/Release with vector inlining off/on each pass **6,749,555 core checks**, three **205-check captures**, and **34,585 official MMU checks**, all without failures. The cloned Debug guest then completes 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-file write with **549.3 million PPC block comparisons and zero mismatches**. That aggregate count is integration evidence, not per-opcode coverage. Remaining delegated KPX helpers and vector FP stay outside VERIFY until they have private execution; independent translations, broader ISA/encoding boundaries and exception combinations remain open. The `vslo/vsro` restriction is retained, and no performance improvement is claimed. Detailed evidence and completion gates are in `NW-PPC-JIT-PLAN.md` and the persistent `JIT_OPS.md` ledger.

## Integer VMX delegation removed

All remaining integer VMX helpers now execute against private registers, replacing the live KPX CPU callback for **83 additional names / 90 forms**. Complete destination staging preserves aliases. The portable implementation independently handles arithmetic/saturation, carry, averages/min/max, comparisons/CR6, products/sums, shifts/rotates, packs/unpacks and pixels. Vector FP still uses the production callback and stays outside VERIFY pending its separate ISA audit.

All **131 integer/permutation/VSCR forms** pass actual KPX/local-C/ARM64/private comparisons with 15 alias topologies, 16 lane patterns and SAT off/on. Twenty-eight literal boundary cases and all 2,048 decoder-slot classification checks provide additional validation. Adversarial callback tests demonstrate host isolation; both production chain paths retain literal vector, SAT and CR6 results. Four Debug/Release/vector-inline configurations each pass **37,923,491 core checks**, three **205-check captures** and **34,585 MMU checks**. ASan/UBSan kernel stress completes **132,480 executions** without errors. The cloned guest completes 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-file write with **538.2 million PPC block comparisons and zero mismatches**. That aggregate count supports integration, not per-opcode conformance or a performance gain. Full evidence, remaining FP/exception/encoding scope and unchanged block restrictions are recorded in `NW-PPC-JIT-PLAN.md` and `JIT_OPS.md`.

## Vector FP delegation removed

The October 3 milestone gives all **22 vector FP names / 26 forms** private register execution and admits them to isolated VERIFY. FP no longer borrows the live KPX CPU. Vector RN/environment ownership, NJ handling, NaN precedence/quieting, signed zero, single-rounding fused operations and bounded saturating conversions are implemented; matching defects in the independent KPX reference were repaired. Estimates use a documented mathematical profile, with independent sampled ISA accuracy bounds and literal special values, rather than a physical CPU's exact approximation bits.

Four Debug/Release/vector-inline configurations each pass **57,864,241 core checks**, three **205-check captures** and **34,585 MMU checks**. The FP matrix covers aliases, host/scalar rounding, NJ, SAT and exceptional operands. Twenty-six additional literal FP cases, estimate accuracy/monotonicity samples, hostile callback isolation and both production integer/FP chain paths supplement implementation comparisons. ASan/UBSan stress passes **168,960 FP** and **132,480 integer** kernel executions without failures. The generated ARM64 FP path currently calls the portable helper; an independent NEON FP path and performance qualification remain open. The cloned guest completes 300 desktop seconds, 16 mouse targets, 20 interrupts and a shared-file write with **544.3 million PPC comparisons and zero mismatches**. This is integration evidence, not per-opcode qualification or measured speedup. Full evidence and the remaining exception/encoding/translation gates are recorded in `NW-PPC-JIT-PLAN.md` and `JIT_OPS.md`.

## VMX dispatch and exception boundaries repaired

The next audit reproduced a production defect: decoder-invalid primary-opcode-4 words were accepted by the JIT, and the legacy VMX callback restored the old PC after the interpreter raised an illegal-instruction exception. The native block could then advance past that exception. Dispatch, direct compilation and local C now reject unknown/malformed forms. Twenty fixed-zero encoding constraints are shared with New World decode and the opcode generator; malformed words receive precise program exceptions through the interpreter.

All immediate domains now have independent literal checks, including signed splats, lane splats, byte shifts and conversion scales. Real CPU-loop fixtures qualify high-prefix vector-unavailable/program entry after completed integer prefixes, both successor paths and unavailable-before-data-access ordering. Four host configurations pass **59,985,581 core checks**, three **205-check captures**, **196,608 C/Python policy probes** and **34,599 MMU checks** each. Sanitizer stress remains clean. The MMU sweep's six malformed unpack fixtures were corrected and malformed-form rejection assertions added.

The isolated Debug guest repetition passes 300 desktop seconds, a shared-folder write, 16 mouse targets and all 20 injected interrupts with **543.7 million PPC comparisons, zero misses** (`/tmp/g8/ppc-vmx-boundary-verify-guest-retry2/results.json`, runner exit 0). Guest system errors, unexpected CPU returns, script errors, mouse failures and IRQ stalls are zero; maximum observed VBL service latency is 86,776 microseconds. The first attempt had clean PPC comparisons but failed its scripted file action; both outcomes are retained in `NW-PPC-JIT-PLAN.md`. Broader exception/operand combinations, direct FP NEON, performance work and `vslo/vsro` remain open. The normal launcher binary is unchanged.

## Further VMX exception priorities and replay boundary repaired

Illegal primary-opcode-4 words previously received vector-unavailable exceptions when VEC was disabled. New World unavailable dispatch now checks architectural decode validity first, so unknown/malformed words receive their program exception. DSI entry also increments the CPU exception counter: without it, isolated replay could execute a cached suffix when the handler vector matched the next sequential PC. Independent load/store tests reproduce that register/PC corruption at `0x2fc -> 0x300` and confirm the repair.

The expanded harness includes 1,200 synchronous CPU-loop fixtures, 360 selected asynchronous/DEC-write boundaries, 144 streaming/VRSAVE positive controls and two vector/next-PC alias regressions. Four configurations each pass **60,263,131 core checks**, three **205-check captures**, **196,608 policy probes** and **34,600 MMU checks**. Private-kernel sanitizer stress remains clean. The original behavior fails 522 assertions in the expanded harness. The isolated Debug guest passes 300 desktop seconds with **544.2 million PPC comparisons and zero misses**, 16 mouse targets, all 20 injected interrupts and a shared-folder write. Guest system errors, unexpected CPU returns, script errors, mouse failures and IRQ stalls are zero; maximum observed VBL service latency is 81,677 microseconds. Final evidence and remaining limits are recorded in `NW-PPC-JIT-PLAN.md`; the normal launcher binary is unchanged.


## Instruction-fetch context invalidation and opcode-31 VMX audit

The instruction translation cache could retain a supervisor mapping after a user return, or a translated handler mapping after exception entry cleared relocation. Its MSR hook now invalidates on IR or PR changes, and interpreter `mtmsr`/`rfi` plus generic/DSI exception entry notify that hook alongside the existing native callbacks. The previous behavior fails 192 assertions in the private fetch regression baseline.

Added 2,160 actual CPU-loop instruction-fetch fixtures for missing/protected/no-execute/guarded translations, page-edge prefixes and selected pending events; 60 warmed-cache context transitions; and 64 MMU context assertions. Fifteen shared opcode-31 AltiVec constraints now reject selected malformed memory/streaming forms through decode, policy, local C and emission. The policy uses the architecture's permitted program-exception alternative for invalid forms; it does not classify all reserved opcode-31 forms as mandatory illegal instructions. Coverage includes 1,488 malformed CPU-loop cases, 1,176 valid encoding controls and expanded policy parity probes.

All four configurations pass **61,015,602 core checks**, three **205-check captures**, **393,216 policy probes** and **34,664 MMU checks**, with clean private-kernel sanitizer runs. The isolated Debug guest passes 300 desktop seconds with **537 million PPC comparisons and zero misses**, 16 mouse targets, all 20 injected interrupts and a shared-folder write. Guest system errors, unexpected CPU returns, script errors, mouse failures and IRQ stalls are zero; maximum VBL service latency is 74,538 microseconds. The existing script completed its shared-folder action later in the same run after an intermediate screenshot showed the CD window. Final evidence and scope limits are recorded in `NW-PPC-JIT-PLAN.md`. The normal launcher binary is unchanged.

## Octet shifts qualified in ordinary VMX blocks

Removed the special `vslo`/`vsro` block-ending rule. Both retain their private alias-safe helpers; execution-class boundaries and the 32-instruction limit remain. Independent byte-array checks cover 23,040 dependent blocks across every control byte, all five operand-alias partitions, DR off/on and lengths 4/16/32. Added 128 cold-cache builder checks, 128 completed-prefix/DSI checks and 256 production successor/event checks. All four configurations pass **74,346,035 core checks**, three **205-check captures**, **393,216 policy probes** and **34,664 MMU checks**, with clean private-kernel sanitizer runs.

Separate cloned current ON and VERIFY guests each complete 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared-folder write. VERIFY completes **540.2 million PPC comparisons with zero misses** and records actual shift-helper use. Guest system errors, CPU outer returns, script/mouse failures and IRQ stalls are zero. These tests qualify current execution; they do not establish a performance gain. The normal launcher binary is unchanged.

The historical investigation remains inconclusive about the original cause. A September 19 source snapshot with only the block restriction removed reaches Finder; the restricted control does not within the same observation period. Its earlier default `vor` helper rejected these shifts before that default and is not evidence of the lock's cause. A single historical pair cannot prove a permanent lock or speedup, and it does not reproduce the reported unrestricted-block failure. The checklist separates completed current-code qualification from open historical attribution. Full source diffs, build adjustments, binary hashes, screenshots and qualification evidence are recorded in `NW-PPC-JIT-PLAN.md`.


## P4 system observations and P5 branch successors

Supported system operations now use typed, ordered observations instead of borrowing the live CPU during private replay. Native execution independently calculates instruction operands, privilege/trap decisions, PC/MSR transitions and exception SRRs; host services and timer reads execute once. User privilege checks and the PVR/SPRG shortcut are repaired, `rfi` aligns its target, and `dcbz` probes and completes an aligned 32-byte line with PPC/68k invalidation. Cache/synchronization behavior follows the existing emulator model. This certifies selected instruction behavior around observed services, not independent hardware/MMU conformance or every SPR/MSR profile.

Conditional and LR/CTR successors now chain to the actual completed PC. Both production paths retain event, MMU/cache, host-68k and FP/vector gates, selective register ownership and bounded hops. Native frames are popped before transfer, and the C loop no longer reuses an initial block's successor after native tails. Literal branch fixtures and stale-vector pixel-corruption negative controls qualify these changes. Those controls reproduce the recorded historical mechanism; the original full guest black-frame incident has not been independently replayed.

All four Debug/Release/vector-inline configurations pass **75,043,992 core checks**, four offline captures, **393,216 policy probes** and **34,664 MMU checks**, with clean FP/integer sanitizer stress. The cloned ON guest passes 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared-folder write, with no guest/script/cursor/IRQ errors and visually clean Finder graphics. The same binary’s cloned VERIFY guest completes the same workload with **521.4 million PPC comparisons and zero misses**, zero guest/script/cursor/IRQ errors, and a maximum observed VBL latency of 98,545 microseconds. VERIFY disables production chaining; independent host cases and ON integration qualify those branch paths. Aggregate comparisons and instrumented boot times establish neither per-opcode coverage nor a measured speedup.

Full fixture counts, logs and scope limits are in `NW-PPC-JIT-PLAN.md`; the remaining work is tracked in `JIT_OPS.md`. Performance measurement, broader conformance and historical incident attribution remain open. The normal launcher bundle is unchanged.

## P6 first scalar milestone

The first P6 scalar changes repair `fctiw`/`fctiwz` rounding-before-range checks, FR/FI replacement, invalid/inexact sticky causes and enabled-invalid destination suppression. `fcmpo`/`fcmpu` now have distinct quiet/signaling-NaN behavior, retain input bits and publish CR/FPCC even on enabled invalid operations. FX is set for newly accrued cause bits, and the selected New World flags no longer depend on the legacy arithmetic exception switch. Reference numeric rounding and native integer-significand rounding are independent. The existing undefined conversion upper-word/FPRF profile is retained.

Selected enabled FP exceptions now stop the native block at the correct PC and publish live CPU exception entry once; private replay computes exception state without a host callback. Immediate delivery is used for both imprecise FE modes. The host FP environment is restored before publication, including the helper compare path; optional inline comparisons remain available. Bit-preserving move/select fixtures cover signed quiet/signaling-NaN payloads and aliases. Broader arithmetic NaN, sticky cause/FR/FI, enabled overflow/underflow/zero-divide, fused and estimate behavior remains open, along with wide memory and invalid-form auditing.

The corrected prior-build conversion/comparison control fails **1,425,600 assertions**. A separate pre-correction callback control fails **5,568 checks**. All eight Debug/Release × FP-inline off/on × vector-inline off/on configurations each pass **89,316,147 core checks**, four capture replays, **393,216 policy probes** and **34,664 MMU checks**, with clean FP/integer kernel sanitizer stress. New literal fixtures cover 319,488 conversion, 36,864 comparison, 1,040 move/select and 1,656 production entry/commit engine cases. The old version-1 conversion capture has an explicitly revised reference FPSCR expectation, so it is not presented as an unchanged historical capture. Full evidence and remaining scope are recorded in `NW-PPC-JIT-PLAN.md`. The final cloned ON guest passes 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and a shared-folder write, with zero guest/cursor/script/IRQ errors and clean Finder graphics. Its first VERIFY clone passes 518.9 million comparisons with zero misses and all mouse/interrupt checks, but fails the shared-directory creation condition. That failed run is retained. The repeat with an explicit Unix-window readiness gate creates the directory, but exposes a cached-NOP/current-branch mismatch during boot and one resolved IRQ stall. Both failed runs and the source capture are retained. The original writer is not identified by the capture alone. No performance improvement is claimed.

## Raw-read publication found during qualification

Investigation of the overwritten-source capture independently reproduces a concrete gap: `Sys_read` can fill a raw guest buffer without notifying either JIT engine. The shared Unix backend now publishes the reported completed byte count through a SheepShaver PPC-only callback, including short reads; EOF, invalid handles and ordinary host buffers do not publish a guest write. Eight real-file ON/VERIFY cases cover page crossings, untouched neighboring code and execution of a replacement branch. The old backend fails 12 publication assertions, while the repaired focused suite passes 124 checks. This closes the selected synchronous raw-read gap; other raw host/DMA writers and noncontiguous buffer behavior remain open.

All eight final host configurations pass **89,316,271 core checks**, five capture replays, **393,216 policy probes** and **34,664 MMU checks**, with clean FP/integer kernel sanitizer stress. The new 205-check source capture was tested separately in every configuration and is now automated. It compiles the current source afresh; the complete old cached source and original writer were not captured, so exact incident attribution remains open. The latest cloned ON guest passes all 300 desktop seconds, 16 mouse targets, 20 ordered interrupts and the shared write, with zero guest/cursor/script/IRQ errors. The same binary’s fresh VERIFY clone also passes 300 desktop seconds, **508.7 million PPC comparisons with zero mismatches**, all 16 mouse targets, all 20 ordered interrupts and the shared write, with zero guest/cursor/script/IRQ errors. Final ON/VERIFY Finder screenshots show clean graphics and the created shared directory. Maximum observed VERIFY VBL latency is 110,563 microseconds. Both latest live integration gates pass; this does not establish the original stale-source writer or a performance gain. Final evidence and retained failed runs are in `NW-PPC-JIT-PLAN.md`; this update supersedes the earlier scalar-only candidate counts.


## P6 round-to-single repair

`frsp` now handles raw NaN payload projection and signaling-invalid destination suppression, rounds finite inputs under every guest RN mode, replaces FR/FI and records sticky causes/FPRF independently of the disabled legacy arithmetic switch. Enabled overflow/underflow use PPC's rounded significand and -192/+192 adjusted exponent results; disabled cases use RN-dependent saturation and gradual single underflow. Exception delivery owns the faulting instruction PC, restores host FP state before publication and suppresses the suffix. Infinity preserves FR/FI; disabled-overflow FR is explicitly zero within the architecture's undefined profile.

The numeric reference and integer-significand native kernel are independent. ARM64 blocks call the complete kernel for `frsp` in both inline settings; the former bare host conversion lacked its status and special result policy. This does not qualify other inline arithmetic or claim a measured speedup. Literal fixtures cover **248,832 engine cases**, including **82,944 production entry/commit cases**. The old helper candidate fails **926,640 assertions**. All eight final host configurations pass **139,445,268 core checks**, five captures, policy and sanitizer checks, and **34,664 MMU checks** each. The final cloned ON guest passes 300 desktop seconds, all 16 mouse targets, all 20 ordered interrupts and a shared write, with zero guest/cursor/script/IRQ errors and clean Finder graphics. The same binary’s fresh VERIFY clone also passes 300 desktop seconds, **520 million PPC comparisons with zero mismatches**, all 16 mouse targets, all 20 ordered interrupts and the shared write, with zero guest/cursor/script/IRQ errors. Both final Finder screens are clean. Maximum observed VERIFY VBL latency is 204,048 microseconds. The selected `frsp` host/live qualification gates pass; aggregate comparisons and boot timing do not establish a speedup. Detailed evidence, CPU/profile limits and the still-open arithmetic, memory and historical attribution work are in `NW-PPC-JIT-PLAN.md`.


## P6 basic arithmetic special-result repair

The eight existing add/subtract/multiply/divide forms now preserve full arithmetic NaN payloads in both precisions, select A before B/FRC, quiet signaling inputs and record the distinct invalid causes. Canonical generated quiet NaNs, VE/ZE destination and FPRF suppression, finite-nonzero/zero ZX, infinity/zero controls, signed zeros and special-case FR/FI are modeled explicitly. Valid finite FPRF updates work with VE enabled. Selected FE delivery, including pre-existing enabled status, owns the instruction PC and prevents suffix execution; host FP state is restored before the live callback. The numerical reference and raw-bit native special-result kernel are independent. Ordinary normal finite single arithmetic stays emitted after integer exponent checks; no measured speedup is claimed.

Literal tests cover **2,525,184 engine cases**, including **841,728 production ON/VERIFY cases**. The bounded old-helper control fails **1,736 assertions**; the final expanded focused inline Debug suite passes **508,688,529 checks**, zero failures. Square-root opcodes are absent from both the current decoder and JIT; eight illegal-instruction controls preserve their rejection. Optional square-root support is a separate profile/decoder task. Complete finite FR/FI/OX/UX/XX and adjusted exception results, fused behavior and estimate profiles remain open; this special-result milestone does not close the scalar arithmetic umbrella. Host/live evidence is recorded in the plan.


All eight Debug/Release × FP/vector-inline host configurations pass **648,133,797 core checks**, all five captures, the **393,216 policy probes** and FP/integer sanitizer suites, plus **34,664 MMU checks** each. Failures are zero. Detailed log paths and the exact live-candidate hash are in the plan.


The same private Debug binary's ON and VERIFY clones each pass **300 desktop seconds**, all **16 mouse targets**, all **20 ordered interrupts**, shared-directory creation and heartbeat/desktop checks. Both have zero guest/cursor/script/IRQ errors and clean final Finder graphics. VERIFY passes **524.7 million PPC comparisons with zero mismatches**; its maximum observed VBL latency is **87,598 microseconds**. App and seed NVRAM hashes match between runs. The selected special-result host/live gates pass. Finite status/adjusted results and square-root/fused/estimate work remain open; aggregate comparisons and boot timing establish no speedup. Normal launcher artifacts remain unchanged.
