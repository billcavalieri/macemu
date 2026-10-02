# New World host 68k JIT implementation

Implementation status, October 2, 2026. This accompanies `NW-68K-HOST-JIT-PLAN.md` and records the executable implementation and its qualification limits.

## Result and selection

The fallback-only/ranked-stub bridge has been replaced by a direct ARM64 integer translator. Ordinary guest execution and host `Execute68k` use the same guarded NanoKernel dispatch hook. No special launcher is required: the app's **Host 68k JIT** preference selects `jit68k_host`. The separate `jit68k` preference still selects Apple's guest DR translator.

The preference is true by default, enabled at the user's request after all six final automated stages passed. The macOS settings editor uses the same default. An explicit `jit68k_host false` setting or `NW_JIT68K_MODE=off` still disables native execution. Physical host mouse capture/release remains unqualified by the scripted tests.

| Plan phase | Current status |
|---|---|
| 2: contract, reference, memory/cache infrastructure | Implemented; selected ROM and isolated regressions validated |
| 3–4: native integer, effective addresses, memory/control flow | Implemented for the direct families below; exceptional/unsupported forms retain exact NK execution |
| 5: normal guest interception | Implemented; regular boot/Finder workloads record native execution |
| 6: remaining integer and event behavior | Shifts/bit/MUL/DIV/extend arithmetic native; uncommon/system forms exact NK; interrupt-return defect fixed; final automated device qualification passed |
| 7: efficient blocks | Bounded register and memory/control blocks, terminal branch register retention, dead flags, guarded direct branches and cache tuning implemented |
| 8: qualification/rollout | All six final automated stages passed, including two-hour active and eight-hour idle soaks; default enabled at the user's request; physical host mouse qualification remains |

The native core, block optimizations and automated qualification are complete, and default enablement is authorized. Physical host input qualification remains separate from the completed automated tests.

Optional diagnostic controls:

| Setting | Meaning |
|---|---|
| `NW_JIT68K_MODE=off` | Original NanoKernel execution |
| `NW_JIT68K_MODE=on` | Native execution; exact NanoKernel exits when guards fail |
| `NW_JIT68K_MODE=verify` | Predict native effects privately; observe the real NanoKernel in the ordinary CPU loop |
| `NW_JIT68K_BLOCKS=0` | Select scalar native execution for comparisons |
| `NW_JIT68K_DECODED=0` | Decode blocks afresh for diagnostic comparisons |
| `NW_JIT68K_CHECK_BLOCKS=1` | Compare native blocks, captured RAM reads and final write effects with scalar execution before commit |
| `NW_JIT68K_HIST=1` | Enable periodic coverage/cache reports |
| `NW_JIT68K_HIST_PATH=/absolute/path` | Override histogram output |
| `NW_JIT68K_TRACE_PATH=/absolute/path` | Bounded verifier failure traces |

Without overrides, `jit68k_host` selects native execution. Per-instruction histograms are opt-in through `NW_JIT68K_HIST=1`, so normal use has the same profiling policy as Release performance qualification. The default histogram path when enabled is `~/Library/Logs/SheepShaver/jit68k-hist.txt`. Reports use a temporary file and rename; periodic persistence also preserves useful evidence after an abnormal stop.

## Execution contract

The compatibility target is the active New World ROM's NanoKernel emulation, not an unverified claim of a full 68020/68040 CPU. Native selection checks current ROM handler fingerprints and canonical dispatch/run-mode conditions. Incompatible ABI fingerprints fall back to the real ROM. This is qualification for that known layout, not a claim of support for arbitrarily patched NanoKernel implementations.

| 68k or NanoKernel state | Verified boundary representation |
|---|---|
| D0–D7 | PPC r8–r15 |
| A0–A6 | PPC r16–r22 |
| A7 | PPC r1; distinct from ARM64 SP |
| Opcode PC | PPC r24 minus 2 |
| Next prefetched word | PPC r27, sign extended |
| Dispatch PC / target | PPC r29, aligned first word of the 8-byte opcode table entry |
| Table range | `0x68080000` through `0x680fffff` |
| X | PPC r26, bit 29 |
| N / Z | PPC CR0.LT / CR0.EQ |
| V / C | PPC XER.OV / XER.CA |
| Overflow bookkeeping | Active XER.SO and saved-X r26.SO are represented independently |
| Interrupt/wakeup/privilege | Existing CR2 and status/control r25 retained |
| VBR / EDP | Existing r28 / r31 retained |
| Next continuation | Update r24, r27, r29, LR and PPC PC together |

