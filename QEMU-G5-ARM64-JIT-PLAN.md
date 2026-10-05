# QEMU G5: direct PPC → ARM64 with TCG fallback

Source review and implementation plan, October 2, 2026.

Reviewed branch: `cat7/qemu:powermac73`, pinned to
`c811969d3e62080eae25081ad7deada0d7007881`. The branch was downloaded
to `/tmp/qemu-powermac73-jit-review` for inspection. This document is a
plan; no QEMU implementation, build, guest boot, or speed measurement was
performed. Existing SheepShaver changes in this workspace were left untouched.

## Recommendation

Proceed with a default-off experimental fast path for the G5 machine on
Apple Silicon, while keeping QEMU's PPC CPU state, MMU, exception delivery,
timers, devices, and TCG fallback authoritative. First prove the dispatch,
fault, invalidation, and code-allocation contracts. Then emit integer code.

The supplied findings get the division of responsibility right. Their main
underestimate is the integration around the emitter. A custom translation
cache needs instruction-fetch validation, code-page ownership, invalidation
from both CPU and external writes, helper fault handling, and a safe lifetime
for executing native code. One dispatch hook is a suitable entry point, but
it cannot provide those services by itself.

TCG already produces ARM64 machine code on this host. The proposed advantage
must come from cheaper dispatch, better register residency, or better code
for measured hot PPC sequences. Replacing TCG IR with direct emission does
not establish a speedup. Helper-based memory can make the first correct
implementation slower.

“Parallel” means different guest vCPUs can use the fast path on their existing
MTTCG threads. For each vCPU, only one execution engine runs at a time. A
verification engine runs on isolated test state, not concurrently against
the live guest and its devices.

## Evaluation of the supplied findings

| Finding | Source-grounded evaluation | Plan adjustment |
|---|---|---|
| Tip is `c811969d`, version `11.1.50` | Confirmed. The tip is a merge of `g5-macos-timer`, and the tree contains custom U3/G5 work. The version string does not make the fork “stock QEMU.” | Pin the full SHA, compiler, build flags, firmware, disk, CPU model, and machine arguments in every result. [Version][version], [commit][commit]. |
| U3 machine, up to four CPUs; PPC translator remains TCG | Confirmed: `mac99` selects U3 for 970 input, defaults to `970fx_v3.1` in the PPC64 executable, and uses `KEYLARGO_MAX_CPU = 4`. Its PPC TCG ops point to `ppc_translate_code`. | Scope the experiment to the U3 configuration, not every machine accepting a 970. Require `via=pmu` or `via=pmu-adb` for SMP as the machine does. [Machine][machine], [CPU ops][cpuops], [CPU limit][cpulimit]. |
| Hook after interrupt handling, before `tb_lookup` | Good location, provided cflags and breakpoint checks have already run. The loop carries `last_tb`, which is subsequently used to patch a TCG jump. | On custom progress clear `last_tb` and restart the loop. Never execute TCG using the pre-progress `s`. [Execution loop][loop]. |
| False means fallback; true means progress | Needs a stricter definition. “Write state and return false” after execution is unsafe with a previously captured lookup state. | False means zero guest instructions completed and architectural execution state unchanged; true means a committed prefix, followed by redispatch. Faults may longjmp instead of returning. |
| Spill before faulting helpers | Necessary. Ordinary helpers use `GETPC()`, and QEMU restores NIP using host-PC lookup in the TCG code buffer. A separate cache is outside that lookup. | Set the exact faulting `env->nip` before the call, use explicit-retaddr memory wrappers, and audit every helper before direct use. [State restoration][restore], [PPC raise][raise]. |
| Reuse `POWERPC_MMU_64B` and softmmu | Correct for 970. `ppc_cpu_tlb_fill` delegates to `ppc_xlate`, which selects hash64. Real mode also belongs to this existing implementation. | Add an adapter to existing instruction/data MMU interfaces; do not implement another walker. [970 model][model], [TLB fill][fill], [MMU dispatch][xlate]. |
| Key `(nip, hflags)` plus generation on `tlb_flush` | Incomplete for a separate cache. QEMU has full, MMU-index, page, and range flushes, with synced cross-vCPU variants. | Include physical instruction identity and code generation; invalidate on every relevant effective flush, not just `tlb_flush()`. [TLB flush paths][tlb]. |
| Hook `tb_invalidate_phys_range` by guest physical page | Necessary but insufficient. The normal softmmu code-write path also calls `tb_invalidate_phys_range_fast`. TCG page ownership triggers `TLB_NOTDIRTY` handling. | Custom-only pages must be protected too. Cover both invalidation paths and prevent TCG from unprotecting a page still owned by the custom cache. Use QEMU RAM identities, accounting for aliases. [Page ownership and invalidation][maintenance], [Not-dirty writes][notdirty]. |
| `icbi` alone does not invalidate translated blocks | Correct in this fork: the helper performs a data-side MMU load to check the access. | Preserve that behavior. Detect code writes through shared QEMU mechanisms; do not rely on the guest issuing `icbi`. [ICBI helper][icbi]. |
| `dcbz` is 128 bytes on 970 | Default cache-line size is 128. However the 970 opcode path can call `helper_dcbzl`, which uses 32 bytes for a particular HID5 value. | Keep the existing opcode/HID5/helper behavior; test default and special cases. An unconditional 128-byte replacement would also be wrong. [Cache helpers][cachehelpers], [Opcode selection][dcbzdecode]. |
| Reuse split W/X, do not share TCG's buffer | Good goal, but `tcg/region.c` is built around singleton region state and global `tcg_splitwx_diff`. It is not a reusable independent arena API today. | Extract a parameterized allocator or add a narrowly scoped independent arena using the same mapping method. Each arena needs its own RW/RX addresses and conversion. [Darwin allocation][allocation]. |
| Timebase comes from QEMU | Correct. The machine creates per-CPU timebase objects and initially copies CPU0 offsets to secondaries. | Use `cpu_ppc_load_tbl/tbu` or audited existing helpers, not raw struct arithmetic or a private host counter. Test later writes and reset as well as initial alignment. [Machine timebase][machinetb], [Timebase helpers][timebase]. |

