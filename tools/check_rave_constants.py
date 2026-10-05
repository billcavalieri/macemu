#!/usr/bin/env python3
"""Check the RAVE numeric constants in SheepShaver/src/rave.cpp against Apple's headers.

RAVE.h and RAVESystem.h are Apple's (QuickDraw 3D SDK) and are NOT part of this repository.
Point RAVE_SDK_DIR at a folder that holds them. rave.cpp carries its own copies of the
numbers it needs (selector tags, gestalt selectors, pixel types, error codes ...); a wrong
number silently breaks guests, so this compares every one of them with the header.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAVE_CPP = ROOT / "SheepShaver/src/rave.cpp"

# our name -> header name when they differ
ALIAS = {
    "kQAAccessDrawBuffer": "kQAccessDrawBuffer",
    "kQAAccessDrawBufferEnd": "kQAccessDrawBufferEnd",
    "kQAAccessZBuffer": "kQAccessZBuffer",
    "kQAAccessZBufferEnd": "kQAccessZBufferEnd",
    "kQAClearDrawBuffer": "kQClearDrawBuffer",
    "kQAClearZBuffer": "kQClearZBuffer",
    "kQATextureFromContext": "kQTextureNewFromDrawContext",
    "kQABitmapFromContext": "kQBitmapNewFromDrawContext",
    "kQABusy": "kQBusy",
    "kQASwapBuffers": "kQSwapBuffers",
    "kQASubmitMultiTextureParams": "kQSubmitMultiTextureParams",
    "kTag_ZFunction": "kQATag_ZFunction",
    "kTag_ColorBG_a": "kQATag_ColorBG_a",
    "kTag_ColorBG_r": "kQATag_ColorBG_r",
    "kTag_ColorBG_g": "kQATag_ColorBG_g",
    "kTag_ColorBG_b": "kQATag_ColorBG_b",
    "kTag_Width": "kQATag_Width",
    "kTag_ZMinOffset": "kQATag_ZMinOffset",
    "kTag_ZMinScale": "kQATag_ZMinScale",
    "kTag_Blend": "kQATag_Blend",
    "kTag_ZBufferMask": "kQATag_ZBufferMask",
    "kTag_DepthBG": "kQATagGL_DepthBG",
}
# our own bookkeeping, not in the header
SKIP = {"kQAEngineMethodCount", "kQADrawMethodCount", "kTagCount", "SLOT_ENGINE_BASE", "SLOT_DRAW_BASE", "SLOT_GETMETHOD"}


def evaluate(expr):
    expr = expr.strip().rstrip("UL").strip()
    if not re.fullmatch(r"[0-9xXa-fA-F()<| ]+", expr):
        return None
    try:
        return eval(expr, {"__builtins__": {}}, {})
    except Exception:
        return None


def enumerators(text):
    out = {}
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*=\s*([^,}\n]+)", text):
        v = evaluate(m.group(2))
        if v is not None:
            out[m.group(1)] = v
    return out


def main():
    sdk = os.environ.get("RAVE_SDK_DIR")
    if not sdk:
        print("RAVE_SDK_DIR is not set. Point it at the folder holding Apple's RAVE.h and RAVESystem.h.")
        return 2
    hdr = {}
    for name in ("RAVE.h", "RAVESystem.h"):
        p = Path(sdk) / name
        if not p.exists():
            print("missing %s" % p)
            return 2
        hdr.update(enumerators(p.read_text(errors="replace")))
    cpp = RAVE_CPP.read_text()
    ours = enumerators("\n".join(re.findall(r"enum\s*\{[^}]*\}", cpp, flags=re.S)))
    bad = 0
    checked = 0
    for name, val in sorted(ours.items()):
        if name in SKIP or name.startswith("RAVE_"):
            continue
        theirs = ALIAS.get(name)
        if theirs is None:
            theirs = "kQATag_" + name[len("kTag_"):] if name.startswith("kTag_") else \
                     "kQATagGL_" + name[len("kTagGL_"):] if name.startswith("kTagGL_") else name
        if theirs not in hdr:
            print("not in header: %s (as %s)" % (name, theirs))
            bad += 1
            continue
        checked += 1
        if hdr[theirs] != val:
            print("MISMATCH %s: rave.cpp has %s, header %s has %s" % (name, val, theirs, hdr[theirs]))
            bad += 1
    print("checked %d constants against the header, %d problems" % (checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
