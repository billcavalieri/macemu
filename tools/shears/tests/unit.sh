#!/bin/bash
# Unit tests for the Sheep Shears host protocol and the edge-release rule. Compiles the two pure Swift files with the
# test driver; no emulator, window or mouse is involved.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
D="$ROOT/SheepShaver/src/MacOSX/SheepApp/Shears"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT"
cp "$ROOT/tools/shears/tests/unit_tests.swift" "$OUT/main.swift"
swiftc -O -o "$OUT/shears_tests" "$D/ShearsProtocol.swift" "$D/ShearsPointer.swift" "$D/ShearsSession.swift" "$D/ShearsInstaller.swift" "$OUT/main.swift"
"$OUT/shears_tests"