## Scope and compatibility

Initial supported configuration:

- Darwin ARM64 host, system emulation, `ppc64-softmmu`, TCG enabled,
  actual native ARM64 backend rather than TCI.
- U3 `mac99` configuration, initially the exact `970fx_v3.1` model used by
  the pinned branch. Expand to other 970 variants only after model-specific
  tests. A family/excp-model check alone is not a substitute for that audit.
- One vCPU until SMP qualification. A request with more CPUs uses TCG and
  reports the reason during this stage.
- A proposed `-accel tcg,ppc-arm64=on` property, default off. This property
  does not exist in the reviewed tree yet.
- Both 32-bit and 64-bit guest execution modes must be handled or explicitly
  refused. `ppc64-softmmu` does not imply MSR[SF] is always one.

x86 hosts, `ppc-softmmu`, linux-user, G3/G4, pseries, and other accelerators
keep their current execution behavior. Linux ARM64 may later be useful as
a test host but is not the first platform gate. Do not add a blanket PPC
translator replacement or change the machine's default CPU.

Disable custom execution when any unsupported execution facility is active:

- icount and record/replay;
- GDB stepping, guest MSR single-step or branch-step;
- breakpoints or watchpoints, including insertion while running;
- requested special next-TB cflags, instruction limits, or one-insn-per-TB;
- TCG plugins until equivalent instruction/memory instrumentation exists;
- guest instruction-counting PMU modes and other translator side effects
  not implemented by the subset;
- diagnostic modes requiring TCG TB traces that this engine cannot supply.

Use live checks at dispatch so attaching a debugger or changing a setting
is effective. Preserve the fact that `cflags_next_tb` was explicitly set:
the existing loop consumes it before lookup. Start with conservative
rejection of all explicit requests. Inapplicable builds need a well-defined
property policy: absence or a clear error for `on`; no silently ineffective
promise of acceleration.

## Dispatch and fallback contract

The proposed signature can stay:

```c
bool ppc_try_arm64_exec(CPUState *cs, const TCGTBCPUState *s);
```

Use C in the QEMU implementation. Keep target-independent integration narrow:
either a target hook in `TCGCPUOps`, null for other targets, or a guarded
target adapter with a false stub. An optional target hook is preferable if
it avoids including PPC internals in generic `accel/tcg` files. Do not expose
the ARM64 emitter or `CPUPPCState` there.

Insert after cflags selection and `check_for_breakpoints()`, before
`tb_lookup()`. Illustrative control flow, not an apply-ready patch:

```c
/* Interrupt handling and the existing cflags/breakpoint code ran above. */
if (eligible_for_target_fast_path(cpu, &s, had_explicit_next_tb_request) &&
    target_try_fast_exec(cpu, &s)) {
    last_tb = NULL;      /* no TCG edge can span custom execution */
    tb_exit = 0;
    align_clocks(sc, cpu);
    continue;           /* interrupt poll, then fresh get_tb_cpu_state */
}
/* No guest instruction ran: existing tb_lookup/TCG execution follows. */
```