In particular, r25 is **not** treated as a raw architectural 68k SR, and table+4 is **not** considered another 68k instruction boundary. Native guards require r0=0, r30=`0x68060000`, the normal r23 DR-hook state, an even prefetch address, and compatible CR/special execution flags. State-changing/system services remain real NanoKernel execution.

SO cannot be reconstructed from CCR.V. Many handlers preserve it, some shift/divide helpers replace it, and long ADDX restores it from r26 before arithmetic. The adapter, scalar helpers, blocks, mixed-handoff tests and verifier account for these distinctions. Untouched raw scratch state is retained; the native path materializes the live integer/prefetch/dispatch state rather than pretending to reproduce every dead PPC temporary.

## Native policy

The decoder classifies all 65,536 opcode words. With zero extension words, 43,844 are direct candidates and 21,692 select exact NanoKernel execution. This is a candidate count, **not** a legal-opcode census: extension formats, operand addresses, run state and ROM fusion checks can change the runtime decision. The real NanoKernel also handles guest-illegal encodings and produces its normal exceptions.

Direct families include:

- NOP, MOVEQ, MOVE/MOVEA, ADD/SUB/CMP, immediate/quick/address variants, AND/OR/EOR, CLR/TST/NOT/NEG, EXT/SWAP/EXG.
- Register indirect, displacement, absolute, PC-relative, immediate, postincrement/predecrement and brief indexed effective addresses; A7 byte operations use a two-byte adjustment.
- LEA/PEA, BRA/Bcc/BSR, DBcc/Scc, JSR/JMP/RTS, LINK/UNLK, ordinary MOVEM forms.
- Register shifts/rotates, bit operations, word MUL/DIV, ADDX/SUBX/NEGX and cumulative Z.

Exact NanoKernel exits include full indexed extensions, unsupported long/exceptional formats, memory shifts, BCD/bitfields, privileged/status/control changes, trace/exception services, A-line/F-line handling, FPU services, Mixed Mode and execution-return trampolines. Division by zero/overflow, model-sensitive MOVEM base-in-list cases and unsafe memory accesses also exit before native effects.

Three ROM peepholes receive explicit compound-form exits: MOVE.L absolute-short to -(A7) followed by RTS, MOVE.L (A7)+ to (A7) followed by RTS, and MOVEM followed by UNLK A6. These real handlers intentionally omit a dead stack store/update; replacing them with separate generic instructions changes RAM or the stack visible at the next canonical dispatch.

## Memory, faults and cache lifetime

`nw_68k_memory.cpp` resolves ordered effective-address changes in private state and journals RAM writes. Reads forward earlier staged bytes by physical address, including different guest addresses aliasing the same RAM and partial-byte overwrites. A failed later instruction discards the private block and retries its first instruction alone. The runtime uses the CPU's guest data MMU context for 68k operands/prefetch, distinguishes it from fetching PPC handlers, and accepts only plain readable/writable memory. It does not invoke devices or host callbacks during preparation.

Translation previews check permissions and may warm the TLB, but do not write guest PTE R/C bits. On a successful native commit, read references and store changes are recorded using ordinary translation. A failed operand, extension or successor prefetch leaves the original CPU/RAM starting boundary intact, then the real NanoKernel executes once. Consequently its existing ordered partial accesses, exception PC, restart behavior and MMIO effects remain authoritative; no invented fault trampoline or repeated MMIO transaction is used.

Odd multi-byte accesses delegate. Word-aligned long accesses are native, including a page crossing when both permitted physical mappings are contiguous. Both pages receive their appropriate PTE R/C updates on commit. Logical code overlap and physical code-page alias writes also delegate, preserving ROM prefetch/store ordering. Ordinary fall-through retains the already-prefetched opcode; taken branches/calls/returns fetch the target afresh, including self-branches and transfers into the original prefetch address. Dedicated stale-prefetch tests cover both behaviors. Each dispatch previews its MMU pages with separate read/write permission entries. Private preparation invokes no callbacks, context transitions or committed stores; those page previews are valid only within that dispatch. Every physical byte still passes memory-bank checks, crossing operands require both page mappings to be contiguous, and no host data pointer is cached. Writes to the active hashed page table delegate before effects so a staged PTE change cannot leave later previews using an old mapping.

