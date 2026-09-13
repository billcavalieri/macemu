# PMU restart / shutdown brief

Work order for the next New World gate on branch `newworld-boot`:
Special → Restart and Special → Shut Down. Written 12 Sep 2026 after
the NVRAM flash model (`9dd038da`); everything quoted below is in
`/tmp/g6/` or in git. Follow `NEWWORLD-BOOT-PLAN.md` conventions: this is
S4 step 10; the addendum goes after the XPRAM/NVRAM one.

## Ground rules (unchanged, non-negotiable)

- Fix gates; do not skip them. No instruction skipping, no A-trap stubs,
  no ROM or NanoKernel patches, no host interrupt injection, no writes to
  guest low memory to steer the OS, no special-casing a guest PC. In
  particular: do **not** intercept `_ShutDown` (`a895`) or patch the ROM's
  ShutDown code the way Old World SheepShaver does (`rom_patches.cpp`
  2552 `shutdown_dat`, `M68K_EMUL_OP_RESET` at 2049). The New World path
  must go through the hardware the guest talks to: the PMU.
- No ROM, .toast, .hfv or golden capture bytes in git. Do not recapture
  the QEMU golden (`~/nw-golden/`).
- Do not edit `~/Library/Application Support/SheepShaver/os921/prefs`;
  use `--config /tmp/prefs-hd`. Run with `HOME=/tmp/g6/home` so the NVRAM
  image is `/tmp/g6/home/.sheepshaver_nvram.flash`, not the user's.
- Never rebuild while SheepShaver runs: `pgrep -x SheepShaver` (not `-f`);
  wait 3 s between runs.
- Every temporary probe carries `/* TEMP */` and is removed before commit
  (`rg TEMP SheepShaver/src`). Verification files live in `/tmp/g6/`.
- Before each code change write down evidence, the exact change and the
  prediction; after it rebuild, run, quote the confirming line or
  screenshot; revert if the prediction fails; never stack unverified
  changes. Every claim points at a file in `/tmp/g6/` or a log line.
- Small commits, one cause per commit, message states the evidence. New
  source files carry `(C) 2026 Bill Cavalieri` / `Part of SheepShaver
  (C) 1997-2008 Christian Bauer and Marc Hellwig`.
- Harness `SheepShaver-MMUTests` must print `≥ 444 passed, 0 failed`.

## Build / run / tools

```
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj \
  -scheme SheepShaver -configuration Debug ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  -derivedDataPath /tmp/macemu-s4-dd            # same with -scheme SheepShaver-MMUTests
/tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver-MMUTests

cd /tmp && HOME=/tmp/g6/home NW_SCRIPT=/tmp/g6/<script>.nws \
  perl -e 'alarm 230; exec @ARGV' -- \
  /tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver.app/Contents/MacOS/SheepShaver \
  --config /tmp/prefs-hd > /tmp/g6/<run>.log 2>&1        # exit 142 = alarm, normal
python3 /tmp/g4/ppm2png.py /tmp/g6                        # PPM shots -> PNG
```

Script grammar: header of `SheepShaver/src/include/nw_script.h` (`at
<sec> shot|text|key|mouse|click|down|up|log|dump`). Boot to the Finder
takes ≈ 140 s; `at 50 key return` dismisses the first dialog, `at 55
mouse 200 200` parks the cursor. Menu geometry (640×480, from
`/tmp/g6/nv1-special.png`): Special menu title at (245,9); with the
button held, Restart at (260,119), Shut Down at (270,135). Apple menu →
Control Panels → Mouse: down at (10,5), move (90,122), (243,122),
(243,363), up; tracking slider knob at (152,135), Very Slow at
(60,135), close box (37,52) — see `/tmp/g6/nv5.nws`.