The adapter has three outcomes:

1. **Decline:** no instruction executed. Registers, NIP, and guest-visible
   execution effects are unchanged. Ordinary QEMU translation bookkeeping
   from an instruction-fetch probe is allowed. TCG proceeds with `s`.
2. **Progress:** at least one instruction completed. All completed results
   are in `CPUPPCState`; NIP is the next instruction to execute. Return true,
   discard `s`, and reenter interrupt handling.
3. **Fault/CPU exit:** fault state is committed and QEMU exits through its
   existing longjmp path. The adapter's ordinary epilogue may never run.

For an unsupported opcode after a supported prefix, leave NIP pointing to
that opcode and return true. On the next dispatch, decline at that opcode
so TCG executes it. Do not accidentally skip it, execute it twice, or
repeatedly return true without progress.

A taken self-branch is real progress even if NIP is unchanged. Track completed
instructions independently of a PC comparison. Keep an internal exit reason
for counters and tests even if the public function remains boolean.

Initially compile at most 32 guest instructions, within one QEMU instruction
page, ending at a branch, unsupported instruction, or budget. This is a
starting policy to tune with evidence, not an architectural constant.
Return to the QEMU loop after every block; check the existing CPU exit
request during lengthy work. Do not invent a second event dispatcher.

TCG can chain TBs without returning through this hook. That preserves
correctness but may make eligible hot code rarely reach the custom engine.
Measure actual coverage. Keep ordinary TCG chaining for the baseline and
for fallback initially. If coverage is poor, evaluate selective chain exits
into eligible regions as a separate optimization. Never claim a speedup
against TCG artificially forced to dispatch every instruction.

## State and helper fault handling

`CPUPPCState` remains the sole live architectural state. JIT metadata may
describe cached code, guards, or a current block, but must not become a
second CPU register file.

Important representation details:

- GPRs are 64-bit in this build; narrow-mode address/PC wrapping and each
  opcode's word-result semantics follow the existing translator. Reusing a
  SheepShaver W-register convention would silently lose upper halves.
- CR lives in `crf[8]`. Preserve field numbering and SO inclusion, signed
  versus unsigned comparisons, and Rc behavior.
- XER is split: `xer`, `so`, `ov`, `ca`, plus `ov32` and `ca32` fields. Audit
  which fields the 970 translator reads/writes for each form; do not assume
  writing the packed `xer` field updates carry or overflow.
- LR and CTR are separate fields. BO/BI logic, CTR decrement, LK updates,
  target alignment, and indirect-branch target capture must match TCG.
- FP and vector values use QEMU's `vsr[]` and associated FPSCR/VSCR/softfloat
  state. Their layout differs from the SheepShaver context.

These layouts are visible in [the CPU structure][cpustate].

Start with conservative state writeback. At every potentially faulting
call, the state must represent the instruction's entry: preceding completed
instructions committed, current NIP set, and current instruction's outputs
not yet committed. For `lwzu`, for example, load into a temporary, then
commit destination and RA only after success. Apply the same rule to store
updates, aliasing operands, and later FP/vector operations.

Provide C memory wrappers using explicit-retaddr `cpu_ld*_mmu` and
`cpu_st*_mmu` APIs with the correct MemOpIdx. For the initial separate-cache
design, a zero restoration address is a candidate because NIP has already
been set and `cpu_loop_exit_restore` skips restoration for zero. Validate
each called path, including alignment faults and MMIO. Do not pass a fake
TCG PC or assume a custom ARM64 PC can be resolved by `tcg_tb_lookup`.

For ordinary helpers that use `GETPC()` internally, audit whether failure
to find a TCG TB leaves the committed state intact and whether any path
requires unwind metadata. Wrap or adapt selected helpers; leave the rest
in TCG. A blanket call to all `helper_*` functions is not a supported ABI.
In particular `cpu_io_recompile` requires a real TCG TB. With icount/replay
disabled, establish an audited `can_do_io` policy for custom memory helpers
so MMIO cannot reach that incompatible recompile path. See
[MMIO preparation][ioprep] and [I/O recompilation][iorecompile].

Faults must preserve existing DAR/DSISR/error-code/SRR behavior. Alignment
handling reloads the instruction at NIP, so accurate NIP matters beyond the
return address itself. See [PPC alignment handling][alignment].

Longjmp cleanup is an explicit implementation task:

- Do not rely on C++ destructors or a normal function return.
- Do not hold private compilation/page locks across faulting calls unless
  QEMU cleanup knows exactly how to release them.