The native cache has 16,384 entries and a bounded 8 MiB executable arena. Entries depend on physical instruction pages and current decoded words/context. Native page ownership is independent of PPC ownership: guest and host writes invalidate it even when the PPC JIT has no code on that page. Reset/all-cache operations invalidate it too. Mapping changes cannot reuse an embedded translation because there is none; an in-flight context change rejects its private result.

A separate atomic page bitmap avoids locking for unrelated stores. Page owners remove dependent entries. Execution/emission/eviction share a mutex; same-thread invalidation is deferred and rejects the private result. Function pointers never escape the execution call, and the arena cannot be recycled under an active frame. Apple JIT write protection is scoped and instruction-cache synchronization precedes publication.

An atomic code generation is captured before decoding and checked before native entry and after execution. Cross-thread invalidation announces the change before waiting for the execution mutex, so a running private result is rejected rather than committed from old code. A scalar retry retains the original generation. The live verifier treats a changed generation as inconclusive.

Decoded block reuse indexes existing compiled entries instead of maintaining a second lifetime. Every lookup re-probes the current physical code mapping, checks context, opcode and the NanoKernel's first prefetched word, and copies metadata under the cache mutex. Entries retain their existing physical-page ownership and eviction rules. Reuse is limited to one-page blocks; operand permissions, data mappings and successor prefetch still use the live MMU on each run. The index contains no executable or host operand pointers. A scalar retry preserves a live block hint when its first instruction, context and physical page still match, so data-dependent fallback does not permanently prevent later block reuse. `decoded_block_hits` records this optimization separately from emitted-code hits.

## Blocks and event delivery

Blocks contain at most eight instructions and remain within one instruction page. Register/immediate blocks, including a terminal conditional branch, retain D0–D7 in ARM64 W20–W27. Producer/consumer liveness omits dead interior flag calculations; all observable CCR and independent NanoKernel SO bookkeeping is materialized at exits. There is no deferred flag state crossing a guest interrupt or reference exit.

Mixed memory/control blocks retain ordered private state and a cumulative write journal. Direct BRA successors can be incorporated within the same bounded block. Decoded words, physical code-page ownership, execution context and successor prefetch are guarded; a changed successor returns after the completed prefix. There are no unbounded chains or escaped target pointers. Services, uncertain memory, context changes and the instruction budget end a native run.

Every completed block returns to the existing decrementer, pending-exception, idle and host-special-flag checks. The dispatcher does not acknowledge interrupts, change interrupt masks, or disable devices. Native PPC chaining stops at canonical boundaries for direct candidates; exact NanoKernel-only opcodes retain bounded PPC chaining. Verification stops at every canonical boundary. Opcode policy is computed once, independently of guest mappings; candidates still receive full decoding, ABI, operand and invalidation checks. Profiling records successful reference/service PPC chains without breaking them, so histogram selection does not change dispatch behavior. The candidate-only boundary optimization is undergoing new app qualification.

The memory path previews the live MMU translation/protection context, checks every physical byte's memory-bank permissions, re-probes a second page, and reuses that immediate resolution for journal keys. It caches no host operand pointer across a dispatch or callback.

## Verification and reproducibility

The live verifier prepares only plain-memory effects in private state, then lets the ordinary CPU loop execute the real ROM handler. It never calls a nested synthetic `execute()` or repeats host/device callbacks. A default four privately predicted checkpoints accommodate short ROM fusion; `NW_JIT68K_VERIFY_WINDOW=1..16` selects the bounded diagnostic window. Exception serial changes and reference continuations outside that window are reported as inconclusive, not passes. A comparable mismatch quarantines the opcode and retains at most 32 detailed traces.

The standalone harness compares actual emitted ARM64 execution with the decoded ROM's PPC handlers, interpreted by the independent PPC oracle. It includes CCR/ABI round trips, all overflow-bookkeeping combinations, arithmetic edge vectors, seeded opcode cases, memory/stack/control/MOVEM, late prefetch failures, code invalidation, deferred invalidation/reentry, arena recycling, scalar-versus-block results and mixed native/NanoKernel streams containing CCR and BCD services.

Commands (guest ROM/media are supplied locally, never bundled):

```sh
python3 tools/run_nw68_tests.py --rom /path/to/decoded-4MiB-ROM --release
python3 tools/run_nw68_tests.py --rom /path/to/decoded-4MiB-ROM --sanitize
python3 tools/run_nw68_tests.py --thread-sanitize
python3 tools/run_nw68_tests.py --rom /path/to/decoded-4MiB-ROM --release \
  --benchmark --policy-csv /tmp/nw68-policy.csv
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver.xcodeproj \
  -scheme SheepShaver-MMUTests -configuration Debug \
  -derivedDataPath /tmp/nw68-mmu CODE_SIGNING_ALLOWED=NO
/tmp/nw68-mmu/Build/Products/Debug/SheepShaver-MMUTests
python3 tools/qualify_nw68.py --app /path/to/Debug/SheepShaver.app \
  --prefs /path/to/guest.prefs --modes off,on,verify \
  --workload extfs --seconds 360 --interrupts 10 --work-dir /tmp/nw68-matrix
```

