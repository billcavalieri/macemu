# ROM milling brief

Operator reference for SheepShaver Mac OS 9.2.1 New World ROM-class hang mills.

Live inventory: 4 Sep 2026, MacBook-Pro-2. `research-core` does not exist. The real tree is `research-score`. `newworldview` and `~/Downloads/Mac OS ROM` are on disk. `rom_disasm.py` decoded the prefs ROM to 4 MiB and confirmed NewWorld and NanoKernel landmarks.

## Path correction

`~/Documents/GitHub/macemu/research-core` is missing. `~/Documents/GitHub/macemu/research-score` exists (the only `research-*` dir under macemu). Mill helpers, `g3_driver`, and `rom_disasm.py` all live under `research-score`.

Also next to macemu: `~/Documents/GitHub/tome-extract` (installer tome to `Mac OS ROM` extractor; not a mill).

No standalone Ghidra `.gpr` / `.rep` project under Documents (maxdepth 3). Ghidra work goes through NewWorldView Export Ghidra (`analysis.json` / `analysis.xml` / `LoadNewWorldROM.py`), not a checked-in project.

## ROM file facts (live)

| Fact | Value |
| --- | --- |
| Path | `/Users/bcavalieri/Downloads/Mac OS ROM` |
| Size | **2552522** bytes (CHRP wrapper / tbxi data fork, not 4 MiB unpacked) |
| SHA-256 | `cd34814e32d35549dee1153acf0d1ccb6f24ac7afefc993f699483322e6fa871` |
| Magic / header | Starts with `<CHRP-BOOT>` then `<COMPATIBLE>MacRISC</COMPATIBLE>` (xxd: `3c434852502d424f4f54...`) |
| `file(1)` | `data` |
| Class | New World CHRP bootscript image (MacRISC); not Old World raw 4 MiB |
| Decode tools that work | `research-score/g3_driver/rom_disasm.py` (decoded to 4 MiB); `NewWorldViewCLI decode-rom` (binary under `.build/.../debug/`) |
| After decode | 4 MiB at ROMBase `0x50000000`; string `NewWorld` at +`0x30D064`; NK first word at +`0x310000` = `4800000c` |
| Ghidra load | Unpacked 4 MiB after decode, **not** the CHRP wrapper |
| Ghidra project | No `.gpr` found; use NewWorldView Export Ghidra onto a locally mapped 4 MiB image |

## Ghidra load recipe

From `~/Documents/GitHub/newworldview`:

```bash
swift run NewWorldViewCLI decode-rom ~/Downloads/Mac\ OS\ ROM ./MacROM.bin
swift run NewWorldViewCLI export ~/Downloads/Mac\ OS\ ROM ./out
```

`export` writes `NewWorldView-ghidra/` plus `LoadNewWorldROM.py` (analysis overlay; ROM bytes stay local). In Ghidra:

1. Load the unpacked 4 MiB `MacROM.bin` at `0x50000000`.
2. Optionally map 68k Toolbox at `0x00000000`.
3. Run `LoadNewWorldROM.py`.

## Mill handoff into the driver

From `~/Documents/GitHub/newworldview` (CLI has no Apple FM):

```bash
swift run NewWorldViewCLI analyze-logs ~/Documents/GitHub/macemu/research-score --histogram-limit 0
swift run NewWorldViewCLI export-histogram ~/Documents/GitHub/macemu/research-score -o /tmp/nw-pipeline --histogram-limit 0
swift run NewWorldViewCLI export-pipeline ~/Documents/GitHub/macemu/research-score -o /tmp/nw-pipeline
```

Then in `research-score/g3_driver`:

```bash
export G3_HISTOGRAM=/tmp/nw-pipeline/mill-histogram.json
export G3_ANNOTATIONS=/path/to/mill-annotations.json   # after approve in app
./run
```

## macemu layout

Root: `~/Documents/GitHub/macemu`.