- Clear current-block pins, private temporary state, and any JIT write
  mode through a cleanup path reached on both normal return and longjmp.
- Audit `can_do_io`, plugin helper state, and lock ordering against
  `cpu_loop_exit` and `cpu_exec_longjmp_cleanup`.

The early subset stops before `sc`, `rfid`, traps, privileged instructions,
and synchronous-exception forms. TCG must execute those instructions to
establish the exception or perform the return. Merely stopping before `sc`
does not cause `ppc_cpu_do_interrupt` to know a syscall happened. DEC and
external interrupts continue through QEMU's existing requests and delivery.

## MMU, instruction identity, and cache invalidation

Reuse the current CPU MMU. There are two distinct responsibilities:

1. **Data access:** correct data MMU index, width, sign extension,
   endianness, alignment/atomicity policy, permission checks, hash-table
   reference/change effects, RAM/ROM/MMIO dispatch, and access faults.
2. **Instruction access:** instruction MMU index and execute permission,
   code backing identity, page boundaries, fetch faults, and instruction
   endianness. A data-side load or debug physical translation is not an
   instruction-fetch substitute.

Use the existing `get_page_addr_code_hostp`/instruction-fetch machinery as
the model. It produces a QEMU RAM address for cacheable code and rejects
non-RAM/small mappings for normal cached translation. Initially decline
uncacheable instruction mappings and MMIO code to TCG. Do not perform a
side-effecting “peek” through MMIO and then let TCG repeat it.
See [code address resolution][codefetch].

Limit compilation to the validated current instruction page; do not fetch
past a branch or unsupported boundary. Do not raise a fault for a future
page before the guest has executed the supported prefix leading to it.
Resolve the mapping and take the shared code-page synchronization before
reading/publishing code so concurrent writers cannot race publication.

Suggested initial per-vCPU cache identity:

```text
(guest NIP, full translation hflags, supported cflags policy,
 instruction backing RAM/ROM identity + offset,
 local translation epoch, instruction-page code epoch)
```

The fixed CPU model/machine and supported execution facilities are part of
cache ownership and eligibility. Do not put changing register values in the
key. Future branch links must validate the same context, physical target,
and generations, not merely equal hflags.

At dispatch, validate instruction identity through existing softmmu state
or an equally audited mapping guard. Initially use broad local epoch bumps
for any relevant effective TLB invalidation; later make them page-selective
only if the flush traffic warrants it. Instrument full, MMU-index, page,
range, reset, and address-space update paths. Do not derive validity from
QEMU's diagnostic flush counters.

Preserve lazy PPC flush semantics. `tlbie`/SLB operations can set pending
bits; the translator performs local checks at `isync` and global checks
at relevant `ptesync`/other architecture-specific synchronization points.
Global checks can schedule synchronized CPU work and must terminate
execution before proceeding. Calling one mutation helper may omit checks
and pending-bit updates emitted by the translator itself. Initially leave
these instructions in TCG. [PPC flush processing][ppcflush],
[storage-control translation][storage], [barrier translation][barriers].

A soft-TLB mapping flush and code-byte invalidation are different events.
An unchanged page at a new mapping may reuse code later if all identity
checks pass; changed instruction bytes must invalidate every alias. Start
conservative, then reduce unnecessary recompilation with measurements.

### Code-page ownership is required before cached execution

Add a shared QEMU interface that lets a custom cache register/unregister
translated code ranges without pretending they are ordinary TCG TBs:

- Track TCG and custom owners of each QEMU RAM page under the established
  page synchronization. The existing `PageDesc` infrastructure or an
  equivalent integrated registry must account for custom-only pages.
- Protect a page on its first code owner, including reset of already cached
  writable TLB entries through QEMU's normal dirty tracking.
- A TCG TB removal must not unprotect a page with a remaining custom owner.
- CPU writes, TCG writes, DMA/device writes, debugger writes, and RAM writes
  through relevant external interfaces must notify both engines.
- Cover `tb_invalidate_phys_range_fast` as well as
  `tb_invalidate_phys_range`; audit other paths reaching the common worker.
- Do not key this registry solely by guest physical address. The TCG code
  path uses RAM backing addresses; mapped aliases must identify the same
  bytes. ROM backing/lifecycle requires an explicit policy as well.
- Full TB flush, VM restore, reset, cache exhaustion, CPU teardown, and
  memory-region lifecycle changes invalidate or safely retire custom code.

Merely adding a callback inside one invalidation function fails when a
page contains only custom blocks and QEMU never marked it as code.

