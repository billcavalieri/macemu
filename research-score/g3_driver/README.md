# G3 mill-and-test

Working branch is **g3 @ e25a61f1**. Closed PRs #5–#10 are history only.

One command mills C++ toward Mac OS 9.2.1, rebuilds, hang-caps **only after a mill**, KEEP/reverts from the log, and resumes from `state.json`. Python 3.9+ stdlib. Qwen is a G3 lock (and optional NEW-class lock). Qwen does not pick the next file, mill PC, or class.

## One command

```bash
./research-score/g3_driver/run
```

Same as `python3 research-score/g3_driver/g3_driver.py` with no flags.

Ctrl-C stops after the current step. Run again to resume. Do not pass a SHA.

What it does, in order:

1. Classify the last log (existing `/tmp/ss-pr10-<sha>.log` counts; no SheepShaver).
2. Mill `LIVE_CLASS` in `ppc-cpu.cpp` (canned skip-pair for refuse-as-wait classes).
3. Debug arm64 rebuild + 45s hang-cap of the **working tree** (not a re-Debug of e25a61f1).
4. KEEP if G2 still live and the hang is not worse; REVERT otherwise.
5. Repeat until installer WINDOW + live G2 HIT, or Ctrl-C. Never idle: after mill-22/35 reached 68k (`pc=50366084`), mill leftover `skip-68k` of **observed 68k PCs** (map/histogram from KEEP logs) first; `+2` walk is fallback (`G3_68K_WALK=0` disables it). Loop ops (`60ff`/`4efa`/RTS) are skipped unless `G3_68K_MILL_LOOPS=1`. Do not skip-68k UI/FS A-lines (GetCCursor/DialogDispatch/SetPort/DisposeDialog/OpenResFile/GetResource/InitCursor/GetEOF/GetFPos/Read) or the GetCCursor proc `0x5c86c–0x5c8c0`. Hang-cap uses runtime `G3_SKIP_68K_OFF` / `--g3-skip-68k` so skip-68k does not rebuild every mill (`G3_RUNTIME_68K=0` to force rebuild). `skip-hang` of 50326 is only while 68k has not been reached.

Hang-cap never runs unless a mill is in the tree to test. `e298371e` is skipped. `50326564 900107d4/7c0604a6` is **stw+mfsr**, not a wait: the mill skips the pair. Do not mill it as `wait-cmp-fwd-bc`.

WINDOW yes: `G3_WINDOW=yes ./research-score/g3_driver/run`

## Other commands

```bash
python3 research-score/g3_driver/g3_driver.py classify --log PATH
python3 research-score/g3_driver/g3_driver.py score   --log PATH --sha SHA [--window yes|no|unknown]
python3 research-score/g3_driver/g3_driver.py debug   --sha SHA --dd /tmp/macemu-g3-$SHA
python3 research-score/g3_driver/g3_driver.py once    --sha SHA
```

`once` is classify one SHA. `run` is the mill loop.

`classify` / `unittest` run if `PLAN-extract-qwen.txt` and `QWEN-32K-ENVELOPE.md` are missing.

## LIVE_CLASS

`LIVE_CLASS` is the class of the last post-leave `(op,nxt)` whose `pc` equals last-heartbeat pc. If last-hb has no op/nxt, use the most frequent post-leave pair in `50325/50326` (prefer `wait-cmp-fwd-bc` on a tie). Cluster residue (false-cmp-li, backward bc, stw+mfsr) is not STILL_CLASS unless it is LIVE_CLASS.

Decode hex only. This is a store then mfsr, not a cmp wait:

```
NW-BOOT G3: DEC leave 50326 cmp pc=50326564 op=900107d4 nxt=7c0604a6
```

`e298371e` is denylisted. Do not mill that pair as `wait-cmp-fwd-bc`.

`refuse_as_wait` means do not mill those pairs as a wait. The run loop still mills a **skip**.

## Qwen / WINDOW

Qwen cannot accept G3 while WINDOW is unknown. Host `SDL2 present n=1` is not WINDOW. G3 yes needs operator WINDOW yes **and** live G2 HIT.

## Git

Do not merge to `arm64-jit` until installer WINDOW and live G2 HIT.
Base mill work on **g3**, not a closed PR number.
