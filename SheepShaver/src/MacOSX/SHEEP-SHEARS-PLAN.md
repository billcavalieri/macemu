# Sheep Shears: guest tools for SheepShaver (plan)

Guest tools in the spirit of qemu-guest-agent and VMware Tools: a small Mac OS 9 program inside the guest that talks to the Swift host app. Goals, in the order asked for:

1. The mouse leaves the guest window by itself when the guest arrow reaches an edge, and enters at the matching place. No ctrl-g, no guessing.
2. Copy and paste both ways, including files where possible.
3. The guest side is cross-compiled on macOS 27 with Retro68. Every piece of new host-side logic is Swift.

Status: plan only. Nothing here is built. "Verified" below means read in this repo's source today; "not verified" means it needs the Phase 0 spike.

## Status (2026-10-07)

| Phase | State |
| --- | --- |
| 0 Spike | Done. Retro68 builds on macOS 27/arm64 (`~/Retro68-build`, outside the repo). A PPC application executes the fake opcode and reaches the host with registers intact, under the JIT. |
| 2 Transport | Done for `HELLO`, `POLL`, `LOG`, `PTR_POS`. Unit tests for the codec; end to end checked headless (tool connects, host logs it, reports arrive). |
| 3 Pointer | Built and unit tested (`tools/shears/test.sh unit`, mutation checked). **Not yet tried with a real mouse**: release, the placement of the host pointer, and re-entry need a hands-on pass. |
| 4 Clipboard | Text works both ways on the New World build and is covered by `tools/shears/test.sh clip` (see below). The tool adds nothing to it yet. |
| Tool status and host-initiated shutdown | Done and tested end to end (`tools/shears/test.sh shutdown`). |
| 5 Files, 6 Control Strip | Not started. |

What differs from the first draft of this plan:

- The selector is a native op, `NATIVE_SHEARS` (45), not an `OP_*` EmulOp. The opcode word is `0x18000B42`. Selectors are append-only; `shears.cpp` has a `static_assert` on the value because installed tools hard-code it.
- The tool checks the SheepShaver signature word `'Baah'` at 0x2800 before executing the opcode. On anything else the opcode is illegal, so it never runs there.
- The host is not an actor. The core calls the handler synchronously on the emulation thread while the guest waits, so `ShearsHost` is a lock-protected class with a synchronous entry point; window work is handed to the main actor and the reply does not wait for it. (A closure created inside a main-actor method inherits main-actor isolation and traps on that thread; the entry point is a file-scope function for that reason.)
- The host sets the polling rate. Every guest wake-up costs host CPU (about 20% of a core at 30 wake-ups a second, measured), so each `POLL`/`PTR_POS` reply carries `nextPollTicks`: 15 (4 a second) normally, 2 only while the host is grabbed in absolute mode and the pointer has moved in the last second. Idle cost with the tool running measured the same as without it. The host's release rule needs a fresh position, not a changing one, so a resting pointer is covered by a one-second heartbeat.
- The mailbox reply for `HELLO` is host version, host capabilities, enabled capabilities, `nextPollTicks`. `POLL` and `PTR_POS` replies are `nextPollTicks`. `LOG` text only reaches the host log with diagnostics on.
- Prefs: `noshears true` turns it off; `edgerelease <points>` sets the push needed (default 40, 0 turns edge release off). The screen size is read from the guest on every report, so any resolution the user picks (and a change made in the Monitors control panel) is followed.
- The pointer is the low-memory `Mouse` global (0x830, `LMGetMouseLocation2`); `GetGlobalMouse` is not in the Multiversal headers.

### Tool status and shutdown (2026-10-07)