If a store modifies a current custom block, complete that store's
architectural results and return before executing a stale suffix. An
initial simple policy is to end every custom store block and redispatch;
this also bounds memory-map changes triggered by MMIO. Later allow longer
blocks only with a proven invalidation/exit guard.

The current PPC `TCGCPUOps` does not set `precise_smc`. Therefore do not
assume the generic TCG precise-SMC retry machinery automatically provides
this behavior to custom code. Define and test the custom exit policy,
including differences in timing allowed by PPC synchronization semantics.

## Apple Silicon code memory and native ABI

Choose an independent RW/RX arena using the mapping technique from
`tcg/region.c`, with private allocator state. Do not point at the TCG buffer,
reuse its global split offset, or reset its code regions for custom cache
maintenance. Extracting a shared mapping allocator is reasonable; sharing
TCG's arena accounting and block allocator is a larger design change.

For the first executable milestone, require successful split mapping and
return to TCG with a clear diagnostic if unavailable. A second MAP_JIT arena
is not the fallback design. Test the actual supported macOS signing/runtime
configuration before claiming allocation support.

Publication order: emit through RW, resolve relocations, synchronize the
host instruction cache with QEMU's RW/RX-aware cacheflush utilities, then
publish an RX entry with appropriate ordering. Metadata is immutable after
publication except synchronized validity/lifetime fields. Each entry must
use its own arena's address conversion. See [cache flushing][cacheflush].

Use QEMU's `qemu_thread_jit_write/execute` conventions where the process
still needs thread-local JIT permissions for TCG's MAP_JIT mapping. Split
mapping does not require toggling permissions for every emitted opcode.
QEMU explicitly switches to execute mode before entering a TCG TB, so
the original claim that *any* return in write mode necessarily faults the
next TB is too broad. Nevertheless, custom entry, normal exit, fault exit,
and later patching need a defined permission state. [Thread permissions][wx],
[TCG execution entry][tbentry].

Native generated functions obey the host calling convention: stack
alignment, callee-saved integer/vector registers, LR preservation across
helpers, and Darwin's reserved x18. Do not borrow TCG's private register
conventions unless entering through its actual prologue. Audit BTI/CFI
requirements for supported build configurations and indirect entry points.
Do not claim arm64e/PAC support merely because standard arm64 works.

Retiring metadata is not permission to overwrite code currently running.
Initially reclaim arenas only at a proven vCPU quiescent point; for SMP use
QEMU exclusive/quiescent mechanisms or a validated epoch protocol. Longjmp
must release active references too. Add memory caps and a safe exhaustion
policy; compilation failure should cause fallback rather than corrupting
or reusing a live code range.

## Opcode coverage and SMP

First integer tranche: immediate and register ALU, logical/rotate forms,
shifts, CR compares and selected CR operations, conditional/direct branches,
then ordinary integer loads and stores. Profile the pinned guest to select
the actual list; maintain an opcode/form/mode support manifest. Invalid or
reserved forms still go to TCG and must not accidentally match a simplified
decoder. Reuse QEMU decode definitions where practical; audit legacy-table
forms separately.

Carry/overflow/Rc forms follow explicit expected-value tests before they
are enabled. Start by refusing unimplemented variants of an otherwise
supported opcode family. Test all operand aliases and width boundaries;
host NZCV is temporary, not a persistent guest XER representation.

SPR support is not required for a working integer experiment. There is no
performance reason to implement all firmware instructions before measuring
the user/kernel hot paths. Whitelist useful reads/writes only after
examining their translator-generated privilege checks, side effects, and
block exits. LR/CTR/XER moves and `mftb` may merit earlier attention, but
their SPR decode and privilege semantics still need verification.

FP and AltiVec begin with audited QEMU helpers and complete state handling.
Some instructions are implemented as TCG operations, not callable helpers;
they stay in fallback until a tested implementation exists. Preserve
facility-unavailable exceptions, softfloat rounding/flags, FPSCR summaries,
NaN propagation, signed zero, fused operations, denormals, vector lane
ordering, VSCR saturation, and operand aliases. Direct ARM FP/NEON is a
later per-form optimization with an expected-bit-pattern corpus.

SMP does not require native ARM64 load-exclusive/store-exclusive lowering
of PPC reservation operations. Keep `lwarx/ldarx/stwcx./stdcx.` in TCG
initially. Correct fallback must preserve QEMU's reservation state, including
intervening stores and cache operations. ARM exclusives cannot simply be
held across C helpers and TCG transitions.

Before enabling more than one vCPU:

- Preserve `sync`, `lwsync`, `eieio`, `isync`, and related TCG barrier
  semantics across engine transitions. A function call is not itself a
  guest memory fence. Early versions stop before these instructions.
