# Next steps: WP3 4b-2 through G6, then WP5

Branch **`arm64-jit`** at **`6beba57a`** (S4 step 16, WP3 4b-2 `lwz`
vs-kpx). Past G5, interpreter baseline, WP4 banks, WP3 4a, WP3 4b
fallback, and `lwz` vs-kpx (`/tmp/g8/4b2-lwz.log`: 23.7 M `lwz` 0-miss;
`stw` still off `nw_jit_op_dispatch` — enabling it hung the `50310490`
fill). Copy-out gated until `stw` helpers are vs-kpx'd.

**Next (strict order):** WP3 4b-2 `stw` vs-kpx, then idle translate,
then 4c invalidation, 4d exception exactness, then the three-run G6
measurement table. **WP5 video damage after G6**, not before. Do not set
`NW_JIT=on` until copy-out is gated off. Follow `NEWWORLD-BOOT-PLAN.md`
conventions.

Deferred (unchanged): Sleep (`0x7f`), Startup Disk / OF `boot-device`.

## Ground rules (unchanged)

Same as `PMU-restart-brief.md`: fix gates, no ROM/NK patches, no
A-trap stubs, no guest-memory steering; no ROM/toast/hfv/golden bytes
in git; `--config /tmp/prefs-hd` (never edit the user's prefs);
`HOME=/tmp/g6/home` (or a fresh dir) for the NVRAM image;
`pgrep -x SheepShaver` empty before any build or run, `sleep 3` between
runs; `/* TEMP */` on every probe, `rg TEMP SheepShaver/src` empty
before commit; evidence → change → prediction → rebuild → run → quote;
small commits, one cause each; harness `SheepShaver-MMUTests`
**≥ 450 passed, 0 failed**; new source files carry the two copyright
lines.

Two rules learned this session, now mandatory:

- **One SheepShaver per volume.** `sys_unix.cpp` opens disk files with
  `O_EXLOCK`; a second instance logs `WARNING: Cannot open … (Resource
  temporarily unavailable)` and boots to the "?" floppy. Before every
  run: `pgrep -x SheepShaver` must print nothing. Two "crashed disk"
  windows means two diskless instances plus a third holding the lock.
- **Scripted runs write to their own log and shot prefix.** Never launch
  the same `NW_SCRIPT` twice into the same `> log` (the second truncates
  the file while the first keeps writing at its offset; the result has a
  NUL hole and `rg` stops there - use `rg -a`).

Verification files: `/tmp/g8/` for this brief.

## Build / run / tools

```
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj \
  -scheme SheepShaver -configuration Debug ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  -derivedDataPath /tmp/macemu-s4-dd          # also -scheme SheepShaver-MMUTests
/tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver-MMUTests

cd /tmp && HOME=/tmp/g8/home NW_SCRIPT=/tmp/g8/<s>.nws \
  perl -e 'alarm 230; exec @ARGV' -- \
  /tmp/macemu-s4-dd/Build/Products/Debug/SheepShaver.app/Contents/MacOS/SheepShaver \
  --config /tmp/prefs-hd > /tmp/g8/<run>.log 2>&1      # exit 142 = alarm
python3 /tmp/g4/ppm2png.py /tmp/g8
```

Restart across a script: `NW_SCRIPT_RESTART=<second>.nws` (the second
boot's script), `alarm 480`. Shut Down from a Finder script: click the
desktop first if another application is frontmost (the Setup Assistant
has no Special menu - `/tmp/g7/boot.log` missed for that reason), then
`mouse 245 9`, `down`, `mouse 270 135`, `up` → `PMU shutdown` → exit 0.

Log lines as before (`NW-BOOT G1:`, `SCRIPT`, `A`, `X E`, `IO page …
first`, `IO … unclaimed`); `NW-BOOT T <wall ms> <exceptions> <A-traps>
<pc> <msr>` once per second (`nw_boot_contract.cpp` 1071) is the only
rate counter today.

## Step 0 - Reliability items from the 13 Sep install (small commits)

0a. **`.AppleCD` read failure is silent.** The second install attempt
    stopped at ≈ 28 % with "Problems were encountered reading the source
    file 'Big System Morsels'"; the third, identical, went through. The
    only error paths are `CDROMPrime` (`cdrom.cpp` ≈ 599/617, shared
    with Basilisk via symlink): `paramErr` on an unaligned
    position/length, `readErr` when `Sys_read` returns short. `Sys_read`
    (`sys_unix.cpp` 752) is `lseek` + one `read`; `read()` may return −1
    (`EINTR`) or short, and −1 cast to `size_t` is "short". Change:
    (1) a permanent `printf("WARNING: .AppleCD read pos=%llx len=%zx
    actual=%zx errno=%d")` on the `readErr` path and the analogous line
    on `paramErr` (the file's existing WARNING style); (2) `Sys_read` /
    `Sys_write` loop on `EINTR` and on short counts until `length` or a
    real error - `pread`/`pwrite` so the seek cannot be split. Prediction:
    the install is unchanged; if the failure recurs the log names errno.
    Verify: harness unaffected, one full install from a fresh
    `mkfile -n 2g` target with the `/tmp/g7/install.nws` script (Quit and
    Shut Down included, exit 0). Keep the old volume aside as
    `macos921-blank.hfv.<date>` until the new one booted twice.

0b. **A missing `disk` is fatal for scripted New World runs.** Today an
    `EAGAIN` on the `O_EXLOCK` open leaves the guest booting nothing and
    the script clicking on grey. In `sys_unix.cpp`'s `EAGAIN` branch,
    when `nw_script_active()` (Debug) - or, simpler, when the
    `NW_SCRIPT` env is set - print the WARNING and `exit(1)`. GUI users
    keep the current behaviour. Verify with a Python `flock` holder as
    in `/tmp/g6/lock-test.log`.

0c. **Pending PMU event without a hook.** `nw_devices_tick()` leaves
    `g_pmu_power_pending` set when no hook is installed, so a hook set
    later fires a stale event. Clear it when there is no hook (harness:
    send `0xd0` with the hook unset, tick, install the hook, tick → no
    call). +2 checks. Cosmetic, but the harness default should be clean.

## Step 1 - G5 accept on the interpreter (commit + addendum)

G5 text: "Finder desktop, menu bar, about box says 9.2.1". Two of the
three are in `/tmp/g7/boot2-finder.png`; the About box is not recorded.

1a. **Proof run** (`/tmp/g8/g5.nws`, installed volume, fresh NVRAM dir,
    `alarm 260`): boot; at 170 click the desktop; Apple menu → About This
    Computer (`mouse 10 5`, `down`, first item ≈ (90,25), `up`); shot at
    +5 s - the box must read **Mac OS 9.2.1** and the memory line the
    prefs' 512 MB; close; Special → Shut Down; exit 0. Record time to
    Finder (first `SCRIPT` line after the desktop is drawn, or the wall
    time of the first `A a9f2`-free idle stretch) from the `T` lines.
1b. **Idle desktop is quiet.** After the Finder is up, one minute with no
    input: count `X E` per second and `A` per second from the `T` deltas
    (`boot2.log` shows ≈ 46 k exceptions/s and ≈ 22 k A-traps/s while
    idle - mostly `aafe`/`a22e`/`a88f` timer and event polling, and
    `0x900` DEC). Write the numbers down; they are the WP3/WP5 baseline
    and the "DSI/s after idle" figure the measurement table asks for.
    Do not tune anything here.
1c. **Unclaimed I/O at zero.** Every boot logs three `IO page … first`
    lines: `80012000` (SCC polling, `R1`), `80020000` / `80021000` (IDE
    probes, `W1 …60 a0`). Claim them explicitly in `nw_io.cpp` /
    `nw_devices.cpp` as absent devices with the values the default
    already returns (0 on read, writes dropped), with a one-line comment
    each and harness checks, so a *new* unclaimed page is a signal
    again. No behaviour change: same boot, same shots. This closes the
    "Keylargo GPIO details / IDE / SCC" open item; the FCR/GPIO writes
    before a PMU reset (S4 step 10 §1) belong to the same commit.
1d. **Apple Monitor Plugins −29208.** Intermittent (S4 step 9 addendum).
    Not a gate. If a G5 run shows it, save the shot and read the number
    from the dialog; do not suppress the extension.
1e. Addendum "S4 step 11 - G5" with the About shot, the idle rates, and
    the boot time; header status → G5 reached. Update the OS921 plan's
    gate table only if its wording needs the New World caveat (About
    box says 9.2.1; machine ID 406).

## Step 2 - Measurement baseline before touching the CPU (no commit until the counters exist)

`OS921-BOOT-PLAN.md` "Measurement": time to Finder, PPC ops/s, tiles/s,
JIT flush/s, DSI/s after idle. Only exceptions/s and A-traps/s exist.

2a. Add an instruction counter to the interpreter's dispatch (a 64-bit
    increment behind `NW_BOOT_LOG`, so Release is untouched) and print
    it in the `T` line as a sixth field; `nw_boot_contract.cpp` 1071.
    Check the cost: boot time to Finder before/after must be within
    noise (three runs each; quote all six).
2b. Add a presented-frames counter to `VideoHostPresent()` and print it
    in the same line (frames/s at idle is the WP5 "tiles/s" stand-in
    until damage tracking exists).
2c. Baseline table in the addendum: interpreter, 512 MB, 640×480×32,
    time to Finder, ops/s during boot and at idle, exceptions/s,
    A-traps/s, frames/s at idle. This table is what G6 is measured
    against; without it "JIT on" is not a gate.

## Step 3 - WP4 memory banks (own branch `memory-subsystem`, merged back)

Prerequisite for the JIT's fast path. Today `Mac2HostAddr` is pointer
math over one mapping, ROM is a copy at `0x50000000` (`vm_protect`ed),
I/O goes through `nw_io` when the MMU yields a physical address in an
I/O page, the frame buffer is host memory the guest writes through the
MMU.

3a. Print a bank map at boot: RAM `10000000+20000000`, ROM copy, SheepMem
    `50510000`, frame buffer `50590000+12c000`, KDP/low memory, NVRAM
    flash aliases, mac-io / uni-n / OpenPIC pages. Pure logging.
3b. Make the physical decode one function used by the interpreter's
    load/store and by `nw_io`: RAM and ROM hits are pointer math, I/O
    hits the trap table, everything else is the unclaimed default.
    Harness: bank lookup for every range above, including the aliases
    the NVRAM model registered (16 of them) and the ROM's read-only
    property (a store must not reach it).
3c. No functional change: same boot, same `IO page` lines (zero after
    step 1c), same shots, harness green. Boot time within noise of the
    step 2 baseline.

## Step 4 - WP3 ARM64 JIT on the MMU (branch `arm64-jit`)

The OS921 plan's design: port the Basilisk II AArch64 approach onto
`kpx_cpu`'s PPC front end; translation cache keyed by
`(phys_page, guest_pc, msr_ir, endian)`, invalidated on `tlbie`,
code-modify, WIM change; `MAP_JIT` + `pthread_jit_write_protect_np`;
TLB hit inlined, miss calls the C walker; interpreter stays the source
of truth. The dyngen JIT (`ENABLE_DYNGEN`) is x86-only and stays off.

Order of work, each step with the interpreter as oracle:

4a. **Equivalence harness first.** A `SheepShaver-JITTests` target (or a
    section of the MMU harness) that runs a block of PPC ops through
    the interpreter and through the JIT and compares GPR/CR/XER/FPSCR/
    VR and the MMU's R/C bits. Start with the integer subset the NK idle
    loop uses (`mfspr`/`mtspr` DEC, `lwz`/`stw`, `cmp`, branches,
    `rlwinm`, `add`), then loads/stores through BAT and HTAB, then
    AltiVec (the S4 step 9 aliasing bug - `vmrghb v17,v0,v17` - is the
    first vector test case).
4b. **Block cache + dispatcher** with everything falling back to the
    interpreter; boot must be identical (log diff on `G1:`/`IO`/`X E`
    lines) and no faster. (Done at `cf7f8eb4` / S4 step 15.)
4b-2. **`lwz`/`stw` helpers vs-kpx**, then idle-loop translate; measure
    ops/s at idle against the step 2 baseline. Copy-out stays gated until
    the load/store helpers match. **`lwz` done** at `6beba57a` (23.7 M
    compares, 0 miss). **`stw` next**: must not JIT NONE/IO stores
    (ROM fill at `50310490` / stack at `0x3fbfe000`); then idle translate.
    WP3's "done" line: NK idle loop faster than the interpreter by a
    measured factor, 9.2.1 still page-faults correctly.
4c. **Invalidation gates**, each with a harness test and a boot run:
    `tlbie` and `mtsr`/BAT change drop blocks of the affected physical
    page; stores into a translated page (`mtdec`-free path: the 68k
    emulator's self-modifying code and the Code Fragment Manager) flush
    the block; MSR[IR] flip keys a different block; W^X toggles are
    counted (`JIT flush/s` in the `T` line).
4d. **Exceptions from translated code**: DSI/ISI/DEC/external/alignment
    must leave SRR0/SRR1/DAR/DSISR exactly as the interpreter does; the
    G2 accept (DR-on `lwz` at SRR0 hits, no second DSI) is re-run with
    the JIT on. PMU restart (exec) and shut down unchanged.

Do not: run the JIT on Old World 9.0.4 (not a goal), copy dyngen,
change the MMU model to suit the JIT, or claim G6 from a single run.

## Step 5 - G6 accept

JIT on in the Debug scheme for the run (a pref or env, logged in the
`G1:` banner like `PVR`), the same G5 script, exit 0, and the
measurement table filled: time to Finder, ops/s, exceptions/s,
A-traps/s, JIT flush/s, frames/s, each interpreter vs JIT, three runs
each. Addendum, header status to G6, `OS921-BOOT-PLAN.md` "Measurement"
filled in. Do not claim G6 until this three-run table exists. Do not
enable bare `NW_JIT=on` until copy-out is gated off (after 4b-2 / 4c / 4d).

## Step 6 - WP5 video damage (branch `video-damage`)

**After G6**, because at idle the full-frame upload every present is the
next cost once the CPU is no longer the bottleneck. Promote
`update_display_static_bbox`; present dirty tiles only; "done" = the idle
Finder does not upload a full frame every vsync (frames/s from step 2b
stays, bytes uploaded per second drops). Not before the three-run G6
table - damage tracking early only slows the interpreter baseline.


## Deferred, with the reason

- Sleep (`0x7f`): the guest expects to wake on input; modelling it means
  a suspend/resume of the CPU thread. Not a gate.
- Startup Disk / OF `boot-device` in the NVRAM `common` partition: the
  exec restart uses the same command line; honouring the guest's choice
  needs a prefs-side mapping of OF paths to `disk` entries. Not a gate.
- BAT range 1 overlap (`nw_boot_contract.cpp`, S4 step 6 "Not done") and
  DEC-pending clear on `mtdec`: revisit in step 4d, where exception
  timing becomes observable.
- ExtFS (host "Unix" volume) and clipboard on New World: polish after G6, if wanted.

## Deliverables per step

Commits with evidence in the message, the addendum, `/tmp/g8/` logs
and shots named in it, harness count, `rg TEMP SheepShaver/src` empty,
`pgrep -x SheepShaver` empty at the end.
