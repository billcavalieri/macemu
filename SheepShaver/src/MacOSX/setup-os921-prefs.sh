#!/bin/sh
# Local WP0/G0 prefs for Mac OS 9.2.1. Never copies a ROM, .toast, .smi, or
# disk image into the git tree. Fill a gitignored prefs file outside the repo
# and create a zero-filled HFS target the installer can format.
#
# Usage:
#   SheepShaver/src/MacOSX/setup-os921-prefs.sh
#   Open SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj, scheme SheepShaver, Run.
# CLI still accepts:
#   SheepShaver --config "$HOME/Library/Application Support/SheepShaver/os921/prefs"
#
# Override paths with SHEEP_OS921_ROM, SHEEP_OS921_CDROM, SHEEP_OS921_DISK.

set -e

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TEMPLATE="$REPO_ROOT/SheepShaver/src/MacOSX/prefs.os921.template"

if [ ! -f "$TEMPLATE" ]; then
	echo "setup-os921-prefs: missing $TEMPLATE" >&2
	exit 1
fi

HOME_DIR=${HOME:?HOME is not set}
OS921_DIR="$HOME_DIR/Library/Application Support/SheepShaver/os921"
PREFS="$OS921_DIR/prefs"
ROM=${SHEEP_OS921_ROM:-"$HOME_DIR/Downloads/Mac OS ROM"}
CDROM=${SHEEP_OS921_CDROM:-"$HOME_DIR/Downloads/Mac OS 9.2.1.toast"}
DISK=${SHEEP_OS921_DISK:-"$OS921_DIR/macos921-blank.hfv"}

# Refuse to write media into the repo (even if gitignored).
in_repo() {
	case "$1" in
	"$REPO_ROOT"/*) return 0 ;;
	*) return 1 ;;
	esac
}
if in_repo "$ROM" || in_repo "$CDROM" || in_repo "$DISK" || in_repo "$PREFS"; then
	echo "setup-os921-prefs: refusing to place G0 media inside $REPO_ROOT" >&2
	exit 1
fi

if [ ! -f "$ROM" ]; then
	echo "setup-os921-prefs: ROM not found: $ROM" >&2
	exit 1
fi
if [ ! -f "$CDROM" ]; then
	echo "setup-os921-prefs: installer CD not found: $CDROM" >&2
	exit 1
fi

HEAD=$(dd if="$ROM" bs=11 count=1 2>/dev/null)
if [ "$HEAD" != "<CHRP-BOOT>" ]; then
	echo "setup-os921-prefs: $ROM is not a CHRP Mac OS ROM (expected <CHRP-BOOT>)" >&2
	exit 1
fi
if ! strings "$ROM" | grep -q 'constant parcels-offset\|constant lzss-offset'; then
	echo "setup-os921-prefs: $ROM has neither parcels-offset nor lzss-offset" >&2
	exit 1
fi
if strings "$ROM" | grep -q 'constant parcels-offset' && \
   ! strings "$ROM" | grep -q 'constant lzss-offset'; then
	echo "G0 ROM: 9.2.1 install-set tbxi (parcels)"
elif strings "$ROM" | grep -q 'constant lzss-offset'; then
	echo "G0 ROM: New World lzss (ROM 1.6-style)"
fi

mkdir -p "$OS921_DIR"
DISK_DIR=$(dirname -- "$DISK")
mkdir -p "$DISK_DIR"

# 2 GiB zero-filled (sparse) HFS target. Not a bootable system disk.
if [ ! -f "$DISK" ]; then
	echo "Creating blank HFS target: $DISK"
	mkfile -n 2g "$DISK"
else
	echo "Keeping existing disk: $DISK"
fi

# Prefs parser: first token is the key, rest of the line is the value.
# Paths may contain spaces. Do not commit this file.
{
	sed -e '/^rom /d' -e '/^disk /d' -e '/^cdrom /d' "$TEMPLATE"
	printf 'rom %s\n' "$ROM"
	printf 'disk %s\n' "$DISK"
	printf 'cdrom %s\n' "$CDROM"
} > "$PREFS"

echo "Wrote $PREFS"
echo "Xcode: open SheepShaver/src/MacOSX/SheepShaver_Xcode8.xcodeproj, scheme SheepShaver, click Run."
echo "CLI: SheepShaver --config \"$PREFS\""
echo "ROM and .toast stay in Downloads; they are not copied into git."
