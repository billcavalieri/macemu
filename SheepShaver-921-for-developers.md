# SheepShaver and Mac OS 9.2.1

The goal is a New World Mac OS 9.2.1 installer, then Finder, on SheepShaver. The build is Xcode on Apple Silicon.

Counts below are from the mill-and-test driver on 2 Sep 2026: **4403** completed mill cycles (`n=4404` pending hang-cap). The live KEEP is PowerPC `pc=50366084` after the guest reached 68k ROM. That is not the installer window.

## SheepShaver is not QEMU

SheepShaver is a user-mode Macintosh emulator. It is one host process: a PowerPC CPU core, host-backed disks and video, and a real `Mac OS ROM` file. It runs the NanoKernel and Mac OS against a reconstructed memory map. It does not model a whole Power Mac.

QEMU is a system emulator. It models a machine (Open Firmware, PCI, devices) and boots the guest from firmware. The product is that machine. The QEMU PowerPC wiki lists Mac OS 9.0, 9.1, and 9.2 as boot, install, and run on `-M mac99`. UTM wraps the same board. That path is slower than SheepShaver, and it does not reuse SheepShaver ROM patches or Toolbox glue.

This project is SheepShaver on Apple Silicon: ROM in-process, a real PPC32 MMU, and an ARM64 JIT. It is not a QEMU board. Basilisk II is 68k Classic. That is a different program.

## Why 9.0.4 was the ceiling

Classic SheepShaver is an Old World guest. It uses a patched 4 MiB Old World ROM (`rom_patches.cpp`, `rsrc_patches.cpp`). It uses `emul_op` traps and thunks instead of a full I/O device model. Guest RAM is a flat host mapping. The CPU largely ignores PowerPC virtual memory.

Mac OS 9.0.4 is the last release that boots in that model.

9.2.1 is a different machine. The boot payload is a New World `Mac OS ROM` (tbxi), not that 4 MiB dump. The NanoKernel is v2. New World 9.2.1 expects a real PowerPC 32-bit MMU: BATs, an HTAB, instruction and data translation (IR and DR), DSI and ISI, and the NanoKernel HotInts path. The first relocated data access must HIT that translation. If it misses, the guest stays in the exception stub and never paints.

A 9.2.1 New World ROM is different firmware. Old World patches do not apply. The guest turns data translation on. Until that MMU lives in `SheepShaver/src/kpx_cpu` (one guest physical address space; the JIT walks the same translation), 9.2.1 cannot boot. That gap is what locked public SheepShaver at 9.0.4.

**On this tree that MMU HIT is live.** DecodeROM accepts the 4 MiB New World image, the nanokernel v2 handoff runs, the first DSI is on the HIT path (BAT/HTAB), and the 68k interpreter hangs at `pc=50366084`. What is missing is the 9.2.1 installer window with that HIT still in the log. Overlay, Balloon Help, or `SDL2 present n=1` is not that window. Do not claim Finder.

## What milling the ROM means

The nanokernel and the 68k code in the Mac ROM busy-wait, poll hardware, and sit in compare-and-branch loops. On a real Mac a device or interrupt ends those waits. On an incomplete emulator they never end. The guest is alive. The window is a host SDL surface. The program counter does not reach the installer.

Milling classifies the stuck instruction, then changes `ppc-cpu.cpp` so the wait is skipped or completed the way hardware would. Examples: CR fallthrough of a forward branch, skip a leftover 68k ROM offset, or cover a store. Then rebuild Debug, run SheepShaver for a short hang-cap, and KEEP the mill only if the hang class moved and MMU HIT is still live. REVERT if it is worse.

This is not a hex-edit of the ROM on disk. The ROM file stays clean. The mill is in the CPU emulator: when you see this opcode at this place, do not sit in the wait. That search tells which waits are false (a store then `mfsr` is not a wait) and which waits need a real device or a real MMU.

The useful unit is a class of hang (compare then forward branch, 68k trap, empty exception vector). Walking the ROM two bytes at a time is the same search at the wrong grain. The driver does **not** walk ROM by default (`G3_68K_WALK=0`). Leftover skip-68k uses 68k PCs already seen in KEEP map/spin logs.

## Other known attempts