- **Is the tool running?** `ShearsHost.shared.status` (a `ShearsToolStatus`: running, version, capabilities, shutdown phase). "Running" means it said HELLO and has called within 5 seconds; the host cannot look into the guest, so it is inferred from the tool's polling (at least four calls a second). The state is shown in the window subtitle (`<VM name> · Sheep Shears`, only while running), on the first line of the new **Guest** menu, and in the log (`guest tool connected`, `the guest tool stopped responding`). The logic is the pure `ShearsSession` (unit tested, mutation checked).
- **Shut Down Guest…** (Guest menu; enabled only while the tool is running and no request is waiting) asks for confirmation, then flags a request in the poll replies (second payload word, bit 1) until the tool acknowledges with `SYS_RESULT` (command 5: action 1 = shutdown, then a Mac OS error code, 0 = ok). The tool sends the Finder the **Finder suite's** shut-down event (`FNDR`/`shut`; the core suite `aevt` has no such event, and sending it there is silently ignored) so Mac OS asks every application to quit and save as for Special > Shut Down. When Mac OS finishes, the guest sends the PMU shutdown command and the emulator quits (`nw_pmu_host_power`). An application with unsaved work asks about it in the guest window; the menu status shows "the guest was asked to shut down" until then. A second request is accepted 10 seconds after the first.
- Capabilities: bit 0 pointer, bit 1 shutdown, in HELLO both ways.
- `tools/shears/test.sh shutdown` boots with the tool, and a diagnostics-only hook (`NW_SHEARS_SHUTDOWN_AFTER=<seconds>`, honoured only with `NW_VERBOSE=1`) makes the host request a shutdown; the test passes when the emulator quits by itself after the PMU shutdown. Restart is the same call with the Finder suite's `rest`; not added.
- Not tested by hand: the Guest menu items and the confirmation sheet themselves (they compile and the subtitle indicator was seen in a screenshot).

### Clipboard findings (2026-10-07)

