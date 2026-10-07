#!/bin/bash
# Builds MAME's CHD file code (util/chd.cpp, chdcodec.cpp and what they pull in: huffman, FLAC, LZMA, zstd, zlib,
# hashing, core_file) as a static library for SheepShaver, from the pinned submodule third_party/mame.
#
# Only the CHD pieces are compiled, not the rest of MAME. The submodule is a sparse checkout (src/lib, a few osd
# headers, 3rdparty/{lzma,flac,utf8proc}); this script sets that up when needed.
#
# Outputs, in $OUT (default build/chdlib):
#   libsschd.a     the CHD library plus the shim (chd_shim.cpp)
#   sschd.rsp      link response file (@file): the archive plus zstd and the C++ runtime
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
M="$ROOT/third_party/mame"
OUT="${SSCHD_OUT:-$ROOT/build/chdlib}"
SHIM="$ROOT/BasiliskII/src/Unix/chd_shim.cpp"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

if [ ! -d "$M/.git" ] && [ ! -f "$M/.git" ]; then
    echo "error: $M is empty. Run: git submodule update --init --depth 1 third_party/mame" >&2
    exit 1
fi
# The sparse set (idempotent; a fresh clone of the submodule has the whole tree, which is much larger).
SPARSE=(src/lib/ /src/osd/eminline.h /src/osd/eigcc.h /src/osd/eigccarm.h /src/osd/eigccppc.h /src/osd/eigccx86.h
        /src/osd/osdcore.h /src/osd/osdcore.cpp /src/osd/osdcomm.h /src/osd/osdfile.h /src/osd/strconv.h
        /src/osd/strconv.cpp /src/osd/osdsync.h /src/osd/osdsync.cpp /src/tools/chdman.cpp src/osd/modules/file/ 3rdparty/lzma/ 3rdparty/flac/ 3rdparty/utf8proc/)
if ! git -C "$M" sparse-checkout list 2>/dev/null | grep -q '3rdparty/utf8proc'; then
    git -C "$M" sparse-checkout set --no-cone "${SPARSE[@]}"
fi

ZSTD="$(pkg-config --variable=prefix libzstd)"
[ -f "$ZSTD/lib/libzstd.a" ] || { echo "error: $ZSTD/lib/libzstd.a missing (brew install zstd)" >&2; exit 1; }

TARGET="${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion | cut -d. -f1-2)}"
mkdir -p "$OUT"
cd "$OUT"
if [ -f libsschd.a ] && [ -f sschd.rsp ] && [ "$(cat .deployment-target 2>/dev/null)" = "$TARGET" ] \
   && [ libsschd.a -nt "$SHIM" ] && [ libsschd.a -nt "$ROOT/tools/build_chdlib.sh" ] \
   && [ libsschd.a -nt "$ROOT/BasiliskII/src/Unix/chd_shim.h" ]; then
    echo "sschd: up to date"
    exit 0
fi
rm -rf obj; mkdir obj
echo "$TARGET" > .deployment-target

COMMON="-arch arm64 -mmacosx-version-min=$TARGET -O2 -DNDEBUG -w -fno-strict-aliasing"
CXXF="$COMMON -std=c++20 -DLSB_FIRST -DPTR64=1 -DCRLF=2 -I$M/src/lib/util -I$M/src/osd -I$M/3rdparty -I$M/3rdparty/utf8proc -I$M/3rdparty/flac/include -I$ZSTD/include"
FLACD="-DHAVE_CONFIG_H -DENABLE_64_BIT_WORDS=1 -DOGG_FOUND=0 -DFLAC__HAS_OGG=0 -DHAVE_LROUND=1 -DHAVE_INTTYPES_H -DHAVE_STDBOOL_H -DHAVE_STDINT_H -DHAVE_STDIO_H -DHAVE_STDLIB_H -DHAVE_STRING_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DCPU_IS_BIG_ENDIAN=0 -DCPU_IS_LITTLE_ENDIAN=1 -DWORDS_BIGENDIAN=0 -DFLAC__SYS_DARWIN -DHAVE_BSWAP16 -DHAVE_BSWAP32"
FLACI="-I$M/3rdparty/flac/src/libFLAC/include -I$M/3rdparty/flac/include"

UTIL="chd chdcodec huffman flac avhuff cdrom hashing md5 corefile ioprocs ioprocsfilter strformat corealloc corestr bitmap coreutil unicode path vecstream palette"
for f in $UTIL; do clang++ $CXXF -c "$M/src/lib/util/$f.cpp" -o "obj/util_$f.o"; done
for f in src/osd/osdcore src/osd/osdsync src/osd/strconv src/osd/modules/file/posixfile src/osd/modules/file/posixptty src/osd/modules/file/posixsocket; do clang++ $CXXF -c "$M/$f.cpp" -o "obj/osd_$(basename $f).o"; done

# utf8proc (unicode.cpp)
clang -arch arm64 -mmacosx-version-min=$TARGET -O2 -w -DUTF8PROC_STATIC -c "$M/3rdparty/utf8proc/utf8proc.c" -o obj/utf8proc.o

# LZMA: encoder and decoder
for f in LzmaDec LzmaEnc LzFind LzFindOpt Alloc CpuArch; do
    clang $COMMON -DZ7_ST -c "$M/3rdparty/lzma/C/$f.c" -o "obj/lzma_$f.o"
done

# FLAC: encoder and decoder (the CD-audio codecs of CHD use it)
for f in bitmath bitreader bitwriter cpu crc fixed float format lpc lpc_intrin_neon md5 memory stream_decoder stream_encoder stream_encoder_framing window; do
    clang $COMMON $FLACD $FLACI -c "$M/3rdparty/flac/src/libFLAC/$f.c" -o "obj/flac_$f.o"
done

clang++ $CXXF -I"$ROOT/BasiliskII/src/Unix" -c "$SHIM" -o obj/chd_shim.o

rm -f libsschd.a
libtool -static -o libsschd.a obj/*.o 2> libtool.log
{
    echo "$OUT/libsschd.a"
    echo "$ZSTD/lib/libzstd.a"
    echo "-lz"
    echo "-lc++"
} > sschd.rsp.tmp
cmp -s sschd.rsp.tmp sschd.rsp 2>/dev/null && rm -f sschd.rsp.tmp || mv -f sschd.rsp.tmp sschd.rsp
echo "sschd: $OUT/libsschd.a ($(du -h libsschd.a | cut -f1))"