Log lines: `NW-BOOT G1:` facts, `NW-BOOT SCRIPT t=…`, `NW-BOOT A <trap>
<pc>`, `NW-BOOT X E <srr0> <vector> [dar]`, `NW-BOOT IO page <pa> first
<R|W><n> …` (first unclaimed touch of a page), `NW-BOOT IO <R|W><n> <pa>
<val> <pc> unclaimed` (first 64). Decoded ROM `/tmp/nwrom.bin` (4 MiB,
68k at logical `0xffc00000`); `cstool m68k40 <hex>`;
`/opt/homebrew/opt/llvm/bin/llvm-mc --disassemble -triple=powerpc-apple-darwin`;
`~/Documents/GitHub/newworldview/.build/debug/NewWorldViewCLI trap-table
"$HOME/Downloads/Mac OS ROM" --trap A895`.

## What is known (evidence)

1. Restart is reached and the guest does its part. `/tmp/g6/nv5.log`
   from `SCRIPT t=195.0 restart selected`: the NVRAM commit runs and
   succeeds (`nvram flash erase bank A`, `bank A programmed 8192 bytes,
   generation 3`), then one new unclaimed page: `NW-BOOT IO page 80000000
   first R4 80000038 00000000 000a10fc` (mac-io + 0x38 = Keylargo feature
   control register FCR0, read by native code at `0x000a10fc`). Then the
   screen goes black (`/tmp/g6/nv2-restart-17s.png`) and the guest sits in
   its idle loop (`A aaf4 ffc0ec2a`, `A aafe …`, DEC/`0x900` and `0x700`
   at `6806e8c0/c4` — the emulator's normal idle) until the alarm. Same
   shape in `nv2.log` (before the flash model: commit timed out, then the
   same black screen) and `nv3.log`.
2. The PMU model (`SheepShaver/src/nw_devices.cpp`, `pmu_dispatch`,
   ~line 1159) already has entries for the two power commands and does
   nothing useful with them: `NW_PMU_RESET = 0xd0` → `return` (no
   response, length table `{0,0}`), `NW_PMU_SHUTDOWN = 0x7e` → answers one
   `0` byte when given 4 data bytes (table `{4,1}`). PMU commands are not
   logged. Whether the guest has sent `0xd0` in the runs above is
   therefore **unknown** — that is the first thing to establish.
3. Reference semantics (Linux `drivers/macintosh/via-pmu.c`, the
   OpenPMU-era command set the model follows): `pmu_restart()` sends
   `PMU_RESET` (`0xd0`) with no data and the PMU asserts CPU reset — the
   whole machine restarts through Open Firmware; `pmu_shutdown()` sends
   `PMU_SHUTDOWN` (`0x7e`) with data `'M','A','T','T'` and the PMU cuts
   power. Neither returns to the caller on real hardware; after sending,
   the kernel spins.
4. Old World SheepShaver's equivalents, for comparison only: `OP_RESET`
   (`emul_op.cpp` 300) re-initialises host services while the 68k ROM
   restarts itself in place; `QuitEmulator()` (`main_unix.cpp` 1464) is
   the exit path, already called from the CPU thread (`emul_op.cpp` 517).
5. Where the machine state comes from on our side: process start.
   `main_unix.cpp` `main()` parses prefs, `load_mac_rom()`, `InitAll()`
   (→ `PatchROM`, `init_emul_ppc` in `kpx_cpu/sheepshaver_glue.cpp` 909:
   CPU registers for the NK entry, `nw_devices_init`,
   `nw_trampoline_program_pic`, NKSystemInfo seed), then `nw_nvram_init`.
   There is no in-process "reset the PPC and rerun the boot contract"
   path today. `main()` overwrites consumed `argv[i]` with `NULL`
   (line 858) — copy argv before that if it is needed later.
6. ConfigInfo / NK / parcels live in the ROM copy at `0x50000000`
   (`vm_protect` read-only after InitAll), the boot-info tree in
   SheepMem `0x50510000`. Guest RAM is `0x10000000 + 0x20000000`.

## Target behaviour

- Restart: the guest's PMU `0xd0` produces a full machine restart — the
  boot contract runs again from the start, NVRAM image kept (bank with
  the higher generation wins, as `nv6` showed), and the guest boots to
  the Finder again in the same log. Proof: Mouse tracking → Very Slow,
  Restart, after the reboot the Mouse panel opens at Very Slow, all in
  **one** run and one log (today this needs two runs).
- Shut Down: the guest's PMU `0x7e 'MATT'` ends the process cleanly
  (exit 0, NVRAM and XPRAM flushed), within a few seconds of the click.
- Nothing else changes: no new unclaimed pages before the click, harness
  green, boot time unchanged.

## Steps

### Step 1 — Probe (no commit)

TEMP in `pmu_dispatch` (or where `via.cmd` is latched, ~1283): print
every command with its data and response, `NW-BOOT PMU cmd %02x len %d
[data…] -> [resp…]`, capped after the first 200 lines *unless* the
command is in {`0x7e`, `0x8f`, `0xd0`, `0xd2`, `0xdf`}. TEMP in
`nw_io.cpp`: log every access (not just the first per page) in
`0x80000000..0x80000fff` (Keylargo FCRs) and `0x80000050..0x8000007f`
(GPIO) after boot, capped. Script A: boot, `at 170` Special → Restart,
`at 172 log restart selected`, shots at +2/+8/+20 s, alarm 230. Script B:
same with Shut Down (270,135). Also dump low memory (`dump 0 1000`) at
+5 s after the click.

Record: the exact PMU command sequence after `restart selected` (expected
somewhere: `0x8f` power-event bookkeeping, then `0xd0`; for Shut Down
`0x7e 4d 41 54 54`), the FCR0 read/write pairs at `0x80000038` (value
written, if any — the guest may be clearing a Keylargo bit before
reset), any GPIO write, and whether the guest spins on a VIA read or a
PMU response afterwards. If `0xd0` never arrives, follow the 68k side
instead: `trap-table --trap A895` gives the ROM `_ShutDown` handler;
`NW-BOOT A a895` in the log gives the caller; disassemble from there to
find what the ROM waits on (a PMU reply, a GPIO level, the FCR bit) and
model that register. Write the findings into the addendum draft first.

Prediction to test: the guest sends `0xd0` once, our model returns
nothing, and the guest is waiting for the reset that never comes.

### Step 2 — PMU power hook in the device model (commit 1)

`nw_devices.h`: `enum nw_pmu_power_event { NW_PMU_POWER_RESTART,
NW_PMU_POWER_OFF }; void nw_pmu_set_power_hook(void (*hook)(int event, void *ctx), void *ctx);`
In `pmu_dispatch`: `0xd0` with `in_len == 0` → log `NW-BOOT G1: PMU
reset (0xd0)`, call the hook with RESTART; `0x7e` with `in_len == 4` →
log `NW-BOOT G1: PMU shutdown (0x7e %02x%02x%02x%02x)`, call the hook
with OFF. Keep the existing responses (the PMU on hardware also
acknowledges before it acts). Check the data bytes but do not require
`'MATT'` unless Step 1 showed the guest sends exactly that. Complete the
handshake before the host acts: raise a flag in `pmu_dispatch`, act from
`nw_devices_tick()` on the next tick (the 60 Hz thread), not from inside
the store instruction. No hook installed → log only (harness default).

Harness: install a test hook, drive `0xd0` and `0x7e 'MATT'` through the
VIA byte bus with the existing `pmu_handshake()` helper, tick, check the
hook fired with the right event exactly once and that a `0xd0` with
stray data bytes does not fire. +4–6 checks.

### Step 3 — Host side: shutdown (commit 2)

`main_unix.cpp`: install the hook after `nw_nvram_init`. OFF →
`nw_nvram_flush()`, then `QuitEmulator()` — the same exit path as the
window close / Old World. Verify with script B: log shows `PMU shutdown
(0x7e …)`, process exit code 0 (not 142) within 5 s of the click,
`/tmp/g6/home/.sheepshaver_nvram.flash` mtime updated by the commit that
precedes it. Note whether the guest turned the screen black before
sending the command (screenshot at +2 s).

### Step 4 — Host side: restart (commit 3)

Two acceptable designs; take the first unless Step 1 shows the guest
needs something in between.

(a) **Re-exec** — a PMU reset is a whole-machine reset including Open
Firmware, and on this branch "Open Firmware" is process start. RESTART →
`nw_nvram_flush()`, `SaveXPRAM()`, `fflush(stdout)`, then `execv(argv0,
saved_argv)` with the copy of `argv` taken before parsing. Environment
(`NW_SCRIPT`, `HOME`, `NW_PVR`) is inherited; the log redirect stays (fd
1 is preserved across exec, so one log holds both boots); `alarm`
timers survive exec (POSIX) — the 230 s budget covers both boots only if
the second is shorter, so use `alarm 400` for the proof run and say so.
`pgrep -x` still finds the same PID. Do the exec from the main thread
if `QuitEmulator` today is main-thread only; otherwise from the tick
thread with all other threads still running is legal for `execv`, but
stop the CPU thread first if the exec ever hangs.

(b) **In-process CPU reset** — stop the CPU thread, `nw_io_reset()`,
`nw_devices_init`, `nw_nvram_init` (same path), redo what
`init_emul_ppc` does (registers, MSR, MMU off, NKSystemInfo seed,
`nw_trampoline_program_pic`), re-run `nw_boot_contract` placement if any
of it lives in guest-writable RAM, then restart the CPU thread. Closer
to what the hardware does (RAM contents survive), but kpx_cpu has no
stop/restart path and the boot contract's RAM-side pieces were not
audited for this. Only if (a) fails for a reason that cannot be fixed on
the exec side.

Verify with script A: one log with two `NW-BOOT G1: nvram flash …`
lines (`bank … gen N` then `gen N+1`), the second boot reaching the
Finder (shot), no `IO page` lines the first boot did not have.

### Step 5 — Proof run and plan addendum (commit 4)

Script: boot, Apple → Control Panels → Mouse, slider to Very Slow,
close, Special → Restart, wait for the second boot, open the Mouse panel
again, shot. One log, one screenshot pair (`before`, `after`). Then a
Shut Down run with exit 0. Addendum in `NEWWORLD-BOOT-PLAN.md` after the
XPRAM/NVRAM one: the PMU sequence as observed (bytes, order, FCR/GPIO
touches), what the model does, both proofs with file names, harness
count, and what is left (e.g. Sleep — `0x7f` — untouched; FCR0 modelled
or not and why). Update the step 9 "Open:" line and the header
paragraph (`PMU restart/shutdown` → done).

### Step 6 — Keylargo FCR0 (only if Step 1 requires it)

If the guest read-modify-writes `0x80000038` and later waits on a bit,
model FCR0..FCR4 (`0x80000038..0x80000048`) as a register file that
reads back what was written, with the reset values the golden's
Trampoline programmed if the golden events show them (`~/nw-golden/run1/
events.txt` DSIs at `8000003x`), in `nw_devices.cpp` like uni-n. Own
commit, own harness checks. Do not add it speculatively.

## Do not

- Do not make Restart "work" by having the 68k ROM's reset code run in
  place (Old World's trick) — it is not what this hardware does and it
  needs a ROM patch.
- Do not answer `0xd0` with a fake "reset done" status and let the guest
  keep running.
- Do not clear the screen, reposition the cursor or touch guest memory
  from the host on restart; the guest already blanks the display itself.
- Do not remove the black-screen wait by shortening PMU timeouts.

## Deliverables

Commits 1–4 above (each with its evidence in the message), the addendum,
`/tmp/g6/pmu-*.log` and shots, harness ≥ 444 + new checks, `rg TEMP
SheepShaver/src` empty.
