# New World mouse, keyboard and Swift app: review and plan

Reviewed October 3, 2026, at `sheepforce` HEAD `5626442e` plus the uncommitted tree. Scope: the AppKit app in `SheepShaver/src/MacOSX/SheepApp/*.swift` (about 2,000 lines), the C bridge in `video_macos.mm`, and the ADB/PMU path in `BasiliskII/src/adb.cpp` and `SheepShaver/src/nw_devices.cpp`. Nothing here was run in a guest. One finding (F2) was reproduced with a standalone Swift script. The rest come from reading the code and are marked **verify** where behavior is not yet observed.

## How input reaches the guest

```
NSEvent
  ├─ SheepWindow.sendEvent → GuestDisplayView.claimMouse   (clicks, drags, picture only)
  ├─ tracking area → mouseMoved / mouseEntered              (released state)
  └─ local event monitor → move()                           (grabbed state, every window)
        │
        ▼
  place() → sendAbsDelta / sendRel   (event.deltaX/Y + fractional carry)
        │
        ▼
  VideoHostMouseMove  (@_silgen_name → C)  →  nw_adb_mouse_move   (atomic dx, dy)
        │
        ▼
  PMU autopoll every 8 ms while motion is pending → adb_mouse_poll (±63 per packet)
        │
        ▼
  Mac OS cursor driver (its own acceleration) → draws arrow
        │
        ▼
  cscDrawHardwareCursor → ADBNoteGuestMouse + moveMacCursorX → Swift arrow overlay
```

Both preference modes are delta-based once grabbed. "Absolute" differs only in sending one `VideoHostMouseAbs` at the grab. The pointer is captured with `CGAssociateMouseAndMouseCursorPosition(0)`, and a 20 Hz timer plus per-move `holdGrab()` re-pins it with `CGWarpMouseCursorPosition`.

## Findings

### A. Defects (fix first)

| # | Finding | Evidence | Confidence |
|---|---|---|---|
| F1 | **Held keys, modifiers and buttons are never released on focus loss.** `focusChanged` only calls `pauseCapture()`. Cmd-Tab delivers Command-down to the guest, then Command-up goes to the other app, so the guest keeps Command held. Ctrl-G releases buttons but not keys. | `GuestDisplayView.swift:193-204`, `flagsChanged` at `:392` | High by reading; **verify** in guest |
| F2 | **Empty text prefs round-trip as `"true"`.** `PrefsFile.save` writes `cdrom ` and `load` reads a one-token line as `true`. A VM that is not running therefore shows `true` in empty Drives, Network and keycode fields, and saving again persists it. | Reproduced: save `cdrom=""`, load gives `true` | **Reproduced** |
| F3 | **Y sign differs between the two modes for the same captured pointer.** `sendAbsDelta` sends `+deltaY` ("a disconnected pointer reports up as down"), `sendRel` sends `-deltaY`. Both end in `nw_adb_mouse_move`. At least one is wrong. `NSEvent.deltaY` is positive downward and ADB dy is positive downward. | `:774-784` vs `:809-819` | High that they disagree; **verify** which is right |
| F4 | **Host key auto-repeat is forwarded as repeated key-downs.** `keyDown` has no `isARepeat` filter (only the Ctrl-G path checks it). Mac OS generates its own repeat from a single down. | `:380-384` | Medium; **verify** |
| F5 | **Middle and other buttons act as the left button.** `which = buttonNumber == 1 ? 1 : 0`, so button 2+ maps to 0. `nw_adb_mouse_button` accepts only buttons 0 and 1. | `:743`, `nw_devices.cpp:1110` | High |
| F6 | **Modifier tracking is flag-based, not key-based.** With both Shifts down, releasing one leaves the flag set, so no up is sent. Right-hand modifiers also collapse onto the left codes. | `change()` at `:484`, table at `:406` | High |
| F7 | **No scroll wheel.** There is no `scrollWheel(with:)` anywhere in the Swift app. The Settings sheet still shows `mousewheelmode` and `mousewheellines`, and the old ObjC launcher implements them. | grep over `MacOSX/` | High |
| F8 | **Settings rows that nothing honors.** `hotkey`, `swap_opt_cmd`, `keycodes`, `keycodefile`, `init_grab`, `scale_nearest`, `scale_integer`, `dsp`, `mixer` appear in `VMSettingsView.swift` but the Swift path never reads them, except `hotkey`, which is hard-coded to Ctrl-G. | `:253-313` | Medium; confirm each against the C side |

### B. Pointer accuracy and feel