- Use the normal synchronized global TLB work so remote caches invalidate
  when their vCPU processes the effective flush. Do not free remote code
  directly from the initiating thread or bypass required CPU kicks.
- Code-byte writes invalidate all owners even when they originated in a
  different vCPU or a device thread.
- Every block has a bounded route back to exit/interrupt/queued-work
  processing. No unbounded native chain may delay pause, reset, IPI, DEC,
  or memory-map updates.
- Test secondary release, halt/nap/wake, interrupts with EE clear, and
  timebase coherence using the fork's existing G5 behavior.

Four guest CPUs are a source-tree limit, not evidence that four-CPU Mac OS X
boots or performs correctly today. Establish that TCG-only baseline first.

## Phased implementation and acceptance gates

The property remains off by default throughout these stages. Passing a boot
checkpoint is necessary but insufficient; each correctness gate also has
focused CPU/system tests.

| Stage | Concrete deliverable | Acceptance gate |
|---|---|---|
| A — Baseline and profiling | Reproducible pinned build and guest configuration; existing TCG boot checkpoints; sampled hot PCs/opcodes and CPU/device cost breakdown. | Record TCG-only behavior for the intended OS X version and 1/2/4 CPU configurations. Preserve known failures rather than attributing them to the JIT. No runtime patch or default change. |
| B — Plumbing only | Default-off accelerator property, target hook/stub, Meson gates, eligibility reasons, counters. Stub always declines. | ARM64 PPC64 on/off reach identical checkpoints. x86 PPC64 and ARM64 PPC32 build and retain TCG behavior. Debugger, replay, plugins, and explicit cflags are correctly refused. |
| C — Executable infrastructure | Independent RW/RX arena, native ABI probe, one integer instruction, dispatcher restart, explicit-retaddr memory proof, longjmp cleanup proof, cache/page-owner interfaces. | Repeated emit/execute/fallback/fault cycles; no stale `last_tb`; no allocation/lock/permission leak. Custom-only code-page write test invalidates correctly. Execute no broad cached subset before these pass. |
| D — Integer and helper memory, one CPU | Support manifest; pure integer/branch tranche followed by scalar loads/stores; bounded one-page blocks; conservative store exits; shared invalidation active. | Independent edge-case corpus and differential blocks match. Forced misses at first/middle/last opcode, data/fetch/alignment faults, update aliases, and store-to-current-code tests pass. OS X reaches baseline checkpoints with identical architectural results where determinism is controlled. |
| E — Measure and choose coverage | Counters for custom instruction share, block sizes, TCG transitions, compile/recompile causes, helper cost, and polling latency. | Compare against normally chained TCG. If slower, identify whether dispatch frequency, helper memory, or missing hot opcodes is responsible before expanding the ISA. Keep a correct slow prototype as diagnostic evidence, not a performance claim. |
| F — Firmware/MMU/SPR helper coverage | Selected hot SPRs and privileged operations only; reproduce translator checks and lazy flush ordering. TCG continues to handle the remainder. | Real-mode stretch, IR/DR transitions, SF transitions, SLB/HPT permissions and R/C bits, same-NIP remapping, local/global TLB flush sequences, exception returns, DEC/nap/reset tests match the baseline. Tests also exercise those operations in TCG between custom blocks. |
| G — FP/AltiVec helpers | Selected operations using audited helpers, complete status and fault handling; default/special `dcbz` cases. | Bit-pattern corpus and randomized differential tests with independent expected cases pass; no host FP state leakage. Target OS X workload stays correct. Native FP/NEON remains separately gated. |
| H — SMP with TCG atomics | Per-vCPU execution caches, shared physical code ownership, remote invalidation, safe publication/reclamation, barrier/fallback audit. | Two CPUs then four: lock/contention and ordering tests, IPI/DEC latency, secondary boot, timebase, concurrent code writes, global remap, pause/reset/snapshot stress. No hangs in repeated runs. |
| I — Faster RAM and bounded linking | Audited soft-TLB fast access, selected hot-path register residency, guarded native links. | End-to-end workloads improve against the unchanged TCG baseline; fault, remap, code-write, dirty logging, SMP, and debugger gates continue to pass. Remove optimizations that fail either correctness or measured benefit. |

Do not make instruction expansion strictly linear when profiling suggests
a different hot subset. Infrastructure and correctness gates are strict;
opcode priority is driven by the intended guest workload.

### Required test matrix

