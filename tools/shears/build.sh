#!/bin/bash
# Builds the Sheep Shears guest side with Retro68 (not part of this repository): the tool, the control panel and the
# installer, and from them the installer disk the SheepShaver app bundles.
#   tools/shears/build.sh              everything; also refreshes SheepShaver/src/MacOSX/SheepApp/SheepShearsInstaller.hfv
#                                      and guest/dist/SheepShears-extfs.zip
#   tools/shears/build.sh --apps-only  just the three applications (the tests use this)
#   tools/shears/build.sh --if-needed  what the Xcode build runs: does nothing when the .hfv is newer than every guest
#                                      source, and only notes (never fails) when Retro68 is not installed
#   RETRO68_PREFIX   the Retro68 toolchain, which also provides hfsutils (default ~/Retro68-build/toolchain)
# The refreshed .hfv is committed so a machine without Retro68 can still build the SheepShaver app; commit the new .hfv
# after changing something under guest/. Intermediate output is in build/sheepshears (ignored by git).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="${RETRO68_PREFIX:-$HOME/Retro68-build/toolchain}"
TOOLCHAIN="$PREFIX/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
HFV="$ROOT/SheepShaver/src/MacOSX/SheepApp/SheepShearsInstaller.hfv"
if [ "${1:-}" = "--if-needed" ]; then
    if [ ! -f "$TOOLCHAIN" ]; then
        echo "note: Retro68 not found at $PREFIX; using the committed SheepShearsInstaller.hfv (see guest/README.md)"
        exit 0
    fi
    if [ -f "$HFV" ] && [ -f "$ROOT/guest/dist/SheepShears-extfs.zip" ] && [ -z "$(find "$ROOT/guest/sheepshears" "$ROOT/tools/shears/build.sh" -type f -newer "$HFV" | head -1)" ]; then
        exit 0
    fi
    echo "note: building the Sheep Shears guest tools and installer disk"
fi
[ -f "$TOOLCHAIN" ] || { echo "error: $TOOLCHAIN not found. Build Retro68 and set RETRO68_PREFIX (see SHEEP-SHEARS-PLAN.md)." >&2; exit 1; }
export PATH="$PREFIX/bin:$PATH"
OUT="$ROOT/build/sheepshears"
mkdir -p "$OUT"
cmake -S "$ROOT/guest/sheepshears" -B "$OUT" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -Wno-dev > "$OUT.cmake.log" 2>&1 || { cat "$OUT.cmake.log" >&2; exit 1; }
cmake --build "$OUT" > "$OUT.build.log" 2>&1 || { cat "$OUT.build.log" >&2; exit 1; }
[ "${1:-}" = "--apps-only" ] && exit 0

IMG="$OUT/SheepShearsInstaller.hfv"
README="$OUT/ReadMe.txt"

cat > "$README" <<'TXT'
Sheep Shears
============

Sheep Shears links Mac OS 9 running in SheepShaver with the Mac that hosts it:

  - The mouse is released at the edge of the screen, so you can go back to the Mac without a keyboard shortcut.
  - The clipboard is shared in both directions.
  - SheepShaver can shut Mac OS down properly (Guest > Shut Down Guest).

To install, open "Install Sheep Shears" and click Install. It puts the tool "Sheep Shears Tool" into Startup Items
and the control panel "Sheep Shears" into Control Panels, and starts the tool. No restart is needed.

Open the Sheep Shears control panel to switch the mouse release or the clipboard sharing off. Both are on
by default. "Remove" in the installer takes everything out again.

Sheep Shears only runs in SheepShaver; elsewhere it does nothing.
TXT

rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1024 count=1440 2> /dev/null
hformat -f -l "Sheep Shears" "$IMG" > /dev/null
hmount "$IMG" > /dev/null
hcopy -m "$OUT/SheepShearsInstaller.bin" ":"
hrename "SheepShearsInstaller" "Install Sheep Shears"
hmkdir "Sheep Shears Files"
hcopy -m "$OUT/SheepShears.bin" ":Sheep Shears Files:"
hrename ":Sheep Shears Files:SheepShears" ":Sheep Shears Files:Sheep Shears Tool"
hcopy -m "$OUT/SheepShearsPanel.bin" ":Sheep Shears Files:"
hrename ":Sheep Shears Files:SheepShearsPanel" ":Sheep Shears Files:Sheep Shears"
hattrib -t APPC ":Sheep Shears Files:Sheep Shears"      # a control panel application
hcopy -t "$README" ":ReadMe"
hattrib -t TEXT -c ttxt ":ReadMe"
[ "${1:-}" = "--if-needed" ] || hls -lR
humount > /dev/null
cp "$IMG" "$HFV"
# The same programs as a folder for SheepShaver's extfs (a Mac folder shared with the guest), zipped; see make_extfs.py.
"$ROOT/tools/shears/make_extfs.py" "$ROOT/guest/dist/SheepShears-extfs.zip" "$OUT" > /dev/null
[ "${1:-}" = "--if-needed" ] || ls -l "$IMG"
