#!/bin/bash
# Builds QEMU's block layer (qcow v1 via block/qcow.c, qcow2/qcow3 via block/qcow2.c, plus file, raw, zlib, zstd) as a
# static archive for SheepShaver, from the pinned submodule third_party/qemu.
#
# It builds the same objects `qemu-img` links (qemu-img.c itself dropped), so the set follows QEMU's own meson
# configuration, then compiles BasiliskII/src/Unix/qcow_shim.c with QEMU's flags and merges everything.
#
# Outputs, in $OUT (default build/qemu-blocklib):
#   libssqcow.a    qemublock.o (all block-layer objects plus the shim, one relocatable object so that the module
#                  constructors that register the block drivers always come along) and libqemuutil.a members
#   ssqcow.rsp     link response file (@file): the archive plus the static glib/zstd/... archives and system libraries
#
# Idempotent and quick when nothing changed. Needs: ninja, meson's prerequisites, pkg-config, glib and zstd from
# Homebrew (static archives are used, so nothing has to be embedded into the app).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/qemu"
OUT="${SSQCOW_OUT:-$ROOT/build/qemu-blocklib}"
SHIM="$ROOT/BasiliskII/src/Unix/qcow_shim.c"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

if [ ! -f "$SRC/configure" ]; then
    echo "error: $SRC is empty. Run: git submodule update --init --depth 1 third_party/qemu" >&2
    exit 1
fi
for tool in ninja pkg-config; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found (brew install ninja pkg-config meson glib zstd)" >&2; exit 1; }
done

# Build for the app's deployment target (Xcode exports it); a change of target rebuilds from scratch.
TARGET="${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion | cut -d. -f1-2)}"
export MACOSX_DEPLOYMENT_TARGET="$TARGET"
if [ -f "$OUT/.deployment-target" ] && [ "$(cat "$OUT/.deployment-target")" != "$TARGET" ]; then
    rm -rf "$OUT"
fi
mkdir -p "$OUT"
cd "$OUT"
echo "$TARGET" > .deployment-target

if [ ! -f build.ninja ]; then
    "$SRC/configure" \
        --disable-system --disable-user --enable-tools --disable-docs --disable-guest-agent --disable-plugins \
        --enable-zstd --disable-gnutls --disable-nettle --disable-gcrypt --disable-libssh --disable-curl \
        --disable-vnc --disable-sdl --disable-gtk --disable-cocoa --disable-slirp --disable-werror \
        --with-coroutine=sigaltstack --target-list= > configure.log 2>&1 \
        || { tail -30 configure.log >&2; exit 1; }
fi

ninja qemu-img > ninja.log 2>&1 || { tail -30 ninja.log >&2; exit 1; }

if [ -f libssqcow.a ] && [ -f ssqcow.rsp ] && [ libssqcow.a -nt qemu-img ] && [ libssqcow.a -nt "$SHIM" ] \
   && [ libssqcow.a -nt "$ROOT/tools/build_qemu_blocklib.sh" ]; then
    echo "ssqcow: up to date"
    exit 0
fi

# The link line of qemu-img: objects, archives, libraries.
LINK="$(ninja -t commands qemu-img | tail -1)"
OBJS=() ; ARCHIVES=()
for word in $LINK; do
    case "$word" in
        qemu-img.p/*) ;;                               # the tool's own main
        *.o) OBJS+=("$word") ;;
        lib*.a) ARCHIVES+=("$word") ;;
    esac
done
# The two drivers the app depends on must be there.
for need in block_qcow.c.o block_qcow2.c.o block_file-posix.c.o block_raw-format.c.o; do
    printf '%s\n' "${OBJS[@]}" | grep -q "$need" || { echo "error: $need is not on the qemu-img link line" >&2; exit 1; }
done

# The shim, with the compile flags QEMU uses for block/qcow.c.
CC_CMD="$(ninja -t commands libblock.a.p/block_qcow.c.o | tail -1)"
SHIM_CMD="$(python3 - "$CC_CMD" "$SHIM" "$ROOT/BasiliskII/src/Unix" <<'PY'
import shlex, sys
cmd = shlex.split(sys.argv[1]); shim, inc = sys.argv[2], sys.argv[3]
out, i = [], 0
while i < len(cmd):
    a = cmd[i]
    if a in ("-MD", "-MQ", "-MF"):
        i += 2 if a != "-MD" else 1
        continue
    if a == "-o":
        i += 2; continue
    if a == "-c":
        i += 2; continue
    out.append(a); i += 1
out += ["-I" + inc, "-o", "qcow_shim.o", "-c", shim]
print(" ".join(shlex.quote(x) for x in out))
PY
)"
eval "$SHIM_CMD"

ld -r -arch arm64 -o qemublock.o "${OBJS[@]}" qcow_shim.o
rm -f libssqcow.a
libtool -static -o libssqcow.a qemublock.o "${ARCHIVES[@]}" 2> libtool.log

# Static dependency archives: the same libraries the qemu-img link uses, as .a files.
libdir() { pkg-config --variable=libdir "$1"; }
GLIB="$(libdir glib-2.0)"
LIBS=()
for a in "$GLIB/libglib-2.0.a" "$GLIB/libgmodule-2.0.a" \
         "$(libdir libzstd)/libzstd.a" "$(libdir libpcre2-8)/libpcre2-8.a" "$(libdir pixman-1)/libpixman-1.a" \
         "$(brew --prefix gettext 2>/dev/null || echo /opt/homebrew/opt/gettext)/lib/libintl.a"; do
    [ -f "$a" ] || { echo "error: missing static archive $a" >&2; exit 1; }
    LIBS+=("$a")
done

# Response file for the app link (clang expands @file): the archive, its static dependencies, system libraries.
{
    echo "$OUT/libssqcow.a"
    printf '%s\n' "${LIBS[@]}"
    printf '%s\n' -lz -lbz2 -lm -liconv -framework CoreFoundation -framework IOKit -framework Foundation -framework CoreServices
} > ssqcow.rsp.tmp
cmp -s ssqcow.rsp.tmp ssqcow.rsp && rm -f ssqcow.rsp.tmp || mv -f ssqcow.rsp.tmp ssqcow.rsp
echo "ssqcow: $OUT/libssqcow.a ($(du -h libssqcow.a | cut -f1))"
