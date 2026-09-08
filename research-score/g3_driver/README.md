# G3 mill-and-test

Working branch is **g3 @ e25a61f1**. Closed PRs #5–#10 are history only.

One command mills C++ toward Mac OS 9.2.1, rebuilds, hang-caps **only after a mill**, KEEP/reverts from the log, and resumes from `state.json`. Python 3.9+ stdlib. **No Qwen.** Skip-68k policy comes from **NewWorldView Apple FM** (`mill-annotations.json`). Escalation uses **Grok Build** (`grok --prompt-file`), not an HTTP API.

## One command

```bash
./research-score/g3_driver/run
```

Same as `python3 research-score/g3_driver/g3_driver.py` with no flags.

Ctrl-C stops after the current step. Run again to resume. Do not pass a SHA.

What it does, in order:

1. Classify the last log (existing `/tmp/ss-pr10-<sha>.log` counts; no SheepShaver).
2. Mill `LIVE_CLASS` in `ppc-cpu.cpp` (canned skip-pair for refuse-as-wait classes).
3. Debug arm64 rebuild + 45s hang-cap of the **working tree** (not a re-Debug of e25a61f1). Hang-cap writes `/tmp/ss-g3-mill-N.log` and guest FB dumps (`N-fb.pgm` packed **mode width×height** P6 RGB for 32-bit, not pitch-as-gray; `N-fb-plant.pgm` still the mill alias), then converts those to `N.png` / `N-fb-plant.png`. Covering windows and display-sleep do not affect them. Host `screencapture` is off unless `G3_SCREENCAPTURE=1` (writes `N-glass.png`). After SheepShaver exits, the mill log is gzipped in place to `N.log.gz` (plain `.log` removed) and the durable copy in `research-score/` is that `.log.gz`. Live `/tmp/ss-g2-run.log` stays uncompressed while the guest is writing. Classify/KEEP/`zcat`-style reads use `read_log()` (`gzip.open` text stream). PNG/PGM dumps are not gzipped. After the dumps land, hang-cap SHA-256-compares this mill’s `.pgm` files to KEEP’s and prints `fb_changed=yes|no|unknown` (PNG is not hashed). That line is a mill signal only; KEEP/REVERT still come from the log. When **guest `fb=yes`** (valid same-size PGM, ≥10% pixels different from KEEP; `G3_FB_DIFF` to change; truncated dumps do not count), it also prints `FB_GLASS_CHANGED`, appends `/tmp/ss-g3-fb-glass-changed.log`, and posts a macOS notification + Glass sound (`G3_FB_NOTIFY=0` to disable). When skip-68k leftover map is empty (`leftover mill kind=grok-escalate`), it prints `SKIP68K_EMPTY`, appends `/tmp/ss-g3-skip68k-empty.log`, and the same Glass notification (mill C++ then `./run`). Plant-only and hash-noise black frames do not ping. Look at that mill’s `N.png`; if it is the installer, `G3_WINDOW=yes ./run`. Existing uncompressed mill logs: `python3 research-score/g3_driver/g3_driver.py gzip-logs`.
4. KEEP if G2 still live and the hang is not worse; REVERT otherwise.
5. Repeat until installer WINDOW + live G2 HIT, or Ctrl-C. Never idle: after mill-22/35 reached 68k (`pc=50366084`), mill leftover `skip-68k` of **observed 68k PCs** (map/histogram from KEEP logs) first; approved **NewWorldView skip candidates** take priority over the histogram. `+2` walk is fallback (`G3_68K_WALK=0` disables it). There is no 4096 unique-off cap: each hang-cap logs at most **16384** unique `68k map r24=` lines; the in-guest bitmap still marks every even ROM halfword. Skip-68k mills the KEEP-log list, not the whole ROM. Loop ops (`60ff`/`4efa`/RTS) are skipped unless `G3_68K_MILL_LOOPS=1`. Do not skip-68k UI/FS A-lines (GetNewDialog/NewDialog/GetCCursor/DialogDispatch/SetPort/DisposeDialog/OpenResFile/GetResource/SysError/InitCursor/GetEOF/GetFPos/Read) or the GetNewDialog overlay `0x5c86c–0x5c8c0` or CODE 66 helper `0x9440–0x94cf`. Hang-cap uses runtime `G3_SKIP_68K_OFF` / `--g3-skip-68k` so skip-68k does not rebuild every mill (`G3_RUNTIME_68K=0` to force rebuild). `skip-hang` of 50326 is only while 68k has not been reached.

Hang-cap never runs unless a mill is in the tree to test. `e298371e` is skipped. `50326564 900107d4/7c0604a6` is **stw+mfsr**, not a wait: the mill skips the pair. Do not mill it as `wait-cmp-fwd-bc`.

WINDOW yes: `G3_WINDOW=yes ./research-score/g3_driver/run`