**Dispatch/state:** first-opcode refusal; supported prefix then refusal;
all-supported block; taken/not-taken/self branch; LK and LR/CTR aliases;
TCG → custom → TCG → custom transitions; stepping or breakpoint insertion
while active; explicit one-instruction retry; cache allocation failure.

**Integer:** 32/64-bit modes, upper GPR halves, signed/unsigned compares,
carry/borrow and overflow edges, sticky SO, CR field selection, rotate
mask wrap, shift counts at and beyond operand width, r0 addressing rules,
update-form illegal encodings, source/destination aliases.

**Memory/MMU:** big-endian byte/half/word/doubleword and reverse forms if
supported; alignment behavior matching the specific opcode; page-crossing
access; fault at first/middle/last instruction; RA/RD preserved on failure;
MMIO executes once; real mode; separate IR/DR; SLB miss; HPT miss/protection;
reference/change bits; NX instruction page; remap same NIP to other bytes;
RAM above 4 GiB as supported by the machine.

**Code invalidation:** custom-only page; TCG-only page; mixed page; code
store from custom code and from TCG; write through a virtual/physical alias;
DMA and debugger write; store to an executing suffix; compiled target changed
before linking; concurrent compilation/write; eviction while another CPU
holds an entry; restore/reset after code bytes changed.

**Events/SMP:** timer/interrupt request during a maximum-length block;
CPU halt/nap/wake/reset; queued exclusive work; `tlbie` followed by the
architecture's synchronization sequence; remote remap and code write;
reservation operations in TCG around custom ordinary memory operations;
barrier litmus tests, spinlocks and contended locks; timebase rollover/read
sequences; longjmp during cache activity.

Use QEMU-focused host unit tests for cache/arena contracts, PPC instruction
tests for arithmetic/branches, and small system-mode PPC programs for MMU,
privilege, exceptions, and SMP. User-mode tests alone cannot qualify the
full-system path. Track concrete pass/fail evidence per gate, not only an
ever-growing aggregate assertion count.

Differential execution must clone all relevant architectural state and
memory effects. Include split XER, CR, status, reservation, exception fields,
SLB/HPT effects, and helper-visible state as applicable. Do not double-run
MMIO, DMA, timers, or timebase reads on the live machine. Use deterministic
fixtures or captured inputs for those cases; minimize any failure into a
replayable test. TCG is the comparison baseline, with independent expected
values for known edge cases to reduce shared-bug risk.

## Performance policy

Record the exact build, host, guest configuration, firmware/disk hashes,
CPU count, and acceleration flags. Use isolated disk overlays from the same
base image, fresh or identically seeded NVRAM, controlled inputs, and fixed
checkpoints. Separate cold boot from warmed application execution. Run at
least three alternating repetitions of each configuration and report all
samples, median, and spread; increase repetitions if variance hides the
effect. Avoid concurrent benchmark guests.

Required counters, disabled or sampled appropriately for release runs:

- completed custom guest instructions, executed blocks, block-length
  distribution, supported-opcode refusals and unsupported-form reasons;
- custom/TCG transitions, custom entry coverage versus eligible hot regions;
- translation time and code bytes, cache hit rate, evictions, recompiles;
- invalidation calls, entries invalidated, cause, and affected code pages;
- data-helper calls and sampled cost, memory fast-hit/miss reasons;
- event/queued-work latency and longest interval without polling;
- code publication/cacheflush cost and retained arena bytes.

Do not call generated instruction count “retired instructions”: a fault
may stop a block partway. Define successful-prefix accounting and test it.
Without icount, exception timing can vary with speed; require correct
architectural delivery and bounded latency rather than byte-for-byte live
boot logs. Use deterministic fixtures for exact exception comparisons.

Benchmark at least integer compute, memory copying/scanning, OS X boot,
and one repeatable application task. Report guest results and correctness
alongside wall time. A faster microbenchmark is useful evidence, but the
release decision uses the intended end-to-end workload.

If fraction `f` of baseline runtime is accelerable and that portion speeds
up by factor `s`, the optimistic bound before new overhead is:

```text
overall speedup = 1 / ((1 - f) + f / s)
```

Opcode frequency is not runtime fraction. R350 rendering/MMIO, USB, audio,
firmware, and device bugs can dominate the observed workload. Keep platform
fixes in separate changes so a CPU-JIT result can be attributed correctly.

For later faster RAM, prefer an audited inline lookup of QEMU soft-TLB
entries over persistent raw host pointers. Guard MMU index, address tag,
width/alignment/page crossing, permission, byte order, and slow flags such
as MMIO, watchpoints, not-dirty writes, and discard-write. Preserve required
atomicity and dirty tracking. A translation generation check alone cannot
detect code-page protection or every slow-path condition. No R350 BAR
access enters this fast RAM path. Generation guards and arena lifetimes
remain required for native branch links.

