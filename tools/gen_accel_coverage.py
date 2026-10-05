#!/usr/bin/env python3
"""Generate ACCEL-COVERAGE.md: where every accelerator hook and RAVE selector goes.

Rows come from the real names: the hook codes in SheepShaver/src/include/video_defs.h and the
engine methods, draw methods, tags, gestalt selectors and feature bits of Apple's RAVE headers
(RAVE.h, RAVESystem.h; an external SDK, point RAVE_SDK_DIR at it, never committed). Each name needs
a status in STATUS below; a name without one fails the run, so the list cannot silently go stale.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "ACCEL-COVERAGE.md"
REVIEW_BEGIN = "<!-- ACCEL-QUALIFICATION-BEGIN -->"
REVIEW_END = "<!-- ACCEL-QUALIFICATION-END -->"


def qualification_section(text):
    """Keep the manually maintained evidence separate from generated coverage."""
    if REVIEW_BEGIN not in text and REVIEW_END not in text:
        return ""
    if text.count(REVIEW_BEGIN) != 1 or text.count(REVIEW_END) != 1:
        raise ValueError("expected one complete accelerator qualification section")
    begin, end = text.index(REVIEW_BEGIN), text.index(REVIEW_END)
    if end < begin:
        raise ValueError("accelerator qualification markers are reversed")
    return text[begin:end + len(REVIEW_END)]

# (status, note). status: shader | state | host | declined | refused
STATUS = {}

def add(names, status, note):
    for n in names.split():
        STATUS[n] = (status, note)

# --- QuickDraw hook codes (video_defs.h) ---
add("ACCL_BITBLT", "shader", "srcCopy..notSrcBic, addOver, adMax, subOver, adMin (32-bit dest; 8/16/32-bit source), transparent (32-to-32); overlapping blits via GPU gather+scatter; no-copy framebuffer only, otherwise the CPU path")
add("ACCL_FILLRECT", "shader", "solid fill (mode 8) and invert (mode 10) with no clip regions; otherwise the CPU path")
add("ACCL_LINES", "shader", "solid pen-colour rectangles (frames, rules) when a reachable clip region is found and none can clip them; otherwise the ROM draws. On by default (pref sheepforce_lines); the gate also follows direct region pointers, verified by the probe (5160 lines decoded, 0 mismatches, 0 clipped leaks in 810 gated lines)")
add("ACCL_BLTMASK", "refused", "mask is a QuickDraw region at a caller-dependent offset and is not verified; the ROM keeps the operation (returns not handled)")
add("ACCL_FILLMASK", "shader", "EXPERIMENTAL, pref sheepforce_fillmask defaults off; non-black solid type-0 1-bit fill pattern (patCopy/notPatCopy) or (only with pref sheepforce_fillmask_tiles, default off; pixel-pattern draws under 4096 pixels refused; probe: 62 more exact ops/5.6 Mpx and 8 icon-sized mismatches/5 kpx before that rule) a type-1 PixPat whose 32-bit expansion (patXData, patXValid 32, tile up to 256x256, bounds at 0,0) is tiled from the destination pixmap origin (patCopy only; sf_filltile), transfer mode 8, 32-bit framebuffer destination; intersection of the overlapping regions reached through handles at the block offsets 0x1a4/0x1a8/0x1ac/0x1e0/0x20c (undecodable ones skipped; probe run: 435 exact ops / 19.5 Mpx vs 2 mismatches, both pen-12 icon dimming); pen-12 draws under 4096 pixels are refused; port visRgn/clipRgn are not added; sf_fillspans with CPU fallback. Black fills, non-solid/pixel patterns and other modes stay with the ROM. Interactive smoke evidence and remaining qualification gaps are recorded below")

# --- RAVE engine methods (RAVESystem.h, TQAEngineMethodTag) ---
add("kQADrawPrivateNew", "host", "creates a Metal draw context for 16/32-bit memory devices and GDevices; declines other devices and non-region clips")
add("kQADrawPrivateDelete kQAEngineCheckDevice kQAEngineGestalt", "host", "implemented")
add("kQATextureNew", "shader", "11 pixel types (RGB16, ARGB16, RGB32, ARGB32, CL4, CL8, RGB8_332, ARGB16_4444, ACL16_88, I8, AI16_88), mipmaps, flip origin; other types declined (kQANotSupported)")
add("kQATextureDetach", "host", "no-op: image data is copied at creation")
add("kQATextureDelete kQAColorTableNew kQAColorTableDelete kQATextureBindColorTable", "host", "implemented")
add("kQABitmapNew kQABitmapDetach kQABitmapDelete kQABitmapBindColorTable", "declined", "returns kQANotSupported; bitmap pixel types are not advertised")
add("kQAAccessTexture kQAAccessTextureEnd kQAAccessBitmap kQAAccessBitmapEnd", "declined", "returns kQANotSupported; the Access feature bits are not advertised")

# --- RAVE draw methods (RAVESystem.h, TQADrawMethodTag; the header's own spellings) ---
add("kQASetFloat kQASetInt kQASetPtr kQAGetFloat kQAGetInt kQAGetPtr", "state", "state tags are stored and read back")
add("kQADrawPoint kQADrawLine", "shader", "a quad of kQATag_Width pixels")
add("kQADrawTriGouraud kQADrawVGouraud kQASubmitVerticesGouraud kQADrawTriMeshGouraud", "shader", "flat/Gouraud, Z buffer, blend, fog, antialiasing")
add("kQADrawTriTexture kQADrawVTexture kQASubmitVerticesTexture kQADrawTriMeshTexture", "shader", "perspective-correct texturing with modulate/highlight/decal, filters, wrap/clamp")
add("kQSubmitMultiTextureParams", "shader", "second texture layer on submitted vertex arrays (add/modulate/blend-by-alpha/fixed)")
add("kQADrawBitmap", "declined", "no-op; bitmaps are not advertised")
add("kQARenderStart kQARenderEnd kQASync", "host", "RenderStart clears; RenderEnd and Sync render the queue and write the target (clipped to the context's region)")
add("kQAFlush kQSwapBuffers", "host", "no-op: drawing is queued until the frame ends")
add("kQARenderAbort", "host", "returns kQANoErr")
add("kQBusy", "host", "returns false")
add("kQASetNoticeMethod kQAGetNoticeMethod", "declined", "returns kQANotSupported")
add("kQAccessDrawBuffer kQAccessDrawBufferEnd kQAccessZBuffer kQAccessZBufferEnd kQClearDrawBuffer kQClearZBuffer kQTextureNewFromDrawContext kQBitmapNewFromDrawContext", "declined", "returns kQANotSupported; the matching feature bits are not advertised")

# --- tags ---
USED = "kQATag_ZFunction kQATag_ColorBG_a kQATag_ColorBG_r kQATag_ColorBG_g kQATag_ColorBG_b kQATag_Width kQATag_Antialias kQATag_Blend kQATag_TextureFilter kQATag_TextureOp kQATag_Texture kQATag_FogMode kQATag_FogColor_a kQATag_FogColor_r kQATag_FogColor_g kQATag_FogColor_b kQATag_FogStart kQATag_FogEnd kQATag_FogDensity kQATag_FogMaxDepth kQATag_ZBufferMask kQATag_MultiTextureEnable kQATag_MultiTextureOp kQATag_MultiTextureFilter kQATag_MultiTextureWrapU kQATag_MultiTextureWrapV kQATag_MultiTexture kQATag_MultiTextureFactor kQATagGL_TextureWrapU kQATagGL_TextureWrapV kQATagGL_DepthBG"
add(USED, "shader", "drives rendering")
add("kQATag_ZMinOffset kQATag_ZMinScale", "state", "read-only; returns 0 and 1")
add("kQATag_MultiTextureCurrent", "state", "stored; layer 1 is the only secondary layer")

OPTIONAL_ON = "kQAOptional_DeepZ kQAOptional_Texture kQAOptional_TextureHQ kQAOptional_TextureColor kQAOptional_Blend kQAOptional_BlendAlpha kQAOptional_Antialias kQAOptional_CL4 kQAOptional_CL8 kQAOptional_NoDither kQAOptional_FogDepth kQAOptional_MultiTextures"
FAST_ON = "kQAFast_Line kQAFast_Gouraud kQAFast_Texture kQAFast_Blend kQAFast_Antialiasing kQAFast_FogDepth kQAFast_MultiTextures"


def enumerators(block):
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.S)
    return re.findall(r"\b([A-Za-z_]\w*)\s*=", block)


def named_enum(text, name):
    m = re.search(r"enum\s+%s\s*\{(.*?)\}" % name, text, flags=re.S)
    return enumerators(m.group(1)) if m else []


def prefixed(text, prefix):
    out = []
    for m in re.finditer(r"enum\s*\{(.*?)\}", text, flags=re.S):
        out += [n for n in enumerators(m.group(1)) if n.startswith(prefix)]
    return out


def main():
    sdk = os.environ.get("RAVE_SDK_DIR")
    if not sdk:
        print("RAVE_SDK_DIR is not set. Point it at the folder holding Apple's RAVE.h and RAVESystem.h.")
        return 2
    rave = (Path(sdk) / "RAVE.h").read_text(errors="replace")
    system = (Path(sdk) / "RAVESystem.h").read_text(errors="replace")
    vd = (ROOT / "SheepShaver/src/include/video_defs.h").read_text()
    m = re.search(r"enum\s*\{\s*(ACCL_BITBLT.*?)\};", vd, flags=re.S)
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", m.group(1), flags=re.S) if m else ""
    hooks = [x.split("=")[0].strip() for x in body.split(",") if x.strip()]
    groups = [
        ("QuickDraw accelerator hooks (video_defs.h)", hooks, None),
        ("RAVE engine methods (TQAEngineMethodTag)", named_enum(system, "TQAEngineMethodTag"), None),
        ("RAVE draw-context methods (TQADrawMethodTag)", named_enum(system, "TQADrawMethodTag"), None),
        ("State tags: integer (TQATagInt)", [n for n in named_enum(rave, "TQATagInt") if n != "kQATag_EngineSpecific_Minimum"], "stored"),
        ("State tags: pointer (TQATagPtr)", named_enum(rave, "TQATagPtr"), "stored"),
        ("State tags: float (TQATagFloat)", named_enum(rave, "TQATagFloat"), "stored"),
    ]
    lines = ["# Accelerator coverage", "",
             "Generated by `tools/gen_accel_coverage.py` (needs `RAVE_SDK_DIR`). Every hook code and RAVE name below",
             "either reaches a Metal shader, is handled on the host, or is declined on purpose with the status shown.", ""]
    qualification = qualification_section(OUT.read_text()) if OUT.exists() else ""
    if qualification:
        lines += [qualification, ""]
    missing = []
    for title, names, default in groups:
        lines += ["## " + title, "", "| Name | Status | What happens |", "|---|---|---|"]
        for n in names:
            st = STATUS.get(n)
            if st is None and default == "stored":
                st = ("state", "stored and read back; no effect (the feature is not advertised, so conforming clients do not rely on it)")
            if st is None:
                missing.append(n)
                st = ("MISSING", "")
            lines.append("| `%s` | %s | %s |" % (n, st[0], st[1]))
        lines.append("")
    for title, names, on in (("Optional features (kQAOptional_xxx, kQAGestalt_OptionalFeatures)", prefixed(rave, "kQAOptional_"), OPTIONAL_ON),
                             ("Fast features (kQAFast_xxx, kQAGestalt_FastFeatures)", prefixed(rave, "kQAFast_"), FAST_ON)):
        lines += ["## " + title, "", "| Bit | Advertised |", "|---|---|"]
        for n in names:
            if n.endswith("_None"):
                continue
            lines.append("| `%s` | %s |" % (n, "yes, implemented and tested" if n in on.split() else "no"))
        lines.append("")
    lines += ["## Gestalt selectors", "",
              "All of `TQAGestaltSelector` is answered by `engine_gestalt` in `rave.cpp`; a selector outside the header returns `kQAGestaltUnknown`.", ""]
    if missing:
        print("names without a status row: " + ", ".join(missing))
        return 1
    OUT.write_text("\n".join(lines))
    print("wrote %s (%d rows)" % (OUT.name, sum(len(g[1]) for g in groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