The qualification runner snapshots its seed media once, clones disk **and CD** images for every mode/repeat, uses positional `.sheepvm` bundles containing private `prefs`, `nvram` and `nvram.flash`, and creates a dedicated shared directory. It preserves HOME. Only its child VM is terminated. It retains screenshots, logs, traces, histograms and JSON results. ExtFS scripting expects an English 1024×768 Mac OS 9 desktop with the Unix icon in the usual right column; screenshots and an actual host-directory creation are required to establish that the workload ran. An IRQ injection request requires an `NW_BOOT_LOG=1` app. Both ExtFS and nested EXEC_RETURN boundaries can receive real VBL injections; completion requires guest IACK and EOI. Scripts wait for Finder before starting their workload clock, and logs must confirm the private flash path. Custom scripts must emit the completion heartbeat; a startup-only run is not treated as workload success.

Counters distinguish native instructions, reference exits, verifier comparisons, exit reasons, blocks and cache churn. PC histogram overflow is explicitly reported. Canonical NanoKernel dispatches can cover fused instructions, so comparing raw off/on dispatch counts is not a speed benchmark or an exact ordinary-instruction coverage denominator.

## Validation record and remaining gates

Final source validation on October 1:

- MMU/PPC/device suite: **2,221 passed, zero failed**, including preview-versus-real PTE R/C behavior and the current ARM64 host locks. Log: `/tmp/macemu-nw68-audited-mmu.log`.
- Core with decoded ROM and ASan/UBSan: **390,865 passed, zero failed**; **150,377 ROM cases compared**, 24,999 unsafe/reference-limited cases explicitly skipped. Log: `/tmp/macemu-nw68-locked-sanitize.log`. Skips are not passes; those cases retain exact NK execution.
- Release core with the complete opcode policy: **390,866 passed, zero failed**. Same 150,377 ROM comparisons and 24,999 explicit skips. Log: `/tmp/macemu-nw68-audited-release-tests.log`.
- Scalar-versus-block tests include every conditional branch condition, physical alias forwarding, overlapping writes, guarded BRA successors, late faults, stale prefetch, changed code, nesting/invalidation, arena recycling and mixed reference/native state.
- Debug and Release app builds succeed. The full-block diagnostic Debug product is under `/tmp/macemu-nw68-full-block-debug-dd/Build/Products/Debug/`; the current Release product is under `/tmp/macemu-nw68-audited-release-dd/Build/Products/Release/`.
- Isolated core register benchmark: 16,000,008 instructions, native 0.218033 s versus PPC-JIT NK 0.263692 s (1.209 ratio), identical final state. This is a harness measurement rather than an application speed claim.

A full guest-loop benchmark runs a known five-instruction arithmetic/conditional-branch loop at the next genuine EMUL_OP callback. It uses the normal CPU/MMU/event machinery, checks D0/D1/D7 after each sample, retains all callback state, and excludes unrelated IRQ code from its scoped native counter. Earlier three Release pairs, before the physical-alias fix, measured approximately 1.16–1.19 ratios and 99.997% native coverage of the known instruction denominator. Those runs used cloned disk/CD state but shared host NVRAM; they are preliminary evidence, not the final isolated production record.

The corrected-lock Release candidate was timed alone in three isolated reference/native pairs with per-instruction histograms disabled (`/tmp/g8/nw68-audited-release-timing/results.json`). Median arithmetic samples are 270,030/164,607 µs, 273,877/162,215 µs and 266,947/168,162 µs: speedups of 1.640, 1.688 and 1.587 with identical D0/D1/D7 and 99.998% native coverage of the known instruction denominator. Finder readiness takes 74.61/83.57 s, 74.56/83.59 s and 74.55/86.36 s. The native 12–16% readiness regression fails the mixed-workload performance gate; the common 30-second settling interval is included in both modes. Candidate-only boundaries are being measured separately rather than treated as an established improvement.