| Path | Role |
| --- | --- |
| `research-score/g3_driver/README.md` | Mill-and-test loop: classify hang, canned mill, hang-cap, KEEP/REVERT; NewWorldView annotation/histogram handoff |
| `research-score/g3_driver/taxonomy.json` | refuse_as_wait, denylist SHAs, refuse examples |
| `research-score/g3_driver/classify.py` | Decodes (op, nxt) into hang classes |
| `research-score/g3_driver/mill_apply.py` | Applies mills; HARD_SKIP_OFFS; r24 map/spin histogram; optional ROM +2 walk |
| `research-score/g3_driver/mill_escalate.py` | Class-mill prompt for NEW leftover |
| `research-score/g3_driver/mill_annotations.py` | Loads NewWorldView `mill-annotations.json` |
| `research-score/g3_driver/mill_histogram.py` | Loads NewWorldView `mill-histogram.json` |
| `research-score/g3_driver/mill_pack.py` | Escalation pack for Grok Build |
| `research-score/g3_driver/rom_disasm.py` | Decode prefs ROM; A-line names; PPC/68k disasm; HARD/NK sites |
| `research-score/g3_driver/g3_driver.py` / `run` | One-command mill loop |
| `research-score/g3_driver/state.json` | Resume state (live) |
| `OS921-BOOT-PLAN.md` | New World boot / NK v2 / MMU / device-tree plan |
| `SheepShaver/src/MacOSX/OS921-WP0-ROM.md` | How to obtain and verify New World Mac OS ROM |
| `BasiliskII/`, `cxmon/`, `SheepShaver/` | Emulator trees |

## newworldview layout

Root: `~/Documents/GitHub/newworldview` (Swift package + App). `scripts/` exists but is empty. Debug CLI:

`~/Documents/GitHub/newworldview/.build/arm64-apple-macosx/debug/NewWorldViewCLI`

| Area | Live contents |
| --- | --- |
| README | Read-only NewWorld ROM viewer; Capstone disasm; analysis DB; Export Ghidra; mill log histogram and annotations into the macemu pipeline |
| `Sources/NewWorldROM/` | Parser + mill + Ghidra library (see below) |
| `Sources/NewWorldViewCLI/main.swift` | Headless CLI: export, decode-rom, analyze-logs, export-histogram/pipeline, build-grok-pack, lookup, compare-disasm |
| `App/NewWorldView/` | GUI: Mill logs, Annotations, histogram charts, research settings |
| `Tests/NewWorldROMTests/` | `MillAnalysisTests.swift`, `MillResearchTests.swift`, plus parser/LZSS/bootinfo tests |
| Capstone | SPM dep `libcapstone-spm` (PPC + 68k) |

Key mill / Ghidra sources under `Sources/NewWorldROM/`:

| File | Capability |
| --- | --- |
| `GhidraExport.swift` | Writes `analysis.json`, `analysis.xml`, `LoadNewWorldROM.py` (no ROM bytes); mill tags; trap filters (all / millCritical / noSkipUI); map MacROM at `0x50000000` |
| `ATrapTable.swift` | Classic Mac A-line / toolbox trap names; `noSkipUITraps` aligned with mill policy |
| `DisassemblyService.swift` | Capstone PPC / 68k disassembly for identified code regions |
| `MillSkip68kPolicy.swift` | Skip-68k millability aligned with `mill_apply`; HARD offs `0x3264FC`/`0x326564`/`0x326568`; off68k `0x366084`; spin `0x26E88`; UI `0x5C86C...0x5C8BF`; NK band; emulator JT block |
| `MillHistogramExport.swift` / `MillAnnotationExport.swift` / `MillPipelineExport.swift` | `mill-histogram.json`, `mill-annotations.json`, `mill-pipeline.json` for g3_driver |
| `MillLogParser.swift` / `MillLogAggregator.swift` / `MillLogDiscovery.swift` | Parse / aggregate KEEP hang logs under research-score |
| `MillResearchEngine.swift` / `MillReachabilityEngine.swift` / `MillSafetyVerifier.swift` | Research triage, reachability, safety checks |
| `MillEscalationPackBuilder.swift` / `GrokPackGenerator.swift` | `pack-escalation.md` / `grok-prompt.md` for ambiguous sites |
| `MacROMImageDecoder.swift` / `LZSSDecoder.swift` / `ParcelParser.swift` / `BootInfoParser.swift` | CHRP / lzss / parcels decode to 4 MiB MacROM |
| `AnalysisEngine.swift` / `AddressTranslation.swift` | Functions, xrefs, SheepShaver map at `0x50000000` |

## What each tree extracts

### research-score / g3_driver (live)

