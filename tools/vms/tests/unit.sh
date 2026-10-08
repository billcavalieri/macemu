#!/bin/bash
# Unit tests for the control channel's and MCP server's pure code (no emulator, window or socket).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
D="$ROOT/SheepShaver/src/MacOSX/SheepApp/Control"
M="$ROOT/SheepShaver/src/MacOSX/SheepApp/MCP"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cp "$ROOT/tools/vms/tests/unit_tests.swift" "$OUT/main.swift"
swiftc -O -o "$OUT/control_tests" "$D/GuestPixels.swift" "$D/GuestInput.swift" "$D/VMControlProtocol.swift" "$M/MCPProtocol.swift" "$M/MCPHTTP.swift" "$ROOT/SheepShaver/src/MacOSX/SheepApp/LibraryImport.swift" "$OUT/main.swift"
"$OUT/control_tests"
