# Disk image formats in the SheepShaver Xcode build

The Xcode app target opens hard-disk images through `Sys_open` (`BasiliskII/src/Unix/sys_unix.cpp`). Raw files, sparsebundles and (optionally) VHD were already supported. This build adds qcow and CHD. They are compiled only into the **Xcode SheepShaver app target** (`HAVE_QCOW2`, `HAVE_CHD`); the Unix `Makefile.in` / `configure.ac` builds are unchanged. Raw images are still the fastest choice for heavy I/O.

The prefs key is unchanged: `disk <path>`, with a leading `*` for read-only. The file type is detected from the file's first bytes, not from its extension (`.qcow`, `.qcow2`, `.qcow3` and `.chd` are only conventions). A file whose magic matches a format but cannot be opened is refused (the disk is not mounted at all), never mounted as a raw disk.

## qcow (v1, v2, v3)

QEMU's own block drivers do the work (`block/qcow.c` for header version 1, `block/qcow2.c` for versions 2 and 3, with the file, raw, zlib and zstd support they use). Reads and writes are read/write for all three versions. The code is the pinned QEMU submodule built into a static library; nothing from QEMU's headers is included by SheepShaver, and the QEMU work runs on its own thread.

### Building

```
brew install glib zstd ninja pkg-config meson qemu     # qemu only for qemu-img (creating and checking images)
git submodule update --init --depth 1 third_party/qemu
xcodebuild -project SheepShaver/src/MacOSX/SheepShaver.xcodeproj -scheme SheepShaver -configuration Release
```

The first build compiles QEMU's block layer (a few minutes, `tools/build_qemu_blocklib.sh`, output in `build/qemu-blocklib`, ignored by git); later builds only check that it is current. The libraries it needs (glib, zstd, pcre2, pixman, gettext) are linked statically, so nothing has to be embedded in the app bundle. Only the arm64 slice has qcow support.

### Creating images

```
qemu-img create -f qcow  disk.qcow  2G                                  # version 1
qemu-img create -f qcow2 -o compat=0.10 disk.qcow2 2G                   # version 2
qemu-img create -f qcow2 -o compat=1.1  disk.qcow3 2G                   # version 3 ("qcow3")
qemu-img convert -f raw -O qcow2 -c -o compression_type=zlib disk.hfv disk.qcow2   # compressed
```

Then `disk /path/to/disk.qcow2` in the prefs. A converted DiskCopy 400K/800K image keeps the same header handling as a raw file.

### Compression and writes

Compression follows the QEMU driver: qcow v1 compressed clusters are zlib; qcow2/qcow3 clusters are zlib, and zstd on version 3 (`compression_type=zstd`). A guest write that touches a compressed cluster is handled by QEMU: it decompresses and allocates a normal cluster, and the image stays consistent for `qemu-img check`. Data reaches the file when the image is closed and, as a safety net, about a second after the last guest write.

### Backing files

An overlay made with `qemu-img create -f qcow2 -b base.qcow2 -F qcow2 overlay.qcow2` works as it does in QEMU: the base is opened as the overlay's backing file, reads of clusters the overlay has not allocated come from the base (and from the base's own backing file, for chains of any depth), and guest writes go to the overlay only. A write that covers part of an unallocated cluster copies the rest of the cluster from the base first. Backing files are opened read-only and are never modified.

- The path stored in the overlay is resolved relative to the overlay's directory when it is not absolute. The format is the one the overlay names (`-F`); without one QEMU probes the file.
- If the backing file cannot be used (missing, moved, damaged, not a supported format, encrypted, a loop in the chain), SheepShaver does **not** start. It logs `ERROR: Cannot start: the qcow image <path> needs a backing file that cannot be used: ...` and shows the same text as an error alert. Mounting the overlay without its base would present a different disk, so there is no fallback.
- The overlay and the base must stay together: QEMU does not detect a base that was changed after the overlay was made, and the overlay then reads inconsistent data. `qemu-img rebase -u` repoints an overlay whose base was moved; `qemu-img commit` merges the overlay into the base; `qemu-img convert` flattens a chain into one image.
- A successful open logs the chain: `qcow image <path> opens with its backing chain: overlay (qcow2) -> base (qcow2)`.

### Limits

- No snapshot selection (the active image state is always used).
- No AES or LUKS: encrypted images are refused (this QEMU build has crypto off).
- Xcode app target only, arm64 only. Raw images remain faster for heavy I/O.
- QEMU's start-up may change the process's fault signal handlers; the shim saves SIGSEGV/SIGBUS/SIGILL before it and restores them after, so the emulator's own handlers stay in place.

### Tests

`python3 tools/run_qcow_tests.py` writes through the same shim into v1, v2 and v3 images (plain and compressed), then checks them with `qemu-img check` and an independent `qemu-img convert` to raw, and checks backing chains (reads fall through, writes stay in the overlay and the base is byte-identical afterwards, relative paths, a chain three deep, raw and compressed bases), that a missing, unusable or looping backing file fails with the backing-chain code, and that encrypted images are refused.

## CHD (hard disks)

MAME's own `chd_file` code (pinned `third_party/mame`, a sparse shallow submodule, built by `tools/build_chdlib.sh` into `build/chdlib/libsschd.a`; only the CHD pieces and their codecs: zlib, LZMA, FLAC, Huffman, zstd) opens hard-disk CHDs. The logical size is the hunk count times the hunk size, and the hunk size must be a multiple of 512. One hunk is cached; a guest sector write reads, patches and writes back its hunk.

```
brew install zstd pkg-config                      # as for qcow
git submodule update --init --depth 1 third_party/mame   # the build script reduces it to the CHD sources
chdman createhd -i disk.hfv -o disk.chd -c none           # uncompressed: written in place
chdman createhd -i disk.hfv -o disk.chd                   # compressed (default codecs): writes go to disk.chd.ssdiff.chd
```

`chdman` is MAME's tool (`brew install rom-tools`, or `tools/build_chdman.sh`, which builds it from the same pinned tree into `build/chdlib/chdman`). Then `disk /path/to/disk.chd` in the prefs.

| File | How it opens |
| --- | --- |
| version 5, uncompressed | read/write, in place (MAME's `write_hunk`) |
| version 5, compressed, not read-only | the original stays untouched and becomes the parent of an uncompressed version 5 diff `<path>.ssdiff.chd`, created on first open and reused later when its parent hash matches (a diff that belongs to another version of the image makes the open fail instead of being replaced) |
| version 5, compressed, `*` prefix | read-only on the original, no diff |
| version 3 and 4 | read-only (MAME only writes version 5) |
| version 1 and 2 | refused |
| CD, GD-ROM, DVD or A/V CHD; a file that already has a parent | refused |

MAME's `write_hunk` skips an all-zero hunk it has not allocated; in a diff that would leave the parent's old data visible, so the shim allocates the hunk first. Check a written image with `chdman verify -i disk.chd`; to see the guest's changes on a compressed image, `chdman extracthd -i disk.chd.ssdiff.chd -ip disk.chd -o out.raw`. Do not edit the original while a diff exists: the diff refuses to open against a changed parent.

### Tests

`python3 tools/run_chd_tests.py` writes through the same shim into uncompressed and compressed (zlib, lzma, zstd, huffman) images and checks them with `chdman verify` and `chdman extractraw` (including the diff over its parent), the reuse of an existing diff, a hand-built version 4 file, and the refusals above.