Grok vision for WINDOW is a **separate** Grok Build CLI script, not nested from `./run` (nested grok hangs on `~/.grok/leader.sock`). Hash the PGMs in the mill loop; use vision only when you want a WINDOW yes/no read of the guest-FB PNG (`N.png` converted from `N-fb.pgm`):

```bash
./research-score/g3_driver/vision --png /tmp/ss-g3-mill-N.png
# or
python3 research-score/g3_driver/g3_driver.py vision --n N
```

It calls `grok --prompt-json` with ACP image blocks (`type=image`, `mimeType`, base64), a unique `--leader-socket /tmp/ss-g3-grok-vision.sock`, and `--max-turns 1`. It does not mill C++. Print is `WINDOW=yes` or `WINDOW=no`. Overlay / Balloon Help / a lone rectangle is not installer.

## NewWorldView + Grok Build

1. In **NewWorldView**: **Analyze logs** (Mill logs tab) for the full historical histogram → bar chart on 68k / PPC / traps → classify addresses (Apple FM) → approve on **Annotations** tab → **Export macemu pipeline** (or copy individual JSON files).
2. Pipeline bundle includes `mill-histogram.json`, `mill-annotations.json`, `mill-research-report.json`, and `mill-pipeline.json`.
3. Copy into `research-score/g3_driver/` **or** set env vars:

```bash
export G3_HISTOGRAM=/path/to/mill-histogram.json
export G3_ANNOTATIONS=/path/to/mill-annotations.json
./run
```

Skip-68k priority: approved annotations → histogram ranks → runtime trap PCs → KEEP log map → `+2` walk.
4. For escalation: export **Escalate to Grok Build** from NewWorldView → set `G3_ESCALATION_DIR` to that folder, or:

```bash
python3 research-score/g3_driver/g3_driver.py grok-escalate --dir /path/to/export
```

Grok Build reads `pack-escalation.md` + `grok-prompt.md`. Run-wide escalate still writes `pack-slim.md` (includes NW annotation summary when loaded).

## Other commands

```bash
python3 research-score/g3_driver/g3_driver.py classify --log PATH
python3 research-score/g3_driver/g3_driver.py score   --log PATH --sha SHA [--window yes|no|unknown]
python3 research-score/g3_driver/g3_driver.py debug   --sha SHA --dd /tmp/macemu-g3-$SHA
python3 research-score/g3_driver/g3_driver.py once    --sha SHA
python3 research-score/g3_driver/g3_driver.py run --annotations mill-annotations.json --histogram mill-histogram.json
python3 research-score/g3_driver/g3_driver.py tokens
python3 research-score/g3_driver/g3_driver.py grok-escalate --dir ./nw-escalate
python3 research-score/g3_driver/g3_driver.py vision --png /tmp/ss-g3-mill-N.png
./research-score/g3_driver/vision --n N
python3 research-score/g3_driver/g3_driver.py gzip-logs
python3 research-score/g3_driver/g3_driver.py screenshot-perm
./research-score/g3_driver/screenshot-perm
```

`once` is classify one SHA. `run` is the mill loop. `tokens` prints Grok per-mill totals and Apple FM sum from loaded NW JSON (`mill-annotations.json`, `mill-histogram.json`). When loaded, driver prints `ANNOTATIONS …` / `HISTOGRAM …` and `TOKENS apple_fm …` lines.

## LIVE_CLASS

`LIVE_CLASS` is the class of the last post-leave `(op,nxt)` whose `pc` equals last-heartbeat pc. If last-hb has no op/nxt, use the most frequent post-leave pair in `50325/50326` (prefer `wait-cmp-fwd-bc` on a tie). Cluster residue (false-cmp-li, backward bc, stw+mfsr) is not STILL_CLASS unless it is LIVE_CLASS.

Decode hex only. This is a store then mfsr, not a cmp wait:

```
NW-BOOT G3: DEC leave 50326 cmp pc=50326564 op=900107d4 nxt=7c0604a6
```

`e298371e` is denylisted. Do not mill that pair as `wait-cmp-fwd-bc`.

`refuse_as_wait` means do not mill those pairs as a wait. The run loop still mills a **skip**.

## G3 lock / WINDOW

No local LLM. G3 yes when operator sets **`G3_WINDOW=yes`** and the hang-cap log has **live G2 HIT**. Host `SDL2 present n=1` is not WINDOW. Overlay GetNewDialog / a white rect / `fb=yes` vs an old fill is not G3.

Hang-cap **one** mill. Current: Launch/LoadSeg keep `r24` in planted CODE. GetResource dummy, FrameRect no-op, CFM sel=1, and GetOSEvent only after that hang-cap shows CODE live (then a real window for events). See `SheepShaver-921-for-developers.md` (When to mill).

## Git

Do not merge to `arm64-jit` until installer WINDOW and live G2 HIT.
Base mill work on **g3**, not a closed PR number.
