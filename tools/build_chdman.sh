#!/bin/bash
# Builds MAME's chdman from the pinned third_party/mame tree (for creating and verifying CHD images in the tests; the
# app itself does not use it). Output: build/chdlib/chdman. Needs tools/build_chdlib.sh to have run (it reuses its objects).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
M="$ROOT/third_party/mame"
OUT="${SSCHD_OUT:-$ROOT/build/chdlib}"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
"$ROOT/tools/build_chdlib.sh" >/dev/null
[ -f "$M/src/tools/chdman.cpp" ] || git -C "$M" sparse-checkout add /src/tools/chdman.cpp
[ -x "$OUT/chdman" ] && [ "$OUT/chdman" -nt "$ROOT/tools/build_chdman.sh" ] && [ "$OUT/chdman" -nt "$OUT/libsschd.a" ] && { echo "chdman: up to date"; exit 0; }
ZSTD="$(pkg-config --variable=prefix libzstd)"
TARGET="$(cat "$OUT/.deployment-target")"
CXXF="-arch arm64 -mmacosx-version-min=$TARGET -O2 -DNDEBUG -w -std=c++20 -DLSB_FIRST -DPTR64=1 -DCRLF=2 -I$M/src/lib/util -I$M/src/osd -I$M/3rdparty -I$M/3rdparty/utf8proc -I$M/3rdparty/flac/include -I$ZSTD/include"
mkdir -p "$OUT/tool"
for f in src/tools/chdman src/lib/util/aviio src/lib/util/vbiparse src/lib/util/harddisk; do
    clang++ $CXXF -c "$M/$f.cpp" -o "$OUT/tool/$(basename "$f").o"
done
# libsschd.a also holds the shim; chdman does not use it, the linker takes only what is needed
cat > "$OUT/tool/stubs.cpp" <<'EOS'
extern const char *const build_version;
const char *const build_version = "ssmame0289";
EOS
clang++ $CXXF -c "$OUT/tool/stubs.cpp" -o "$OUT/tool/stubs.o"
clang++ -arch arm64 "$OUT"/tool/*.o "$OUT/libsschd.a" "$ZSTD/lib/libzstd.a" -lz -o "$OUT/chdman"
echo "chdman: $OUT/chdman"