- **The existing trap-based clipboard bridge was never installed on the New World ROM.** `patch_68k()` stops part-way on that ROM (the log line `patch_68k incomplete (New World, continuing)`), before it reaches the ZeroScrap/PutScrap/GetScrap patches, so neither direction worked. They are now in `patch_scrap()`, called from `PatchROM()` for that case. This does not need the guest tool.
- A guest self-test app (`guest/sheepshears/scraptest.c`) and `tools/shears/test.sh clip` check the round trip against the real Mac pasteboard: host text to guest (MacRoman bytes exact, line breaks as CR), a second host copy, guest text to host (the MacRoman characters and the CR to LF change), and a 1 MB copy. The script saves your pasteboard first and restores it at the end.
- **The scrap is per process for background-only applications.** Data imported by one background process is not visible to another (measured: the tool's import was invisible to the test app and the reverse). Foreground applications get the scrap moved by the Process Manager when you switch. So the tool must **not** import the pasteboard on the host's behalf: the import is a one-shot per pasteboard change in `clip_macosx64.mm`, and an import done in the tool's context uses it up before the foreground application pastes. A first version of the tool did this ("nudge on change") and broke host-to-guest paste in the test; it was removed.
- Not tested headless: pasting into and copying from real foreground applications (SimpleText, the Finder). That needs a hands-on check.
- `clip_macosx64.mm` imports the pasteboard only for the first guest process that calls `GetScrap` after a host change. If two foreground applications are used one after the other and the Process Manager does not carry the scrap across, the second could miss it; a per-process import record (keyed on the process's A5) is the fix if that shows up.

## Installing and building the guest tool

`tools/shears/build.sh --apps-only` builds `build/sheepshears/SheepShears.bin` (MacBinary) and `SheepShears.dsk` (an HFS image holding the application). It needs a Retro68 toolchain (`RETRO68_PREFIX`, default `~/Retro68-build/toolchain`). To install: mount `SheepShears.dsk` in the guest (as a `floppy`), copy the application into `System Folder:Startup Items`, restart. It is background-only, so nothing appears; the host log shows `Sheep Shears: guest tool connected`. For a disk image on the host, Retro68's `hmount`/`hcopy -m` can copy the `.bin` into a volume without starting the guest.

## 1. What the tree already gives us

Verified in source:

| Need | What exists | Where |
| --- | --- | --- |
| Guest to host call | A fake PowerPC opcode (`0x18000000 | fn<<12 | op<<6 | 2`, primary opcode 6) that the emulator executes as an EmulOp or NativeOp. ExtFS, SheepBlaster and the QuickDraw hooks already use it. Selectors are `OP_*` in `emul_op.h`; `OP_MAX` is the end of the list. | `thunks.cpp`, `emul_op.h`, `sheepshaver_glue.cpp` `execute_emul_op` |
| Guest arrow position | The video driver reports where Mac OS drew the arrow (`csSave->cursorX/Y`, visible flag) to `video_cursor_moved`, which reaches `GuestDisplayView.setGuestArrow`. | `video.cpp:672`, `video_macos.mm:194`, `GuestDisplayView.swift:483` |
| Absolute pointer | `ADBSetAbsMouse` writes the guest's `MTemp` (0x828), `RawMouse` (0x82c) and `CrsrNew` (0x8ce). `VideoHostMouseAbs/Move/Button/SetRelMouse` are already exported to Swift. | `adb.cpp:499`, `video_macos.mm:102` |
| Clipboard | ROM-level patches of `ZeroScrap`/`PutScrap`/`GetScrap` (traps A9FC/A9FE/A9FD) call `clip_macosx64.mm`, which converts TEXT, styl and PICT to and from `NSPasteboard`. | `rom_patches.cpp:2615`, `emul_op.cpp:607`, `clip_macosx64.mm` |
| Shared folder | ExtFS, shown as the "Unix" volume. Finder info is kept in the `com.apple.FinderInfo` xattr (or in `.finf` files where xattrs are unavailable). **Resource forks are not supported** (the code comments say "pseudo resource fork, silently ignored"). | `extfs.cpp`, `extfs_macosx.cpp` |

Not verified, and the Phase 0 spike must settle them:

- that clipboard sync works end to end in the current New World build (it should; nobody has re-tested it since the NW work);
- that the arrow-position feed is exact in every cursor mode (hardware cursor, software cursor, hidden arrow, games);
- that a guest *application* (not ROM code) can execute the EmulOp opcode and reach the host with registers intact.

Two consequences:

- **Edge release comes from the guest tool.** The video-driver arrow feed exists but is unverified in every cursor mode and covers only the hardware cursor, so it is kept as a logged cross-check, not the source. The tool reads `Mouse` at 0x830 and reports the real position. The header of `GuestDisplayView.swift` says the picture edge deliberately does not release today because the window keeps no position of its own; the tool's report is the position it was missing.
- **Clipboard text and pictures already work through traps.** The tool is needed for what traps cannot do: files, more flavors, and being told about changes without waiting for the guest to paste.

## 2. Architecture

```
 Mac OS 9 guest                                          SheepShaver process
+-------------------------------+                +-----------------------------------+
| Sheep Shears (background app) |  EmulOp word   | C++ core: ShearsCall(selector,    |
|  - mailbox in locked memory   | -------------> |   mailbox ptr)  -- thin C ABI --  |
|  - polls on idle (null event) |                |                |                  |
|  - uses File Manager, Scrap,  | <------------- | Swift: ShearsHost (one actor)     |
|    Process Manager, Apple Ev. |  mailbox data  |   pointer / clipboard / files /   |
+-------------------------------+                |   settings / logging               |
                                                 +-----------------------------------+
```

### 2.1 Transport

One new selector, `OP_SHEARS`, added at the end of the `OP_*` list so no later number moves. The tool calls it with one register pointing at a *mailbox*: a fixed 4 KB, 4-byte-aligned, locked block in guest memory (`NewPtrSysClear`, then held). The mailbox has a header (magic, protocol version, sequence, command, status, length) and a payload area; bulk data goes in 3 KB chunks. The handler runs on the emulation thread, reads and writes guest memory through the existing accessors, and hands the request to Swift through a small C function that Swift imports with `@_silgen_name`, as `VideoHost*` already does. The C++ side holds no logic.

Host to guest is polling, not interrupts: the tool makes a `POLL` call on null events and from a Time Manager task at a low rate (about 10 per second when idle, faster during a transfer). The reply carries any pending host requests. This avoids running anything at interrupt time in the guest.

Every length and offset the guest supplies is checked against the mailbox size before use. A bad request gets an error status and a log line, never a crash.

PPC versus 68k guest code: the tool is a PPC (PEF) application, because Mac OS 9 here is PPC-native and the PPC opcode is the established path. The same sources can build for 68k, which goes through the 68k layer (`0x71xx`-style EmulOps); that is the fallback if the spike shows the PPC route fails. Either way the mailbox format is identical.

### 2.2 Guest program form: where it should live in Mac OS 9

Recommendation: **a background-only application in Startup Items**, plus an optional **Control Strip module** as a front end. Not an extension, not a control panel, not a Control Strip module on its own.

| Candidate | Verdict |
| --- | --- |
| Background-only application | Runs from startup, has its own event loop and heap, can use File Manager, Scrap Manager and Apple Events safely. Easy to quit, update, debug. **Primary.** |
| Extension (INIT) | Runs once at boot, then only through patches or tasks. Anything heavy has to run at interrupt time or inside patched traps, which is how extensions crash Mac OS 9. Also the hardest thing to produce with Retro68. Avoid. |
| Control Panel (cdev) | Code runs only while its window is open. Good for settings, useless for a service. |
| Control Strip module (sdev) | Runs inside the Control Strip's context, called at its schedule. Good for a status icon and a few menu items ("Share clipboard", "Send files to Mac...", "Release mouse"), poor as the engine. **Optional front end**, talking to the app through Apple Events. |

Settings: the app opens a small window when launched from the Finder (a second launch brings it forward). A cdev is only worth building if the toolchain makes it cheap; the Control Strip module, if the toolchain can make a standalone code resource, gives the quick toggles.

### 2.3 Toolchain: Retro68

Facts from the Retro68 README: it builds 68k and PowerPC (PEF) Classic Mac applications, code resources through Rez, and disk images; its default Multiversal Interfaces are incomplete (no Carbon, Open Transport, Navigation Services, or anything after System 7.0), and Apple's Universal Interfaces 3.x (3.4 tested) can be used instead. Building it builds binutils and gcc twice. The README does not claim Apple Silicon or recent macOS support, and community reports mention arm64 linker problems with stray x86_64 Homebrew libraries.

On this machine today: `cmake`, `bison` and `gmp` are installed; `boost`, `mpfr`, `libmpc`, `flex`, `texinfo` are not; there is no Retro68. Xcode and Swift 6.4 are present.

Plan:

- Retro68 stays outside the repo: `RETRO68_PREFIX` points at an install, the same way `RAVE_SDK_DIR` does for Apple's RAVE headers. The repo holds only `guest/sheepshears/` sources, a CMake file, and `tools/shears/build.sh --apps-only`.
- Apple's Universal Interfaces are Apple's; they are never committed, only referenced by path. If the Multiversal headers lack something we need (Drag Manager, Control Strip, Appearance, Navigation), we declare the few prototypes ourselves in `guest/sheepshears/compat.h` or switch to the Universal Interfaces locally.
- Output: the application, packaged as MacBinary and as a small HFS disk image (`SheepShears.dsk`) that the host app can mount in the guest (as a CD or floppy) for installation. Later the host app can offer "Install Sheep Shears" in VM settings.
- If Retro68 will not build on macOS 27/arm64: build it in a Linux container or VM (the README's primary platform), or use a prebuilt toolchain; the guest build is then a Docker step and the rest of the plan is unchanged.

### 2.4 Host side (Swift)

New group `SheepShaver/src/MacOSX/SheepApp/Shears/`:

- `ShearsProtocol.swift`: the mailbox layout and a bounds-checked encoder/decoder (pure value types, unit-testable without the emulator).
- `ShearsHost.swift`: one actor that owns session state (guest version, capabilities, what is enabled) and dispatches requests.
- `ShearsPointer.swift`, `ShearsClipboard.swift`, `ShearsFiles.swift`: the features.
- Settings in `VMSettingsView.swift`: "Sheep Shears" section with a switch per feature (pointer, clipboard, files), the guest tool version, and an Install button.
- C++ stays a thin shim: `ShearsCall(uint32 mailbox)` in the core and the `OP_SHEARS` case in `emul_op.cpp`.

### 2.5 Protocol sketch

Header (big-endian, as the guest is): `magic 'SHRS'`, `version`, `seq`, `cmd`, `status`, `length`. Commands, grouped by phase: `HELLO` (guest version, host version, capability bits both ways), `POLL`, `LOG` (guest text for the host log), `PTR_POS` (guest arrow position, visibility), `PTR_WARP` (host asks the tool to move the arrow), `CLIP_OFFER` (flavors and sizes), `CLIP_GET` and `CLIP_DATA` (chunks), `FILE_BEGIN`, `FILE_DATA`, `FILE_END` (both forks plus Finder info), `FILE_CANCEL`, `SYS_TIME`, `SYS_QUIT`. Unknown commands return `status = unsupported`, so old tools and new hosts work together. Capabilities, not version numbers, decide what a session may do.

## 3. Features

### 3.1 Pointer: automatic edge release

Current behaviour: a click grabs, host cursor hidden, movement is delivered to the guest as deltas, ctrl-g releases. The window keeps no position of its own.

Design:

1. While grabbed, `PTR_POS` reports arrive in guest pixels. Keep the last one.
2. When the arrow is at an edge (within 1 px, in guest pixels) and the host pointer's physical delta pushes outward for more than a small threshold (a few host pixels, to ignore jitter), release: ungrab, show the host cursor, and place it at the matching spot on the window edge (the guest x or y scaled to the window, plus the overshoot).
3. Entering: when the host pointer enters the picture while released, place the guest arrow at the entry point with `VideoHostMouseAbs` (already done in absolute mode), and grab on the first click as now. A pref ("Capture on entry") can grab immediately.
4. Games and relative mode (`mouse relative`): no edge release by default; the pref remains.
5. The source is the tool: it reads `Mouse` (0x830) and sends `PTR_POS` when it changes. The video-driver feed is only compared against it (a disagreement is logged once); the tool wins. This also covers a software-drawn arrow, where the driver reports nothing. Without the tool running, there is no edge release (ctrl-g as today).

Test: a headless harness cannot move a real mouse, so the host-side decision logic (`ShearsPointer`, a pure function of position, delta and bounds) gets unit tests, and the rest is a short manual checklist for the user (no synthetic input without a frontmost check, per the standing rule).

### 3.2 Clipboard

Phase 2 verifies the existing trap-based path and fixes what is broken. The tool then adds:

- change notification, only if a way is found that does not import in the tool's own context (see the findings above): the host pasteboard change count and the guest's scrap count are the signals;
- loop prevention (a sequence number per clip; a clip we just wrote is not sent back);
- flavors: TEXT with styl, PICT, then RTF to and from `styl`, and Unicode (`utxt`) with MacRoman conversion done in Swift, replacing the ObjC++ font-mapping code only when a defect makes that necessary;
- large clips in chunks, with a size cap pref (default 16 MB);
- **files**, via the file protocol below: copying files in the Finder does not put them on the Mac OS 9 scrap, so "copy files" is a drag or a menu command, not Cmd-C.

### 3.3 Files

ExtFS cannot be the transport: it drops resource forks, and Classic files without their resource forks are usually broken. So files travel through the Shears protocol as a stream of both forks and the Finder info (type, creator, flags, dates), written by the tool with the File Manager (`FSpOpenRF`, `FSpCreate`, `FSpSetFInfo`) and read on the host from the data fork, `/..namedfork/rsrc` and the `com.apple.FinderInfo` xattr.

- Host to guest: drop files on the guest window (`NSDraggingDestination` in `GuestDisplayView`). The host queues them; the tool receives them and writes them into a "From Mac" folder on the Desktop (or the folder set in preferences), then posts a notification (and optionally opens the window). Names go from UTF-8 to MacRoman, are cut to 31 characters with a unique suffix on collision, and reserved characters are replaced.
- Guest to host: a droplet (the same application: dropping files on its icon, via the `odoc` Apple Event) and a Control Strip menu item "Send to Mac...". The host writes into `~/Downloads/Sheep Shears/<VM name>/` (configurable), re-creating the resource fork and Finder info, and reveals the file.
- Dragging out of the guest window onto the Mac desktop is not part of this plan: the guest window gives no drag source to AppKit. The droplet is the supported way.
- Safety: the host only writes inside the chosen folder, refuses names containing `/`, `:` or `..` after conversion, opens with `O_NOFOLLOW`, caps file size and count, and shows progress with a cancel.

### 3.4 Later

Time sync (host to guest), restart from the host menu (shutdown is done; restart is the Finder suite's `rest`), a prompt to shut the guest down when the window is closed while the tool is running, a "guest is idle" hint that lets the emulator sleep, the guest display following the host window size (Display Manager from the tool), and volume sync. None is needed for the first release.

## 4. Phases and exit criteria

**Phase 0: spike (decides everything else).**
- Install Retro68 on macOS 27/arm64 (or its container fallback); build a PPC hello-world and a 68k one; launch both in the guest.
- A PPC app executes the `OP_SHEARS` opcode with a pointer register; the host prints the mailbox magic. Repeat from 68k.
- Check whether a standalone code resource (cdev, sdev) can be produced; if not, drop the optional front end.
- Exit: the call works from at least one of PPC/68k, or the spike says where it fails.

**Phase 1: pointer edge release from the guest tool (decided).** The host-only route (judging the edge from the video-driver arrow feed) is dropped: its edge behaviour depended on unverified assumptions and earlier host-only attempts released early or on one edge only. The guest tool reads the real pointer (`Mouse` at 0x830) and reports it, so the host decides from the guest's own position. Phase 1 is therefore folded into Phases 2 and 3; the decision logic (`ShearsPointer`, a pure function of position, delta, bounds and hotspot) still gets symmetric four-edge and corner unit tests. The driver feed stays as a cross-check that is logged, not used.

**Phase 2: transport, `HELLO`/`POLL`/`LOG`, the app skeleton, the install image.** Exit: the tool starts at boot, the host shows its version in VM settings, protocol fuzz tests pass (malformed lengths, truncated chunks, wrong magic, a guest that never answers), no measurable CPU use when idle.

**Phase 3: pointer position from the guest (`PTR_POS`, `PTR_WARP`).** Exit: with the tool, edge release also works with a software arrow; the log shows no disagreement between the two sources in a normal session.

**Phase 4: clipboard.** Exit: a verified matrix (text, styled text, picture, empty, 1 MB, 20 MB, repeated copies, copy in both directions in quick succession) with no echo and no loss.

**Phase 5: files.** Exit: a verified matrix (data-only file, file with resource fork, long and non-ASCII names, name collisions, a 100 MB file, cancel mid-transfer, disk full on either side, an aborted guest), and a byte-for-byte check of both forks after a round trip.

**Phase 6: Control Strip module and the extras in 3.4**, if the toolchain allows.

Tests that need no guest live as Swift unit tests (protocol, pointer decision, name conversion, fork reading). Guest tests run headless with a cloned disk, as the boot tests do, and look for `SHEARS:` lines in the log plus a screenshot taken once, late (screenshots during start-up stall the boot).

## 5. Security and privacy

A guest that shares the clipboard and files is a channel between two security domains. Defaults: pointer on, clipboard on for text only, files off until enabled per VM. The clipboard bridge ignores items marked concealed or transient by the host (password managers). Everything the guest sends is untrusted input: bounds, sizes and counts are checked, paths never leave the transfer folders, nothing the guest sends is executed or auto-opened on the host. The tool itself talks only to the emulator, never to the network.

## 6. Risks

| Risk | Answer |
| --- | --- |
| A library we want to reuse needs POSIX calls | Retro68's own C library covers the protocol code; posix9 (Apache 2.0, small, young, PPC and 68k) is an optional fallback or reference, not a dependency. It is Toolbox-mapped POSIX and would not replace the File Manager calls for forks and Finder info |
| Retro68 does not build or run correctly on macOS 27/arm64 | Linux container or VM for the guest build; the plan above does not change |
| A guest application cannot reach the EmulOp opcode in the New World path | Fall back to the 68k opcode; if both fail, add a host-injected stub (the emulator already writes guest code, as `rom_patches.cpp` does) and have the tool call that |
| Multiversal headers lack the managers we need | Local prototypes, or Apple's Universal Interfaces kept outside the repo |
| Control Strip modules or cdevs cannot be built | Ship the application only; its window and a menu are enough |
| Polling latency feels slow for clipboard or drops | Faster poll during activity; the host can also post a null event to the app through a flag the Time Manager task checks |
| The arrow-position feed is wrong in some mode | Phase 3 adds the tool's own reading; the host logs disagreements |
| Resource forks or Finder info mangled | Round-trip test of both forks and the 32 bytes of Finder info on every build |
| Clipboard echo loops | Per-clip sequence numbers and an "originated here" flag |

## 7. Decisions needed

1. Guest binary: PPC first with 68k as the fallback (recommended), or 68k first.
2. ~~Phase 1 before the tools exist~~ Decided: no host-only edge release; the guest tool is the source.
3. Defaults for clipboard and files (section 5).
4. Whether to install Retro68's prerequisites on this machine for the Phase 0 spike (`brew install boost mpfr libmpc flex texinfo`, then a long build), or use a container.
5. Where received files land in the guest by default (Desktop "From Mac" folder is the suggestion).


## Distribution and settings (control panel + installer)

- Settings: two switches, both on by default: mouse edge release and clipboard sharing. The guest control panel ("Sheep Shears", an `appc` application in Control Panels) writes "Sheep Shears Prefs" and sends SETTINGS (command 6) to the host at once; the tool sends the same word after every HELLO. The host also has the two switches in the Guest menu (kept in UserDefaults). A feature runs only when both allow it.
- Install: the Xcode build runs `tools/shears/build.sh --if-needed` (a build phase of the SheepShaver target; skipped without Retro68) and builds SheepShearsInstaller.hfv (installer app, "Sheep Shears Files" folder with the tool and the panel, ReadMe). The app bundles it; Guest > Install Sheep Shears... copies it to ~/Library/Application Support/SheepShaver and adds `disk *<path>` to the VM's prefs. After the guest restarts, open "Install Sheep Shears" and click Install: it quits a running tool, copies the files, starts the tool. "Remove" undoes it.
- Tests: `tools/shears/test.sh [unit|install|settings|shutdown|clip|all]` (unit = 117 Swift checks).
- Open: a Control Strip module (PowerPC code resource, needs its own investigation), custom icons, clicking through the panel and installer by hand.