- Decodes CHRP lzss or parcels/prcl to a 4 MiB image (`ROM_SIZE=0x400000`, `ROMBase=0x50000000`).
- Checks NewWorld magic at off `0x30D064`; NK region at `0x310000`.
- Emits A-line trap names, 68k disasm at mill offs, PPC disasm around HARD/NK sites.
- Tags regions (68k JT, SANE, SCSI/VIA, ui-dialog, nk-50326-wait).
- stdout report only; never copies ROM into git.
- A-line traps in rom_disasm; refuse/HARD offs in taxonomy and HARD_SKIP_OFFS.
- NK hang cluster 50325/50326 (ROM offs `0x325xxx`/`0x326xxx`); NK about ROMBase+`0x310000`.
- HotInts DataStorageInt cited at ROM +`0x3132a0` in boot notes.
- 68k offs: r24 map/spin to r24 minus `0x50000000`; KEEP hang `pc=50366084` to off `0x366084`.

Live `rom_disasm.py` this inventory: loaded **4194304** bytes from prefs; NewWorld +`0x30d064`: yes; NK +`0x310000` first word `4800000c`; 68k interp hang PPC +`0x366084` present.

### newworldview (live)

Read-only macOS app + `NewWorldROM` library + `NewWorldViewCLI`. Parses CHRP bootscript, Trampoline ELF, Toolbox parcels, PowerPC ROM (ConfigInfo / NanoKernel / emulator), and embedded 68k SuperMario. Builds analysis DB (functions, A-traps, xrefs). Export Ghidra for symbol overlay without shipping ROM bytes. Mill path: analyze hang logs, then histogram, then Apple FM classify in the app / approve annotations, then export pipeline JSON consumed by `g3_driver`.

### tome-extract (adjacent)

Unwraps installer tomes (MacBinary / NDIF / Tome) to `Mac OS ROM` (tbxi). Does not decode CHRP to 4 MiB MacROM. Use `rom_disasm` or `NewWorldViewCLI decode-rom` for that.

## Mill-relevant PCs / opcodes

| Site | Shape | Handling |
| --- | --- | --- |
| 50326xxx cluster | post-leave hang before 68k | skip-hang only until 68k reached; after mill-22/35 do not keep milling 50326 |
| pc=503256f4 op=2c9e0000 nxt=3bc00000 | cmp+li | false-cmp-li; refuse-as-wait |
| pc=503264fc op=4082fff0 | backward bne | false-back-bc; HARD 0x3264FC |
| pc=50326564 op=900107d4 nxt=7c0604a6 | stw+mfsr | false-stw-spr; HARD; never wait-cmp; denylist e298371e |
| pc=50366084 / off 0x366084 | 68k interpreter hang | KEEP after mill-22/35; leftover is skip-68k |
| r24 map/spin (e.g. 50026e88 to 0x26e88) | 68k PC histogram | Prefer over +2; skip loop ops 60ff/4efa/4e75 |

HotInts DataStorageInt (DR-on lwz at SRR0) is an MMU accept, not a 50326 mill target.

## Hang-class mill flags

Prefer class flags over a sequential ROM +2 walk. Classify last heartbeat as `(op, nxt)`, then mill that class.

| Class | Meaning (operator view) |
| --- | --- |
| `wait-cmp-fwd-bc` | Compare then forward branch; spin / wait shape |
| `false-cmp-li` | Compare against immediate that never matches under current state |
| `false-back-bc` | Backward branch that never exits under current state |
| `false-stw-spr` | Store-to-SPR that does not take effect as expected |
| `dsi-on-store` | DSI raised on a store; check MMU / BAT / PTE path |
| `empty-vector 0x300` | Vector page empty / illegal at `pc=00000300` |
| `msr-collapse` | MSR bits collapsed so expected translate / interrupt mode is gone |
| `skip-68k leftover` | 68k path leftover after a skip; trap table needed |

NK vector page: IP=0 then `0x300`; IP=1 then `0xFFF00300`. Empty illegal `pc=00000300` means the vector is not mapped (MMU problem), not a 68k mill. Fix mapping first.

Live DSI accept: HotInts `DataStorageInt` when DR on, `lwz` at SRR0, no second DSI. Map those PCs to named handlers, not anonymous offs.

68k A-line / trap mills need a trap table keyed by opcode (or small family), fed by hang-log histograms, not sequential ROM offs.

## Class-level mills vs +2 ROM walks

1. classify + taxonomy: one (op, nxt) to a class; refuse list blocks false waits cluster-wide.
2. next_skip_68k_off: unique offs from KEEP map/spin histograms first; approved NewWorldView annotations first when `G3_ANNOTATIONS` / `G3_HISTOGRAM` set; +2 walk only afterward (disable with `G3_68K_WALK=0`). When histogram is empty, one NEW class mill via mill_escalate.
3. Session cost: about 2781 cycles, about 2737 per-offset skip-68k, about 51s mean hang-cap. Walk is weeks; histogram then class mill is hours.
4. rom_disasm A-line + region tags: trap/opcode identity beats anonymous halfwords. JT `0x350000`-`0x400000` must not divert r24.
5. NanoKernel symbols map NK PCs to handlers instead of milling every DEC-leave PC.

