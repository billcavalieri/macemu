# G3 driver

Working branch is **g3 @ e25a61f1**. Closed PRs #5–#10 are history only.

Python 3.9+ stdlib. Classifies SheepShaver Debug NW-BOOT hangs by **opcode class**, not by PC. Qwen is a G3 lock only. Qwen does not pick the next file, mill PC, or class. Debug is the verifier.

This tree does not mill C++. A later PR mills off `g3` from an escalate file.

## Commands

```bash
python3 research-score/g3_driver/g3_driver.py classify --log PATH
python3 research-score/g3_driver/g3_driver.py score   --log PATH --sha SHA [--window yes|no|unknown]
python3 research-score/g3_driver/g3_driver.py debug   --sha SHA --dd /tmp/macemu-g3-$SHA
python3 research-score/g3_driver/g3_driver.py once    --sha SHA
```

`once` writes a class mill prompt (`escalate-<sha>.md`) when policy says so. It does not launch Grok Build. Cloud cannot run SheepShaver.

`classify` / `unittest` run if `PLAN-extract-qwen.txt` and `QWEN-32K-ENVELOPE.md` are missing.

## LIVE_CLASS

`LIVE_CLASS` is the class of the last post-leave `(op,nxt)` whose `pc` equals last-heartbeat pc. If last-hb has no op/nxt, use the most frequent post-leave pair in `50325/50326` (prefer `wait-cmp-fwd-bc` on a tie). Cluster residue (false-cmp-li, backward bc, stw+mfsr) is not STILL_CLASS unless it is LIVE_CLASS.

Decode hex only. This is a store then mfsr, not a cmp wait:

```
NW-BOOT G3: DEC leave 50326 cmp pc=50326564 op=900107d4 nxt=7c0604a6
```

`e298371e` is denylisted. Do not mill that pair as `wait-cmp-fwd-bc`.

## Qwen / WINDOW

Qwen cannot accept G3 while WINDOW is unknown. Host `SDL2 present n=1` is not WINDOW. G3 yes needs operator WINDOW yes **and** live G2 HIT.

## Git

Do not merge to `arm64-jit` until installer WINDOW and live G2 HIT.
Base mill work on **g3**, not a closed PR number.
