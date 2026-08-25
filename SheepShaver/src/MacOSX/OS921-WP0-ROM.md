# WP0 — New World ROM + 9.2.1 install set (G0)

Local media only. Never commit a ROM, `.smi`, `.smi.bin`, `.toast`, or disk image.
Copy `prefs.os921.template` to a gitignored `prefs`, fill three absolute paths, point SheepShaver at that file.

Basilisk II is out of scope. Do not use the extractor in this tree.

## 1. Preferred G0 ROM: Update 1.0 → Mac OS ROM 1.6

[billcavalieri/tome-extract](https://github.com/billcavalieri/tome-extract) is a native macOS app. Headless:

```sh
TomeExtract.app/Contents/MacOS/TomeExtract \
  --extract /ABSOLUTE/PATH/TO/Mac_OS_ROM_Update_1.0.smi.bin \
  --output /ABSOLUTE/PATH/TO/out
```

It looks for type `tbxi` / `Mac OS ROM`. The output is a 1.x New World CHRP image (`lzss-offset` / `lzss-size`). Point the `rom` prefs key at that file.

## 2. Alternate G0 ROM: 9.2.1 install-set tbxi

The 9.2.1 install set already contains a System Folder file named `Mac OS ROM`. Copy that file. It is not a tome unwrap.

That image is CHRP with `parcels-offset` / `parcels-size`; the payload signature is `prcl`. `DecodeROM` in `SheepShaver/src/rom_patches.cpp` already takes this path. Both 1.6 (lzss) and this tbxi (parcels) must pass G0.

## 3. Not the G0 default: iMac Update 1.1 → ROM 1.2.1

`iMac Update 1.1` unwraps to Mac OS ROM 1.2.1. That is the 9.0.4 regression ROM only. Do not put it in `rom` for G0.

## 4. G0 byte checks (after `DecodeROM`)

File, before decode:

```sh
head -c 11 "Mac OS ROM"          # <CHRP-BOOT>
strings "Mac OS ROM" | grep -E 'lzss-offset|parcels-offset|parcels-size|lzss-size'
```

- ROM 1.6: `<CHRP-BOOT>` plus `constant lzss-offset` / `constant lzss-size`.
- 9.2.1 System Folder tbxi: `<CHRP-BOOT>` plus `constant parcels-offset` / `constant parcels-size`, then `prcl` at the parcels payload.

After `DecodeROM` (`SheepShaver/src/rom_patches.cpp`): 4 MiB at `ROMBase`. G0 accept:

| Check | Where |
|---|---|
| Unpacked ROM | 4 MiB (`ROM_SIZE`) at `ROMBase` |
| New World id | ASCII `NewWorld` at `ROMBase+0x30d064` (`PatchROM`) |
| Nanokernel v2 | NK at `ROMBase+0x310000` (entry `jump_to_rom` uses) |

`strings "Mac OS ROM" | grep NewWorld` on the still-wrapped file is a sanity check (`MacROM for NewWorld.`). The G0 offsets apply to the unpacked 4 MiB image, not the CHRP wrapper.

## 5. Installer CD and blank HFS

- `cdrom`: 9.2.1 retail/install image (`.toast` / ISO). That CD is not itself the system disk.
- `disk`: blank HFS target the installer can format (zero-filled `.hfv` / `.dsk` is enough; Disk Utility HFS Standard also works).

## 6. Grok Build — G0 command / env

Darwin `LoadPrefs` (`SheepShaver/src/Unix/prefs_unix.cpp`, non-Linux): `--config PATH`, else `~/.sheepshaver_prefs`, else `$HOME/Library/Application Support/SheepShaver/os921/prefs`, else `foo.sheepvm/prefs`. There is no prefs environment variable. Keys are in `SheepShaver/src/prefs_items.cpp`.

The helper writes a filled-in prefs **outside the repo** (never a ROM or `.toast`). After that, Xcode Run needs no arguments:

```sh
SheepShaver/src/MacOSX/setup-os921-prefs.sh
# Xcode: SheepShaver_Xcode8.xcodeproj → scheme SheepShaver → Run
# or:
SheepShaver --config "$HOME/Library/Application Support/SheepShaver/os921/prefs"
```

Default paths (override with `SHEEP_OS921_ROM`, `SHEEP_OS921_CDROM`, `SHEEP_OS921_DISK`):

- `rom`: `$HOME/Downloads/Mac OS ROM`
- `cdrom`: `$HOME/Downloads/Mac OS 9.2.1.toast`
- `disk`: `$HOME/Library/Application Support/SheepShaver/os921/macos921-blank.hfv` (2 GiB sparse zeros)

Filled-in local prefs by hand (not the `.template`):

```sh
cp SheepShaver/src/MacOSX/prefs.os921.template /ABSOLUTE/PATH/TO/os921/prefs
# edit the three /ABSOLUTE/PATH/TO/... lines, then:
SheepShaver --config /ABSOLUTE/PATH/TO/os921/prefs
```

Xcode scheme `SheepShaver` (`ARCHS=arm64`): click Run. No `--config` if the Application Support prefs file exists. Optional: Arguments Passed On Launch → `--config /ABSOLUTE/PATH/TO/os921/prefs`.

Host G0 (optional ROM, never from git): `SheepShaver-MMUTests` decodes `$SHEEP_OS921_ROM` or `$HOME/Downloads/Mac OS ROM` when that file exists.

G0 is done when `DecodeROM` accepts the New World image as above. Old World 9.0.4 with ROM 1.2.1 must still boot on its own prefs.