Safer unit: class or trap opcode, fed by histograms, not ROM+2.

| Prefer | Avoid |
| --- | --- |
| Class mills from `(op, nxt)` / trap histograms | ROM +2 walks from hang PC |
| NanoKernel named handlers (HotInts) | Anonymous offs in NK vector page |
| Trap table keyed by opcode | Sequential 68k ROM offs |
| Fix empty `0x300` as MMU map | Treating empty vector as 68k mill |
| One helper per new hang class | One helper per new PC |
| NewWorldView approved annotations + histogram | Blind +2 from leftover PC |

## Public / known tooling

| Project | What it gives mills | Limits for SheepShaver 9.2.1 |
| --- | --- | --- |
| [elliotnunn/NanoKernel](https://github.com/elliotnunn/NanoKernel) | RE of PPC NanoKernel asm through v2.28. `HotInts.s` defines `DataStorageInt`, `DecrementerInt`, `AlignmentInt`. Map hang PC into named NK handlers. | Reference source for labels, not a SheepShaver runtime. |
| [elliotnunn/newworld-rom](https://github.com/elliotnunn/newworld-rom) / tbxi | Parcels / TBXI and PEF patch tooling; ROM identity and layout. Cousin of local newworldview. | ROM identity work, not a SheepShaver mill. |
| NewSheep (Hawkins) | Invite-only GitHub pre-release. Reached 9.2.1 Finder with ARM64 JIT. Useful reference invariants: NK v2, just-enough HW. | Not a public clone; design reference only. |
| [kanjitalk755/cebix](https://github.com/kanjitalk755/cebix) SheepShaver | Public SheepShaver lineage. | 9.0.4 ceiling; no NK v2 MMU path for 9.2.1 New World. |
| [rcarmo/macemu-jit](https://github.com/rcarmo/macemu-jit) | ARM64 JIT donor; Welcome / desktop path. | Not 9.2 New World; JIT ideas only. |
| QEMU mac99 | Full 9.2 install and run on emulated Mac hardware. | Comparison / golden behavior; not a mill target. |
| DingusPPC | Accurate Old World / beige G3 model. | 9.2 path experimental; Old World bias. |

Use NanoKernel labels and hang-class flags as the primary mill map. Other trees: ROM layout, JIT ideas, or behavior comparison, not drop-in mills for SheepShaver 9.2.1.

## Recommended analysis workflow

1. Open ROM in NewWorldView (or CLI `export` / `decode-rom`). Confirm CHRP identity matches the live hash above.
2. Load unpacked 4 MiB in Ghidra after decode (`NewWorldViewCLI decode-rom` or rom_disasm output), not the CHRP wrapper. Map at `0x50000000`. Run Export Ghidra / `LoadNewWorldROM.py` for symbols (no ROM bytes in export).
3. Run `rom_disasm.py` against the prefs ROM (no SheepShaver): confirm NewWorld @0x30D064, NK @0x310000, dump HARD/50326 PPC and next histogram 68k sites. (Verified this inventory.)
4. Overlay elliotnunn NanoKernel symbols (HotInts) onto NK @ROMBase+0x310000 so hang PCs resolve to `DataStorageInt`, `DecrementerInt`, `AlignmentInt`.
5. Default `G3_68K_WALK=0`. Exhaust r24 histogram and NewWorldView annotations only. When empty, one mill_escalate NEW-class card (68k trap shape).
6. Build A-line / opcode histogram from KEEP logs keyed by trap; extend taxonomy / escalate, not per-off mills. Prefer `NewWorldViewCLI analyze-logs` / Export macemu pipeline.
7. Map parcel PEFs + trampoline to device-tree expectations (boot/Name Registry, not skip-68k) via NewWorldView tree view or tbxi dump.

Operators run hang capture and mill flags. Host-side owns Ghidra overlays (via NewWorldView export), symbol overlays, and helpers in `research-score`.

## Open gaps (live session)

| Gap | Notes |
| --- | --- |
| No `mill-annotations.json` / `mill-histogram.json` beside `g3_driver` yet | Export from NewWorldView (Analyze logs / Export macemu pipeline) then set `G3_ANNOTATIONS` / `G3_HISTOGRAM` |
| `newworldview/scripts/` empty | Use App Export Ghidra / CLI; no checked-in helper scripts |
| No `.gpr` Ghidra project on disk | Export tooling only; load locally decoded 4 MiB at `0x50000000` |
| Driver tip still around `e25a61f1` | Leftover skip-68k + Launch/LoadSeg mill in tree; prefer stay-in-CODE over +2 |
| NewWorldView `ATrapTable.swift` | Still names `A97C` GetCCursor / `A97D` GetNewDialog (swapped vs Traps.h). Do not feed that table into mill_apply. |

## Head start (6 Sep 2026) — do not mill into the live skip-68k hang-cap

Findings only. No ROM/toast bytes in git. **Do not restart skip-68k** — mill Launch is now Mac OS Install CFM, not CODE 66.

### Toast: mill was Modem Scripts Installer 4.x, not Mac OS Install

`g3_rf_off=112182784` is `CD Extras/Additional Modem Scripts/Installer` (Apple Installer 4.0.6, CODE 0–10+66, DLOG 510 435×288 Continue…). **Mac OS Install** is CFM `cfrg`/`pwpc` fragment **Upgrader** (vers 1.2.7), rsrc at `g3_rf_off=74305024` map 55022/1882, DLOG 510 Splash 354×266, **no CODE**. `Install Mac OS 9.2.1` is the package (`flrf`/`pffn`), not the engine.

### Toast CODE 66 (Installer 4.x extra, not the OS installer)

KEEP already entered `LoadSeg A9F0 enter seg=66 r24=1007d45e`. CODE 66 is **68k MixedMode glue**, not the installer window:

- Nested `LoadSeg 1` thunk, then `GetOSTrapAddress` `$A1AD` / `GetToolTrapAddress` `$A346` / `GetTrapAddress` `$A146`
- Gestalt `'sysv'` (`0x73797376`) and CMP.W against ROM versions `$75` / `$276` / `$178` / `$37A` / `$67C`
- Then more 68k

CODE 1 starts `$A89F` then does **not** look like classic 68k (likely PPC/compressed). Do not set `r24` at CODE 1+0 and expect a dialog.

### ROM `0x9440` (post-CODE hang)

NewWorldView `build-context` 68k (not the PPC `compare-disasm` at the same off):

```
move.w d1,$12(a2) / bra $9464 / … / move.l a0,$34(a2) / movea.l (a7)+,a1 / rts
```

KEEP spin: `JMP (A0)` at `0x94c2` with `A0=0x9440`, `D0=0x35`. Static xrefs: **none** (register dest). No `LEA/JSR $9440`. Next mill if G3 fails: **stay in CODE 66; don’t fall into this A0-dispatch helper.** Do not skip-68k `0x9440` as data.

### Toolbox trap *call sites* in ROM (not a vector table)

New World has no useful dumped A-line vector at these offs. Opcode search (even hits) is callers, not implementations:

| Trap | Name | Sample ROM offs (callers) |
| --- | --- | --- |
| `$A9F2` | Launch | `0x2c18e` (`PEA $02E0` / CLR / `_Launch` / `MOVEQ #26` / SysError) — same grok-era site `0x2c186` |
| `$A9F0` | LoadSeg | `0x527e`, `0x2bfaa`, plus high ROM copies |
| `$A97C` | GetNewDialog | `0x5c86c` (overlay), `0x10766` (same MOVE.L (SP)+,A4 / BEQ shape) |
| `$A97D` | NewDialog | `0x62c10`, `0x62d44` (then SetPort / GetCIcon) |
| `$A8A1` | FrameRect | many, e.g. `0x4bc0` (QD demo/frame sequence) |

Un-hosting `A97C` still needs the **Toolbox implementation**, not these callers. Implementations are A-line dispatch inside the 68k emulator, not a second copy at the call site.

### NewWorldView

CLI works: `lookup`, `build-context`, `compare-disasm`, `export-histogram`. Smoke histogram wrote `/tmp/nw-hist-smoke/mill-histogram.json` (20 entries). **Not** copied into `g3_driver/`. Full `analyze-logs` / Export pipeline still needed for `mill-annotations.json`. `compare-disasm` at 68k offs is PPC and misleading; use `build-context` for 68k.

Ghidra: still no `.gpr`. Next: `NewWorldViewCLI decode-rom` then `export` to a **local** dir, load 4 MiB at `0x50000000`. Do not commit `MacROM.bin`.