The old runner's `--config` assumption was incorrect on macOS: `LoadXPRAM()` still selected HOME's NVRAM. The runner now uses positional VM bundles, snapshots NVRAM once and checks the actual flash path. The normal-app startup guard was corrected to honor a supplied VM bundle instead of opening the config picker. Earlier qualification results cannot establish NVRAM isolation. Original user state has not been rolled back.

The final isolated lifecycle matrix passes in both reference and native modes: real guest PMU restart, second Finder boot, PMU shutdown, normal exit, mouse targets and heartbeat, with zero IRQ-stall diagnostics. Native's second boot/shutdown records 222,069,243 direct instructions out of 233,555,216 canonical counted instructions (95.1%). This denominator includes NK dispatch fusion and services; it is not an exact independent ordinary-integer census. Results: `/tmp/g8/nw68-clean-final-lifecycle/results.json`.

The repaired seed was produced by actual Disk First Aid completion and Finder shutdown. Scripts observe Finder, let startup tasks settle for 30 seconds, and check CurApName again near completion. CurApName can change during background tasks, so it is not required to remain constant during that interval. Actual file writes/playback establish that the workload ran.

The baseline shared-folder deadlock has been traced and corrected as described below. The current decoded-cache candidate is being requalified; complete timed soaks and physical native mouse capture/release remain rollout gates. Default enablement requires passing device/application, input/audio, restart/shutdown, coverage/performance and active/idle soak gates. A successful core test does not replace those gates.

The decoded-cache native device run records 431,381,451 native instructions, creates the host-share directory, completes all 20 ExtFS/EXEC_RETURN VBL injections, and retains Finder, mouse delivery and heartbeat with zero stuck interrupts (`/tmp/g8/nw68-decoded-events/results.json`). Reference also passes those operations. The verifier makes 242,567,500 comparable checks with zero mismatches, but its run fails: it opens the wrong Finder folder, never creates the share directory, and later logs an outer CPU execution-return from the ExtFS HFS callback at PPC `0x680ff3ac`. Video updates then stop. That result is a failure, not a verified device pass; return tracing and a foreground Finder fixture are being compared. The runner now rejects unexpected outer CPU returns explicitly.

The current paired movie fixture passes actual moving video and audio in both native and reference modes (`/tmp/g8/nw68-decoded-movie/results.json`). The native content crop changes 3,240 pixels between playback shots, compared with 3,144 in reference. Native input/output audio maxima are 43,008/44,544 frames per reporting interval; reference is 45,056/44,032. Reference has one recovered boot-time VBL delay of about 1.04 seconds, with a matching EOI and no unresolved service at completion. These are playback/liveness checks, not audio-latency or whole-application speed benchmarks.

The native Control Strip volume module displays a slider and dismisses normally (`/tmp/g8/nw68-decoded-input/on-0.sheepvm/controlstrip-menu.ppm`). Physical native capture/release was repeated after the Mac was unlocked: two `captured=true` grabs and two `ungrab reason=hotkey` events accompany the UI capture badge appearing/disappearing (`/tmp/g8/nw68-atomic-input/on-0.sheepvm/guest.log`). This confirms the capture/release transitions; synthetic native dragging is not a measurement of physical mouse delta delivery. Guest ADB target movement and Finder heartbeat also pass in that private VM.

The first atomic-flags lifecycle pair passed reference restart/shutdown but failed native's second-boot readiness, without an outer CPU return. Subsequent scalar native and block-native runs both pass real restart, second Finder boot and shutdown (`/tmp/g8/nw68-atomic-scalar-lifecycle/results.json`, `/tmp/g8/nw68-ready-lifecycle/results.json`). That intermittent failure is retained and requires repeated testing of the final host-lock revision; it is not erased by later passes.

Verifier screenshots show that CurApName may name Finder well before it draws the Unix volume. The fixed-time fixture then clicked blank desktop and opened an existing QuickTime folder; its missing share write did not establish a hung VM. The script now waits for one second of stable white at the known Unix disk stripe in this 1024×768 fixture before starting its workload clock. The runner retains second-boot screenshots as well. This is an observable test-readiness condition; it changes no guest boot or file-system behavior.

The revised readiness fixture passes the reference and ROM-verifier device runs, including the shared-folder write and all twenty injected interrupts. Native fails that matrix: its desktop icons disappear, it completes eighteen of twenty injections and never creates the share directory, while mouse delivery and VBL servicing continue. Results: `/tmp/g8/nw68-ready-events/results.json`. The final-lock native run with decoded reuse disabled also fails before Finder, with an empty startup alert and no injected interrupts (`/tmp/g8/nw68-locked-fresh-decode/results.json`). Neither failure establishes a decoded-cache defect, and both remain qualification failures.

