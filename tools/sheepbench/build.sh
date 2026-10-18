#!/bin/bash
# Builds SheepBench (guest/sheepbench) with Retro68 (not part of this repository) and from it the disk the SheepShaver
# app bundles.
#   tools/sheepbench/build.sh              everything; also refreshes SheepShaver/src/MacOSX/SheepApp/SheepBench.hfv
#                                          and guest/dist/SheepBench-extfs.zip
#   tools/sheepbench/build.sh --apps-only  just the application, in build/sheepbench (the tests use this)
#   tools/sheepbench/build.sh --if-needed  what the Xcode build runs: does nothing when the .hfv is newer than every
#                                          source, and only notes (never fails) when Retro68 is not installed
#   RETRO68_PREFIX   the Retro68 toolchain, which also provides hfsutils (default ~/Retro68-build/toolchain)
# The refreshed .hfv is committed so a machine without Retro68 can still build the SheepShaver app; commit the new .hfv
# after changing something under guest/sheepbench. Intermediate output is in build/sheepbench (ignored by git).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="${RETRO68_PREFIX:-$HOME/Retro68-build/toolchain}"
TOOLCHAIN="$PREFIX/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
HFV="$ROOT/SheepShaver/src/MacOSX/SheepApp/SheepBench.hfv"
ZIP="$ROOT/guest/dist/SheepBench-extfs.zip"
if [ "${1:-}" = "--if-needed" ]; then
    if [ ! -f "$TOOLCHAIN" ]; then
        echo "note: Retro68 not found at $PREFIX; using the committed SheepBench.hfv (see guest/README.md)"
        exit 0
    fi
    if [ -f "$HFV" ] && [ -f "$ZIP" ] && [ -z "$(find "$ROOT/guest/sheepbench" "$ROOT/tools/sheepbench/build.sh" -type f -newer "$HFV" | head -1)" ]; then
        exit 0
    fi
    echo "note: building SheepBench and its disk"
fi
[ -f "$TOOLCHAIN" ] || { echo "error: $TOOLCHAIN not found. Build Retro68 and set RETRO68_PREFIX (see guest/README.md)." >&2; exit 1; }
export PATH="$PREFIX/bin:$PATH"
OUT="$ROOT/build/sheepbench"
mkdir -p "$OUT"
cmake -S "$ROOT/guest/sheepbench" -B "$OUT" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" > "$OUT.cmake.log" 2>&1 || { cat "$OUT.cmake.log" >&2; exit 1; }
cmake --build "$OUT" > "$OUT.build.log" 2>&1 || { cat "$OUT.build.log" >&2; exit 1; }
[ "${1:-}" = "--apps-only" ] && exit 0

IMG="$OUT/SheepBench.hfv"
README="$OUT/ReadMe.txt"
cat > "$README" <<'TXT'
SheepBench
==========

SheepBench measures Mac OS 9 running in SheepShaver: the PowerPC and 68k processors, memory, the MMU, QuickDraw,
3D (RAVE) and the disk. Instead of a comparison with a G3 it reports what matters for an emulator:

  - raw numbers: ns per instruction, MB/s, operations, triangles and frames per second;
  - JIT speedup: each test with the JIT on and off;
  - percent of native: the same C code timed on the Mac itself;
  - GPU speedup: QuickDraw with SheepForce on and off.

Copy SheepBench to your hard disk and open it. Pick the suites and click Run. File > Save Report writes a text file
(tab-separated); File > Compare Reports puts two of them side by side.

The JIT and GPU comparisons switch the emulator while it runs. They need "Allow SheepBench to change JIT and
SheepForce while running" in the virtual machine's settings (Advanced); otherwise SheepBench measures the current
settings only. On a Mac or emulator without SheepBench support it still runs and reports the host fields as unknown.
TXT

rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1024 count=1440 2> /dev/null
hformat -f -l "SheepBench" "$IMG" > /dev/null
hmount "$IMG" > /dev/null
hcopy -m "$OUT/SheepBench.bin" ":"
hcopy -t "$README" ":ReadMe"
hattrib -t TEXT -c ttxt ":ReadMe"
[ "${1:-}" = "--if-needed" ] || hls -l
humount > /dev/null
cp "$IMG" "$HFV"
# The same as a folder for SheepShaver's extfs (a Mac folder shared with the guest), zipped; see make_extfs.py.
"$ROOT/tools/shears/make_extfs.py" --top SheepBench --file "SheepBench.bin=SheepBench" "$ZIP" "$OUT" > /dev/null
[ "${1:-}" = "--if-needed" ] || ls -l "$IMG" "$ZIP"