| # | Finding | Notes |
|---|---|---|
| F9 | **The grab does not reliably put the guest arrow under the click.** `VideoHostMouseAbs` sends one delta computed from the last guest position, but the guest applies acceleration to each packet. `nw_script.cpp` already needs a closed-loop step-and-read-back (`ST_MOUSE_TO`, up to `MOUSE_MAX_TRIES`) for this reason. The first click after a grab can land off target. **verify** by measuring error. |
| F10 | **Sensitivity ignores picture scale.** Deltas are host points, but the overlay arrow is drawn at `guestPixels × scale`. At scale 2 the arrow moves 1 guest pixel per point and visibly lags the hand. At scale below 1 it overshoots. |
| F11 | **Double acceleration.** `event.deltaX` is already macOS-accelerated, and the guest driver accelerates again. This matters most in the "games" mode. |
| F12 | **Large flicks drain slowly.** Each packet clamps to ±63 and one packet is sent per 8 ms poll, so about 7,900 counts/s at most. Hard flicks keep moving after the hand stops. **verify**; the cadence is `NW_ADB_POLL_MS = 8`. |
| F13 | **Arrow overlay does one `DispatchQueue.main.async` per guest cursor report.** There is no coalescing. This is fine at 60 Hz but unmeasured. |

### C. Capture robustness

The capture logic is a stack of workarounds for observed macOS behavior (reconnect after idle, event suppression after a warp). It works, but it is hard to change safely.

- About 20 loose properties (`attached`, `gaming`, `pausingCapture`, `disassociated`, `cursorHidden`, `hidHost`, `trackingClick`, `lockPoint`, `stuckDx/Dy`, `suppressGuestKeyUp`, …) form an implicit state machine spread over six methods.
- The local monitor sees every window's events. It is gated only by `attached` and `pausingCapture`.
- `holdGrab` re-calls `CGAssociateMouseAndMouseCursorPosition(0)` every 50 ms and on every move.
- `warpToCenter` toggles associate and disassociate in sequence. `grabAbsolute` does not warp at all.
- The two-path event delivery (`moveMonitor != nil` suppresses the view overrides) is order-dependent.

### D. Structure, tests and tooling

| # | Finding |
|---|---|
| F14 | **The Swift input layer has no automated coverage.** `nw_script.cpp` injects at `nw_adb_mouse_move` and `nw_adb_key`, so every scripted mouse and key test in `qualify_nw68.py` bypasses `GuestDisplayView`. The "16 mouse targets" pass says nothing about event routing, capture, deltas or focus handling. |
| F15 | `GuestDisplayView.swift` is 900 lines and mixes event routing, capture, hotkeys, key mapping, cursor image construction and the arrow overlay. |
| F16 | 16 `@_silgen_name` declarations across three files, and no bridging header. A signature mismatch with the C side is silent undefined behavior. |
| F17 | Logging uses `fputs(stdout)` with a `NW-BOOT` prefix. It is not separable from guest output and not filterable in Console. |
| F18 | `VirtualMachineStore.create` writes `disk`, `rom` and name into the prefs file unvalidated. A newline in a field injects extra keys. |
| F19 | The "optimize mouse for games" checkbox and the "absolute" pref name do not describe behavior. Both modes capture the pointer. |

## Plan

Each phase has a gate. Do not start a phase before the previous gate passes. Phases 1 and 2 are independent of each other.

### Phase 0: seam, baseline and tests (about 2 days)

1. Extract the pure logic from `GuestDisplayView` into a plain Swift module with no AppKit dependency:
   - `PointerMath`: delta accumulation with fractional carry, picture-scale mapping, point-to-guest-pixel mapping.
   - `KeyboardMapper`: host virtual key code to ADB code, per-key modifier state, repeat filter, pressed-key set.
2. Add a unit-test target (Swift Testing). Cover the current behavior first, including F3's sign question, so the tests record what the code does today.
3. Add a synthetic-event hook to the app, enabled only by an environment variable or launch flag. It posts real `NSEvent`s into `SheepWindow.sendEvent` (not `nw_adb_*`) so scripts exercise the Swift path. Add script verbs for move, drag, click, key-down and key-up, scroll and focus loss. Keep the existing verbs as the control.
4. Measure the baseline in a Release build and log it:
   - click-to-guest-arrow error in pixels after a grab (F9);
   - host event to arrow-visible latency, p50 and p95;
   - guest pixels moved per host point, with the picture at 0.5×, 1× and 2× (F10);
   - flick drain time for a 3,000-point flick (F12).

**Gate:** tests pass in CI form (`xcodebuild test`), synthetic-event scripts run in a cloned guest, and the baseline numbers are recorded in this file.

### Phase 1: input correctness (about 2 days)

1. **F1:** on resign-key, resign-active and ungrab, send key-up for every pressed key, every held modifier and every held button, and clear the tracking state.
2. **F2:** write an empty value as an explicit marker the loader understands (or quote values), and make `load` return an empty string for `key ` lines. Add a round-trip test over every Settings key. Check how `prefs.cpp` parses the same lines so both readers agree.
3. **F3:** use the Phase 0 test and a guest check to establish the correct Y sign, then make both paths share one function.
4. **F4:** drop `isARepeat` key-downs (keep the repeat filter on the Ctrl-G path).
5. **F5:** ignore buttons above 1 for now. Decide separately whether a middle click maps to anything.
6. **F6:** track modifiers per physical key from `flagsChanged.keyCode`. Decide whether to keep right-hand modifiers distinct (ADB has separate codes).
7. **F7:** implement `scrollWheel` according to `mousewheelmode` and `mousewheellines`. Check the ObjC launcher's behavior (arrow keys or page keys) and match it. Handle trackpad phases and momentum so one swipe does not send hundreds of key presses.
8. **F8:** for each dead Settings row, either implement it or remove it. Do not leave controls that do nothing.