Both failed logs report `SysError #27` at 68k PC `0x0015ccdc`. Apple's installed SDK `MacErrors.h` defines this as `dsFSErr`, file-system map corruption. An earlier reference lifecycle failure has the same error (`/tmp/g8/nw68-decoded-lifecycle/off-0.sheepvm/guest.log`), so native translation alone is not established as its cause. Qualification now rejects positive guest system errors and block mismatches explicitly. Diagnostic builds preserve registers and plain-memory code/stack/address-register pages at the error; those reads preview the MMU without setting PTE R/C bits or touching devices. The corruption source remains under investigation.

With the corrected host locks, native block comparison passes 548,441,465 instructions when restricted to register blocks (`/tmp/g8/nw68-locked-block-check/results.json`). Expanded comparison of mixed blocks captures the original physical RAM reads, replays scalar instructions privately, overlays their staged writes and compares complete state, successor prefetch and final physical-byte write effects before commit. It repeats no device access or host callback. That run passes 336,445,522 native instructions, the actual shared-folder write, all twenty injected interrupts and Finder/mouse heartbeat with zero comparison mismatches or unresolved IRQ stalls (`/tmp/g8/nw68-full-block-check/results.json`). This comparison checks block optimization against scalar native execution; the separate verifier checks scalar execution against the real NanoKernel. These successful runs do not erase the intermittent native failures above.

## CPU special-flag synchronization

Qualification exposed unexpected outer CPU-loop returns at both an ExtFS callback (`0x680ff3ac`) and a reboot-time PPC instruction (`0xffd83a0c`), without an outer EXEC_RETURN request. ARM64's Unix `sysdeps.h` has no test-and-set implementation, so the old special-flag setters used no-op spin locks. A timer interrupt's ordinary read/modify/write could overwrite a nested return's flag clear, restoring EXEC_RETURN or losing an interrupt update. ThreadSanitizer confirms concurrent conflicting writes in `basic_spcflags::set/clear`; this demonstrates the data race, while the attribution of individual historical freezes remains an inference.

Special-flag reads, stores and bit updates now use atomic acquire/release operations. Copy construction/assignment also use atomic access, and the existing register layout is retained. ARM64's general Unix spin lock now also uses an acquire atomic exchange and release store, protecting the legacy interrupt-update helpers that use it. This fixes synchronization at its source without discarding execution-return flags or synthesizing interrupts. Every standalone test run includes 100,000 concurrent return-clear/interrupt-set updates and 200,000 lock-protected increments; `--thread-sanitize` runs those regressions alone under ThreadSanitizer. The original special flags report a data race (`/tmp/nw68-flags-race-tsan.log`), and the original host lock reports a protected-counter race (`/tmp/nw68-original-lock-tsan.log`). Both corrected regressions pass with no resurrected return, lost interrupt, missed protected increment or sanitizer report (`/tmp/macemu-nw68-atomic-locks-tsan.log`). Full guest qualification of this revision is in progress.

## Component registration correction

The reference Finder wait was an asynchronous File Manager read (`_ReadAsync`, `ioResult=1`) called from RAVE component registration inside an active ExtFS callback. Registering a resource-backed component from arbitrary EMUL_OP entry prevented the outstanding callback from returning and completing that read. Continued VBL IACK/EOI and mouse motion did not imply that Finder was progressing. Reentry could also begin a second registration.

Registration now runs from temporary head patches of WaitNextEvent/GetNextEvent, preserving D0–D7/A0–A6 and tail-jumping to the previous trap handler. A central busy guard prevents recursive registration. Each event call registers one component; after completion, the hooks are retired through the Trap Manager. Cleanup compares the installed address returned by GetToolTrapAddress, including a possible PPC routine descriptor, preserves newer third-party patches, and replaces our callback with NOP so later daisy-chain calls are harmless.

All sound, Cinepak/SVQ1 and RAVE components remain enabled. The corrected reference and native runs complete all four registrations and perform actual host-share writes. No I/O completion is fabricated and no codec or renderer is disabled. Paired QuickTime runs show changing Cinepak video frames and nonzero input/output audio frames (`/tmp/g8/nw68-qualified-movie-center/results.json`). Those results precede decoded block reuse, so playback is being repeated for the final candidate.

## Interrupt-return correction