| Attempt                                        | What it is                      | What it reached                                                                                                                                                                                                                                                                                                                 |
| ---------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Ken Hawkins **NewSheep** (10 Aug 2026)         | SheepShaver fork, ARM64 JIT     | Settled 9.2.1 Finder on Apple Silicon. SiliconSheep can install 9.2.2 from ISO and boot it. Source is invite-only pre-release GitHub, not a public clone. Invariants are the reference; the code is not vendored here. [writeup](https://www.allaboutken.com/posts/20260810-newsheep-sheepshaver-mac-os-9-2-apple-silicon-jit/) |
| `kanjitalk755/macemu`                          | Community SheepShaver           | E-Maculation documents 7.5.3 through 9.0.4 only, not 9.1 or 9.2.                                                                                                                                                                                                                                                                |
| `cebix/macemu`                                 | Original SheepShaver            | Same Old World 9.0.4 ceiling.                                                                                                                                                                                                                                                                                                   |
| `rcarmo/macemu-jit`                            | ARM64 JIT donor (Linux aarch64) | SheepShaver Welcome/desktop in interpreter. Not a 9.2 New World path. This fork uses it as `jit-ref` only.                                                                                                                                                                                                                      |
| QEMU `qemu-system-ppc -M mac99` (UTM wraps it) | Full Power Mac                  | Wiki: 9.0, 9.1, and 9.2 boot, install, and run. Slower than SheepShaver. Different architecture (system emulator).                                                                                                                                                                                                              |
| DingusPPC                                      | Accuracy-first beige Power Mac  | Manual lists 7.1.2 to 9.2.2 from CD or disk. 9.2 on that path is used but still experimental. 6100 cannot boot 9.0+.                                                                                                                                                                                                            |
| PearPC                                         | PowerPC emulator aimed at OS X  | FAQ: OS X 10.1 to 10.4. Nobody succeeded at Mac OS 9.                                                                                                                                                                                                                                                                           |
| Basilisk II / Mini vMac / Shoebill             | 68k or Mac II / A/UX            | Not PPC 9.2.1.                                                                                                                                                                                                                                                                                                                  |


Sources: [E-Maculation SheepShaver](https://emaculation.com/doku.php/sheepshaver_mac_os_x_setup), [QEMU PowerPC](https://wiki.qemu.org/Documentation/Platforms/PowerPC), [DingusPPC](https://github.com/dingusdev/dingusppc), [PearPC FAQ](https://pearpc.sourceforge.net/faq.html), [rcarmo/macemu-jit](https://github.com/rcarmo/macemu-jit).

This tree is `billcavalieri/macemu`. Work branch for the mill driver is `cursor/g3-driver` (PR toward `g3`). Base mill C++ is **g3** @ `e25a61f1`. Closed PRs #5–#10 are history; do not reopen or merge them to `arm64-jit`. Build with Xcode (`SheepShaver/src/MacOSX`). Do not merge installer progress to `arm64-jit` until the 9.2.1 installer window is up and live MMU translation is still in the log.

## How many mills, and how long

Driver: `research-score/g3_driver/run`. Snapshot 2 Sep 2026 (state after mill-4403 KEEP).


|                                 | Count                                                                                                             |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Mill-and-test cycles completed  | **4403** (`n=4404` pending hang-cap)                                                                              |
| Of those, skip-68k tested       | **4357**                                                                                                          |
| KEEP                            | **1518**                                                                                                          |
| REVERT                          | **2872**                                                                                                          |
| Hang-cap                        | default **45 s** (floor 15, cap 180). hang04 REVERT ~15 s. KEEP-stable can stop after 8 heartbeats at `50366084`. |
| Live hang                       | leftover `skip-68k`; KEEP PowerPC `pc=50366084`; last KEEP log `/tmp/ss-g3-mill-4403.log`                         |
| Observed 68k map still untested | KEEP-log millable offs (not a 4096 cap). Each hang-cap logs ≤16384 unique `68k map r24=` lines; in-guest bitmap covers the whole 4MiB ROM. Skip-68k mills the KEEP-log list, not the bitmap and not `+2` of the image (`G3_68K_WALK=0`). |


The guest has been in 68k since mill-22/35 (`pc=50366084`). Later KEEP mills reached Toolbox UI traps (InitCPort, then GetCCursor / DialogDispatch / SetPort / DisposeDialog / CloseRgn). That is still not the installer window.

What is left, if milling stays:

1. Unique 68k offsets already seen in KEEP map/spin logs (the histogram), not every halfword. There is **no 4096 unique cap**. Hang-cap logs at most **16384** unique map lines; leftover skip-68k reads those logs. Loop ops (`60ff` / `4efa` / RTS) are skipped unless `G3_68K_MILL_LOOPS=1`. Do not map the whole ROM.
2. Do **not** skip-68k the WINDOW/FS path: GetNewDialog overlay `0x5c86c–0x5c8c0`, CODE 66 helper `0x9440–0x94cf`, `$a190` data `0x16de8–0x16e20`, or A-lines GetNewDialog / NewDialog / GetCCursor / DialogDispatch / SetPort / DisposeDialog / CloseRgn / OpenResFile / GetResource / SysError / InitCursor / GetEOF / GetFPos / Read / SetFPos. Look-again KEEP sites (OpenResFile / GetResource / InitCursor / GetFPos / GetEOF) stay skipped until a later undo mill; do not remill skip.
3. When the histogram is empty: canned leftover (not skip-hang 50326 while 68k has been seen), then one Grok Build mill on escalate. Teach `ppc-cpu.cpp` a trap or device shape, not the next `+2` offset.
4. The real remaining work is still the installer WINDOW (then Finder), video, and any MMU/exception gap that only shows after the window paints. It is not a complete mill of the ROM.

## When to mill (do not stack)

Hang-cap **one** mill at a time. Overlay GetNewDialog at `0x5c86c` is not the installer. A white rect / `fb=yes` vs an old fill is not G3. Do not host-paint `the_buffer`.

| Mill | When |
| --- | --- |
| Launch `A9F2` **CFM Upgrader** (`Mac OS Install` rsrc `g3_rf_off=74305024`, DLOG 510 Splash 354×266). Stamp `G3: 68k Launch A9F2 CFM Upgrader`. Do not LoadSeg 66 from Modem Scripts Installer 4.x at `112182784`. | KEEP 16511. |
| CFM-enter planted PEF (stamp `G3: 68k Launch A9F2 CFM Upgrader PEF enter`). Unpack pidata, reloc, jump TVector. Imports stub `blr`. Overlay 59840 stays. | KEEP 16527 `pc=101013d0 toc=10115000`. G2 live. Then `xlate miss ea=6e657273` (ASCII `ners`) and 68k hang. Banner still 506×44, not G3. |
| Host InterfaceLib imports (not `blr` stubs). Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF import`. First PEF `bl` is glue `lwz r12,off(r2); bctr`. Not `cfm-aa5a` sel=65532. Not skip-68k. | KEEP 16529. PEF live. Then ParamText + Alert 501 (DITL: System ^0 or later). |
| Host SysEnvirons (idx 37, `SysEnvRec.systemVersion=0x0921`). Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF SysEnvirons`. Not grok-escalate. Not skip-68k. | KEEP 16530. Alert 501 gone. Then CurResFile / GetVRefNum / PBHGetVInfoSync. |
| Host CurResFile / GetVRefNum / PBHGetVInfoSync (volume “Mac OS 9.2.1”). Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF vol`. Not grok-escalate. | KEEP 16531. Dummy GetDCtlEntry DSI `DAR=00015000`. |
| Plant GetDCtlEntry Handle+DCE+DRVR `.vfc` (not zeros). Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF dce`. Not grok-escalate. | KEEP 16532. PEF idle in WaitNextEvent; splash GetNewDialog 510 is after that loop returns 0. |
| Host WaitNextEvent (idx 149) + AEProcessAppleEvent err so wait returns 0. Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF WaitNextEvent`. | KEEP 16533. Then DSI `lha r4,0x18(r3)` — TENew Handle. |
| Host TENew/TESetText/TEDispose (idx 263/137/58). Stamp `G3: 68k Launch A9F2 CFM Upgrader PEF TENew`. | **Now.** Hang-cap this alone. KEEP if G2 + TENew stamp and no DSI 15018. |
| Plant/enter PEF `Joy!peffpwpc` from `Mac OS Install` data fork (86446). Not `cfm-aa5a` sel=65532. | KEEP 16518 plant / 16527 enter. |
| GetResource `A9A0` real DLOG/PICT (not dummy); SysError 12 / `$fe` | After CFM Upgrader is the Launch path **and** SysError 12/`$fe` or dummy `GetRes` is on that path (not the overlay). |
| Stop no-op FrameRect/PaintRect/EraseRect; stop dummy GetNewDialog | After the PNG is still not the installer. Do not skip-68k `0x5c86c–0x5c8c0`. |
| CFM `AA5A` sel=1 real fragment / Appearance | After Upgrader PEF is live (not JT spin) and InitCPort / CFM sel=1 is still the hang. Do not remill skip `0x613dc`. |
| GetOSEvent / real click (not `itemHit=1`) | Only after a real window is on the dump. `itemHit=1` was DisposeDialog. |
| G4–G6 (installer writes disk, reboot) | Only after G3: installer WINDOW + live G2 HIT (`G3_WINDOW=yes`). |

`50326564 900107d4/7c0604a6` is a store then `mfsr`, not a wait. Do not mill it as `wait-cmp-fwd-bc`. Do not skip `0x3264fc` / `0x326564` / `0x326568`. Do not remill `e298371e`, skip-pair, skip-mfsr, unstick-stw, or spin-26e88. Do not r24-divert off CODE 0 jump table. Do not or-in EE. Do not mill `ppc-mmu` for `68fff0dc`.

Canned skip-68k mills do not call a model. G3 lock is operator `G3_WINDOW=yes` plus `g2_live` in the hang-cap log (no local LLM). Skip-68k policy comes from NewWorldView Apple FM via `mill-annotations.json`. Grok Build runs **on escalate only** (`grok --prompt-file`, not an HTTP mill API): one C++ mill, then `./run` hang-caps. Do not paste `pack.md`. Spend frontier tokens on a new hang class, not on skip-68k mill 4405.

ROM disasm (`python3 research-score/g3_driver/g3_driver.py rom`) is read-only. It does not mill, launch SheepShaver, or pick the next skip-68k PC. Findings go into `mill_apply.py` policy, not into the unattended loop.

## Where to look

- Boot plan: `OS921-BOOT-PLAN.md` in the repo (it uses internal checkpoint names; this briefing does not).
- CPU mill: `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp`
- MMU: `SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-mmu.cpp`
- Driver: `research-score/g3_driver/` (`README.md`, `./run`, `mill_apply.py`)
- ROM disasm (local prefs ROM, not copied into git): `python3 research-score/g3_driver/g3_driver.py rom`

Hang-cap logs live under `/tmp/ss-g3-mill-N.log`. Mill resume is local `research-score/g3_driver/state.json` (not git). ROM and disk images stay out of git.