**Gate:** Phase 0 tests plus new tests for each item. Guest check: Cmd-Tab away and back leaves no stuck modifier; holding a key produces one down; scrolling a Finder window scrolls it.

### Phase 2: pointer accuracy (about 3 days)

1. **F9:** make placement closed-loop. At grab (and on a resync request), step toward the click point and read the arrow position back from `ADBNoteGuestMouse` until within tolerance, with a bounded number of tries. Only then deliver the button-down. Reuse the logic in `nw_script.cpp` `ST_MOUSE_TO` rather than inventing a second one.
2. **F10:** scale deltas by `guestSize / pictureBounds` in the click-to-grab mode so the arrow tracks the hand on a scaled picture. Keep the unscaled behavior for "relative" mode.
3. **Spike (time-boxed, 1 day):** inject absolute position by writing the Mac low-memory cursor globals (`MTemp` at 0x828, `RawMouse` at 0x82C, `CrsrNew` at 0x8CE) on the CPU thread. The classic non-NW `ADBInterrupt` path does this. Open question: the commit history and the comment on `ADBSetAbsMouse` suggest delta-only was chosen deliberately on New World, so the spike must first show why. If it works it removes both the grab step and the need to capture the pointer for absolute use.
4. **F11:** raw-input spike for relative mode using `GCMouse` unaccelerated deltas behind a preference. Keep only if the measured feel and focus behavior are better.
5. **F12:** if the drain test shows a real problem, raise the per-packet clamp handling (carry the remainder across packets faster) and compare with a real ADB mouse's 100 polls/s.

**Gate:** Phase 0 baseline re-run. First-click error under 2 guest pixels at 1× and 2× scale, arrow tracks the hand within 1 pixel per 100 points of travel at scales 0.5×, 1× and 2×, and no regression in the existing 16-target scripted run.

### Phase 3: capture robustness (about 3 days)

1. Replace the loose flags with one explicit state enum (`released`, `grabbed`, `paused`) and one transition function, with the CG calls isolated behind a small protocol so tests can fake them.
2. Spike alternatives to the 20 Hz re-pin timer, in this order:
   - disable the local-events suppression interval after a warp (the likely cause of the freeze fixed in `25c2d695`), so a single warp at grab replaces repeated warping;
   - take movement from `GCMouse` deltas so it no longer depends on where the cursor sits.
   Keep the current timer as the fallback until the replacement passes the matrix below.
3. Limit the local monitor to events for this window (`event.window === window`).
4. Test matrix, run by script where possible and by hand otherwise: grab and ungrab by hotkey; Cmd-Tab; click another app; sheet open and close; Spaces switch; second display; full screen; window resize and sidebar toggle while grabbed; cursor idle for 60 seconds then move; sleep and wake.

**Gate:** the matrix passes with the timer removed, or the timer stays and the reason is documented with the failing case.

### Phase 4: structure and tooling (about 2 days, can overlap Phase 3)

1. Split `GuestDisplayView` into the Phase 0 modules, a capture controller (Phase 3) and an arrow overlay view. The view keeps only event routing and layout.
2. Replace the 16 `@_silgen_name` declarations with a bridging header (or a small C module) so the compiler checks signatures.
3. Replace `fputs` logging with `os.Logger` (subsystem and category per area). Keep the `NW-BOOT` lines the Python runner parses, either by mirroring them or by updating `qualify_nw68.py` in the same change.
4. Validate and escape prefs values in `VirtualMachineStore.create` (F18).
5. Rename the mouse setting in the UI to describe behavior (F19), keeping the `mouse` pref key for compatibility.

**Gate:** behavior unchanged on the Phase 0 tests and the scripted guest run, no new warnings under Swift 6 strict concurrency (already enabled).

### Phase 5: polish (optional)

- Configurable release hotkey that honors the existing `hotkey` pref.
- Coalesce arrow-overlay updates to one per display frame and disable implicit layer animation.
- Predictive local arrow movement. Do this only if Phase 0 latency is poor, because the guest's acceleration makes prediction rubber-band.

## Risks and things not to do

- **Do not remove the re-pin timer before Phase 3's matrix passes.** It exists for reproduced macOS behavior.
- **Do not change the ADB layer and the Swift layer in one commit.** Native input changes (clamp, cadence, button count) need their own test run through `nw_script`.
- **Raw-input and low-memory injection are spikes, not commitments.** Either could be rejected on evidence.
- **Keep the existing script verbs.** They are the control that separates a Swift-layer regression from a device-layer one.
- The PPC JIT work and this work both touch `ppc-cpu.cpp` timing indirectly through DEC and idle wake. Run the Release soak from [NW-PPC-JIT-REPORT.md](NW-PPC-JIT-REPORT.md) on a build that includes both before calling either done.

## Order of value

F1 and F2 first (real defects, small, low risk). Then F9 and F10 (the pointer-feel problems users will notice). Then F7. Phase 3 pays off in maintenance, not in new behavior, so schedule it after the user-visible fixes.