The persistent baseline VBL freeze was reproduced without native translation. An interrupt returning directly to the host's 68k EXEC_RETURN trampoline at `0x680ff208` bypassed NK slow dispatch; unwinding the nested frame restored the caller's old CR and lost its outstanding wake/service state.

EXEC_RETURN now routes a pending NK CR2.LT request through the real `0x6806d114` slow dispatch with the trampoline as the retry continuation. The guest decides masking, performs its own IACK/EOI and then returns. Nested Execute68k retains the original synthesized supervisor-entry contract and retains the existing EMUL_OP return mode and restores the caller's saved CR. Scripted timing is invoked while that callback mode is still active, before normal MODE_68K reentry. A broader experiment treating CR2 wake bits as CPU-global regressed boot and Finder callbacks in isolated runs, and was removed. Those bits belong to the NK execution context and cannot be copied indiscriminately into a new host-created frame.

The deterministic unfixed reference injection retained VBL acknowledgement 4605 indefinitely, stopped mouse delivery and failed its heartbeat (`/tmp/g8/nw68-return-unfixed/off-0/guest.log`). The fixed run completed ten injected EXEC_RETURN interrupts, shared-folder creation, mouse targets and heartbeat (`/tmp/g8/nw68-return-fixed/on-0/guest.log`). These runs established the return-path mechanism; the corrected final bundle matrix additionally tests the full current source with isolated NVRAM.

IACK/EOI diagnostics include acknowledgement serials and EOI age. Qualification distinguishes recovered delays from an IRQ still in service at the end, and preserves maximum service latency. There are no forced EOIs, synthetic success callbacks, interrupt-mask changes, disabled devices or watchdog recovery in this change.

## October 2 dependency checks

The candidate-only dispatch revision did not establish a mixed-boot gain: its controlled Release pair took 76.31/86.32 seconds (reference/native), while the arithmetic median improved from 274,083 to 164,564 microseconds (1.666×). A subsequent native run reproduced SysError 27 with a fault-time register and memory dump (`/tmp/g8/nw68-service-fs-events/on-1.sheepvm/nk-state.txt.syserr-1`). A2 identifies the mounted Mac OS 9.2.1 CD VCB; the stack returns through the File Manager fatal-error hook at 0x0015cd06. This identifies the failing subsystem, not the corrupting instruction. The PPC-interpreter/native-68k file workload passes (`/tmp/g8/nw68-service-ppc-off/results.json`). Its interrupt-injection settings differ, so it narrows the investigation without proving PPC causation.

The PPC verifier repeats the five known startup mismatches documented in `NW-PPC-JIT-REPORT.md`. It remains unsuitable for production qualification because compiled execution and reference execution share mutable machine state. That diagnostic was stopped at its stuck bootstrap; its result is a failure, not a certificate.

A precise PPC dependency defect has been corrected: D-form byte/word loads now check the fault before committing the result or updated base, preserving the effective address separately. Generated-code regressions fault lwz/lwzu/lbz/lbzu at each block position, checking prefix completion, untouched RD/RA, fault PC/address and suppressed suffix execution. The MMU/device/PPC target passes 2,305 checks with zero failures (`/tmp/macemu-nw68-fault-mmu.log`). This is not yet established as the filesystem-error fix. The native dispatcher also deduplicates successful read-reference materialization by logical page while continuing to preview translation/protection on every access. New controlled Release timing and guest workload checks are in progress.

SysError diagnostics now retain the last 128 A-line trap entry records along with fault-time registers/pages. Qualification rejects script parsing errors, positive SysError codes, native block mismatches and PPC VERIFY misses. An earlier invalid diagnostic fixture and a repeat stopped at a blank startup alert remain failed/excluded evidence; they are not passed device runs.

The page-reference-only Release pair passes exact arithmetic state and records 259,278/142,900 microsecond medians (1.814×), with 75.29/85.34-second readiness (`/tmp/g8/nw68-page-fault-release-timing/results.json`). Later redundant repeats were interrupted before results were written to qualify the next revision; only that completed pair is evidence. Dispatch-local permission-separated page previews and the hash-table-write guard subsequently pass 390,900 core checks under ASan/UBSan, including 150,377 ROM comparisons (`/tmp/macemu-nw68-preview-core-sanitize.log`). Debug and Release app builds succeed. Paired timings and repeated file workloads for that revision are in progress; no default rollout or soak gate has been claimed.