## Proposed patch boundaries

| Area | Expected files or interfaces |
|---|---|
| Accelerator property | `accel/tcg/tcg-all.c`; capability validation and default-off configuration. |
| Dispatch adapter | `accel/tcg/cpu-exec.c`, optionally `include/accel/tcg/cpu-ops.h`; preserve breakpoint/cflags ordering and loop restart. |
| PPC implementation | New `target/ppc/arm64-jit/` sources: eligibility/state, decode/support manifest, emitter, memory wrappers, cache, diagnostics. `target/ppc/meson.build` gates the real backend. |
| Shared code ownership | `accel/tcg/tb-maint.c` and `accel/tcg/cputlb.c`, plus a narrow shared interface. Account for CPU/external invalidation, protection and all relevant flush forms. |
| Host code allocation | Shared utility extracted from `tcg/region.c` or independent parameterized allocator using its technique; use `util/cacheflush.c` conventions. |
| Lifecycle and cleanup | CPU reset/teardown, VM restore/full TB flush, and `cpu_exec_longjmp_cleanup`; no native addresses in migration state. |
| Tests and docs | Host arena/cache tests; PPC instruction corpus; system-mode MMU/SMP programs; compatibility/property documentation and per-stage evidence. |

Keep these as reviewable changes with a precise gate in each description.
Do not hide shared QEMU lifecycle changes inside the opcode emitter.

## What carries over from SheepShaver

Useful starting points: decoder logic, ARM64 instruction encoding, endian
operations, CR/BO/BI/XER test cases, register-alias regressions, fault-before-
commit patterns, host ABI lessons, and measured code-publication overhead.
Port ideas and independently checked tests before porting full subsystems.

Do not carry over the 32-bit CPU context, private MMU/physical-address
decode, private DEC/timebase, MAP_JIT ownership assumptions, global cache,
or a shadow register-file convention. Audit source licenses and attribution
before moving code across projects; QEMU and existing SheepShaver files
do not necessarily use identical licenses. No guest ROM/disk bytes belong
in the implementation or test repository.

## First work package

Start with stages A–C: pinned TCG baseline, default-off stub and compatibility
gates, then a minimal executable/fault/invalidation harness. Defer broad
integer emission until a custom-only translated page is invalidated by a
TCG store and an explicit-retaddr helper fault survives longjmp with exact
NIP and state. Those tests decide whether the proposed integration is sound.

Remaining baseline questions are the intended OS X build, firmware image,
disk configuration, boot checkpoint, and existing TCG-only SMP behavior.
They do not prevent building the stub or deterministic CPU tests, but must
be fixed before claiming guest acceptance or speed.

[version]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/VERSION
[commit]: https://github.com/cat7/qemu/commit/c811969d3e62080eae25081ad7deada0d7007881
[machine]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/hw/ppc/mac_newworld.c#L216
[cpuops]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/cpu_init.c#L7523
[cpulimit]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/include/hw/ppc/openpic.h#L35
[loop]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cpu-exec.c#L934
[restore]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/translate-all.c#L196
[raise]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/tcg-excp_helper.c#L37
[model]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/cpu_init.c#L5981
[fill]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/mmu_helper.c#L1361
[xlate]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/mmu_common.c#L823
[tlb]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cputlb.c#L370
[maintenance]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/tb-maint.c#L699
[notdirty]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cputlb.c#L1337
[icbi]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/mem_helper.c#L344
[cachehelpers]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/mem_helper.c#L286
[dcbzdecode]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/translate.c#L2890
[allocation]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/tcg/region.c#L635
[machinetb]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/hw/ppc/mac_newworld.c#L216
[timebase]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/timebase_helper.c#L29
[cpustate]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/cpu.h#L1280
[ioprep]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cputlb.c#L1273
[iorecompile]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/translate-all.c#L575
[alignment]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/tcg-excp_helper.c#L218
[codefetch]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cputlb.c#L1541
[ppcflush]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/helper_regs.c#L376
[storage]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/translate/storage-ctrl-impl.c.inc#L158
[barriers]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/target/ppc/translate/misc-impl.c.inc#L24
[cacheflush]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/util/cacheflush.c#L244
[wx]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/include/qemu/osdep.h#L848
[tbentry]: https://github.com/cat7/qemu/blob/c811969d3e62080eae25081ad7deada0d7007881/accel/tcg/cpu-exec.c#L430
