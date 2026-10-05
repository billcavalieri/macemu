#!/usr/bin/env python3
"""Check (or with --write, regenerate) the shader string embedded in sheepforce_metal.mm.

SheepForce.metal is the source of truth. The runtime fallback (newLibraryWithSource)
and the Xcode-built metallib must carry the same shaders; comments and whitespace
are ignored when comparing. `--write` rewrites SheepForceShaderSource() from the
.metal file so the two are never edited by hand twice.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAC = ROOT / "SheepShaver/src/MacOSX"


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def tokens(s):
    return re.findall(r"[A-Za-z_]\w*|\d+\.?\d*(?:[eE][+-]?\d+)?[fu]?|\S", strip_comments(s))


def embedded_source(mm):
    m = re.search(r'SheepForceShaderSource\(void\)\s*\{\s*return\s+@((?:\s*"(?:[^"\\]|\\.)*")+)\s*;', mm)
    if not m:
        sys.exit("check_sheepforce_shaders: cannot find SheepForceShaderSource()")
    out = []
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
        out.append(lit.encode().decode("unicode_escape"))
    return "".join(out)


EMBED_RE = re.compile(r'(SheepForceShaderSource\(void\)\s*\{\s*return\s+@)((?:\s*"(?:[^"\\]|\\.)*")+)(\s*;)')


def write_embedded():
    metal = strip_comments((MAC / "SheepForce.metal").read_text())
    lines = [ln.strip() for ln in metal.splitlines() if ln.strip()]
    lits = "\n".join('\t"%s\\n"' % ln.replace("\\", "\\\\").replace('"', '\\"') for ln in lines)
    mm_path = MAC / "sheepforce_metal.mm"
    mm = mm_path.read_text()
    new, n = EMBED_RE.subn(lambda m: m.group(1) + "\n" + lits + m.group(3), mm, count=1)
    if n != 1:
        sys.exit("check_sheepforce_shaders: cannot find SheepForceShaderSource()")
    mm_path.write_text(new)
    print("wrote %d shader lines into sheepforce_metal.mm" % len(lines))


def main():
    if "--write" in sys.argv:
        write_embedded()
    a = tokens((MAC / "SheepForce.metal").read_text())
    b = tokens(embedded_source((MAC / "sheepforce_metal.mm").read_text()))
    if a == b:
        print("ok: SheepForce.metal and the embedded shader source match (%d tokens)" % len(a))
        return 0
    n = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    print("MISMATCH at token %d:" % n)
    print("  .metal   :", " ".join(a[max(0, n - 6):n + 8]))
    print("  embedded :", " ".join(b[max(0, n - 6):n + 8]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