The first dispatch-preview Release pair passes boot, arithmetic, Finder/ADB and error checks (`/tmp/g8/nw68-preview-timing/results.json`). Arithmetic medians are 266,756/133,993 microseconds (1.991×) with exact state and 99.9985% native coverage of the known loop. Readiness is 85.32/76.28 seconds (reference/native). The reference boot differs from prior measurements; one pair is encouraging but does not establish repeatable mixed-workload improvement. The current diagnostic file repeats run without forced IRQs to match the prior PPC-interpreter comparison settings.

Both subsequent native/PPC-JIT-on file workloads pass from separate private seeds with no forced interrupts (`/tmp/g8/nw68-preview-file/results.json`): readiness 113.44/112.42 seconds, actual share-directory creation, Finder heartbeat, 18 mouse targets each, no positive SysError, outer CPU return, unresolved IRQ, mouse failure or script parse error. These are Debug correctness/liveness runs. They strengthen the evidence for the precise-fault/current-preview revision, but do not isolate which change removed the intermittent failure or certify the final injected-IRQ/soak gates.

`tools/finish_nw68_qualification.py` now runs the remaining gates sequentially and stops at the first failure. It retains atomic stage status and each child's manifests/hashes/logs, checks three paired Release arithmetic speedups and at least 90% native loop coverage, and rejects a median boot regression beyond max(2 seconds, 5%). Subsequent stages exercise reference/native/NK-verifier file operations with 20 injected IRQs and full native-block comparisons, real restart/shutdown, changing QuickTime video and nonzero audio, two hours active ExtFS work, and eight hours idle followed by mouse wakeup. Native coverage here is explicitly the benchmark loop, not an independent legal-integer census of all guest software. Physical mouse qualification still needs review separately.

The sequence completed under process-scoped `caffeinate -i`, allowing display sleep while keeping the host awake for the required durations. Status: `/tmp/g8/nw68-final-oct2/status.json`; driver log: `/tmp/g8/nw68-final-oct2.out`. No original guest media or user NVRAM was modified. All six automated stages passed, and the default preference has not been enabled.

## October 2 completed automated qualification

The final sequential run has passed repeated Release timings, injected-event/reference/native/NK-verifier checks, restart/shutdown, QuickTime movie/audio, and the two-hour active workload. Three Release arithmetic speedups are 2.005×, 2.046× and 1.979×; minimum known-loop native coverage is 99.9984%, with median reference/native readiness 94.37/71.26 seconds. This measures the scoped integer loop and fixture boot, not a whole-application 2× speedup.

Each event mode performs its actual share write and completes all 20 injected interrupts. Native records 345,683,997 instructions with zero block mismatches; the real-NanoKernel verifier records 303,639,140 comparable checks with zero mismatches. Reference/native lifecycle runs both perform real restart, second boot and shutdown. Movie crops change by 3,064/3,058 pixels, with nonzero input/output audio in both modes. The active soak completes 7,200 workload seconds and 5,232,682,937 native instructions, with share write, Finder/mouse heartbeat and no guest system error, unexpected CPU return or unresolved interrupt.

The final idle stage completed all 28,800 workload seconds and 10,736,858,113 native instructions. The end-of-soak scripted mouse wakeup passed all three targets, with final Finder and heartbeat confirmation. It recorded no positive SysError, native block mismatch, unexpected CPU return or mouse-delivery failure. One interrupt-delay warning recovered; maximum VBL latency was 363,784 microseconds and unresolved interrupt stalls were zero. The runner reports `complete: true` and all six stages passed in `/tmp/g8/nw68-final-oct2/status.json`.

Scripted ADB mouse delivery proves guest responsiveness after the idle soak; it does not establish physical host cursor capture, relative-motion delivery or hotkey release. At automated gate completion, the default preference was still false.

## October 2 default enablement

At the user's request, `AddPrefsDefaults()` now enables `jit68k_host`, and the macOS settings editor defaults the **Host 68k JIT** toggle to on. Existing explicit off settings and the diagnostic environment override remain effective. The original `/tmp/prefs-1024` config has no `jit68k_host` entry, so it inherits the new default on its next launch.

The normal Debug app was rebuilt successfully at `/Users/bcavalieri/Library/Developer/Xcode/DerivedData/SheepShaver-enocqjdeiirbxxhgydyjglccdajj/Build/Products/Debug/SheepShaver.app`; the Release rebuild also succeeds at `/tmp/macemu-nw68-preview-release-dd/Build/Products/Release/SheepShaver.app`. A temporary check using the actual preferences implementation confirms default-on initialization, explicit preference-file disablement and command-line disablement. Physical host input qualification is not claimed by this preference change.
