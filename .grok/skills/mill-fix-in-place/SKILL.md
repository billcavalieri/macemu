---
name: mill-fix-in-place
description: >-
  Keep a named SheepShaver NW JIT mill item in the tree and change the
  transfer when hops, ARM64 tail-call, or QuickDraw invert/garbled text
  fails. Do not delete, revert, or `if (0)` the item so a mill boots.
  Use when ungated C hops invert QD, tail-call blacks or smears menu
  text, chaining/epilogue/class-gate mill fails, the user says do not
  delete / fix in place / stop deleting, or when tempted to pull
  helper_chain or emit_chain_epilogue to restore Control Strip.
  Use when the user runs /mill-fix-in-place.
---

# Mill fix in place

A mill that boots because the named item was pulled is not done. Done is
the item still in the tree **and** the product goal (platinum Control
Strip, readable menu/window text, hops or tails actually taken).

Pulling a newly milled opcode that locked Starting Up is a different
rule. This skill is for the item the user named this turn (hops,
tail-call, class gate, marshalling).

## When the mill of the named item fails

1. If the item is already gone, restore it before any other edit.
2. Keep a **control** mill of the last form that booted with the item
   off, NULL, or capped — do not destroy that code.
3. Write down what the failing transfer **skips** that the working
   path does. The skip is the next patch, not a reason to drop the item.
4. Change **one** skipped fact. Mill. Keep or change the next fact.
5. Report a short failure plus the next route. Do not recap every dead
   recipe. Do not claim the control mill as the item landing.

Facts that have actually been the skip, in the order they were found:

| Symptom | What the transfer skipped | Route that worked |
|---|---|---|
| Magenta CS, inverted chrome, black labels; wrap-p0 platinum | `execute()` MSR[VEC]/MSR[FP] class gate (`take_vpu` 0xf20 / `take_fpu` 0x800) | Break the hop if successor `uses_vr`/`uses_fpr` and the MSR bit is clear; return 1 so `execute()` takes the exception. Re-read MSR per hop. Clear `dec_wr` after in-chain DEC commit. Refuse A-line PA. |
| Black FB 972, IACK 0, 68k spin `68067ed4` | Frame: pop this ARM64 frame, `x0=cpu`, `br` successor **full** prologue | Nested `blr` and skip-prologue `br` are dead as-is. Do not retry them. |
| Same black with pop+`br` at cap 8/64 | Cap | `NW_JIT_TAIL_MAX = 1`, then C hops. Do not raise the cap until text matches the control. |
| Platinum chrome, smeared menu bar and window title | C-hop marshalling: `commit()` live jc VR/FP to ppc, copy-in only if this block did not already have that class | Pass current block class into `nw_jit_helper_chain`. Copy-out if `cur_vr`/`cur_fpr`. Copy-in only if successor needs the class and `!cur`. Never copy ppc VR over live jc VR on VMX→VMX. |

## Isolation order (tail-call)

This is the sequence that reached a Control Strip mill with the epilogue
still on. Use it for the next transfer, not only tail-call.

1. Control: helper always NULL (or hops gated off). Must show Control
   Strip. Proves the tree still boots.
2. Restore the transfer (pop+`br` full prologue). Cap 1. Mill.
3. If black: the frame or the cap is still wrong. Change that fact.
   Leave the helper, emit, and host hook in the tree.
4. If Control Strip but chrome inverted: add the class gate from the
   table. That is wrap-p3fix2, not "turn hops off".
5. If platinum but menu/title glyphs smear: crop those two strips
   against the last sharp mill (wrap-p3fix2). Diff marshalling against
   the C hop loop. Make the ARM64 hop do the same copy-out/copy-in.
6. Only then consider raising the cap or continuing C hops from the
   successor `chain_pc`.

Do not mill opcodes, grow the cache, or retry KCallTbl 0x700 as a
substitute for this isolation.

## Shot read

Full-window "Finder is up" hides smeared glyphs. Crop the menu bar
(`y=0..22`) and the window title (`y=20..48`) against the last sharp
control. Pixel-identical title + clock-only menu bar diff is done for
text. Magenta CS / ~351xx PNG is invert. ~972 PNG is black. ~24k PNG
is Starting Up lock.

## Known-dead as-is (do not retry to "just boot")

- Ungated C hops (wrap-p3sync/cin/fetch/fall still invert; irq/copy no
  boot; al/ppc black; dec invert no CS). The fix was the class gate,
  not another ungated variant.
- Skip-prologue `br` and nested `blr` (black or NK death at `5031063c`).
- `NW_JIT_TAIL_MAX` 8 or 64 before marshalling and text match cap 1.
- Deleting `emit_chain_epilogue`, `nw_jit_helper_chain`,
  `jit_host_chain`, or `nw_jit_set_host_chain` so C hops boot.

## Done check

Before saying the item landed:

- The named functions and emits are still in `nw_jit.cpp` /
  `ppc-cpu.cpp`.
- The mill took hops or tails (`chain` count not zero for a hop item).
- Control Strip visible at ~100s and still up at 5 min with a moving
  clock.
- Menu bar and window title match the last sharp mill, not merely
  "desktop looks like Finder".
