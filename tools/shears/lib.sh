# Shared set-up for the Sheep Shears guest tests (sourced by the boot tests in tools/shears/tests).
# With no environment set it finds everything itself. Optional overrides:
#   SHEEPSHAVER_APP     SheepShaver.app/Contents/MacOS/SheepShaver (default: the newest Xcode build in DerivedData)
#   SHEARS_BASE_DISK    the disk image to clone (default: ~/Library/Application Support/SheepShaver/os921/macos921-blank.hfv).
#                       It is only cloned (cp -c), never opened for writing. It needs Mac OS 9 and a Startup Items folder.
#   SHEARS_CDROM        the Mac OS 9 CD image the ROM is read from (default: ~/Downloads/Mac OS 9.2.1.toast); opened read-only
#   SHEARS_TEST_PREFS   a prefs file to base the run on (default: a minimal generated one; its `disk` line is replaced)
#   RETRO68_PREFIX      Retro68 toolchain (for hmount/hcopy and the guest build; default ~/Retro68-build/toolchain)
#   SHEARS_ALLOW_STALE  1 to run even if the app is older than the sources it should contain

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${RETRO68_PREFIX:-$HOME/Retro68-build/toolchain}"

# shears_check_environment: sets SHEEPSHAVER_APP, SHEARS_BASE_DISK, SHEARS_CDROM; exits with a message when something is missing.
shears_check_environment() {
    if [ -z "${SHEEPSHAVER_APP:-}" ]; then
        SHEEPSHAVER_APP="$(ls -t "$HOME"/Library/Developer/Xcode/DerivedData/SheepShaver-*/Build/Products/*/SheepShaver.app/Contents/MacOS/SheepShaver 2>/dev/null | head -1)"
        [ -n "$SHEEPSHAVER_APP" ] || { echo "No SheepShaver build found in Xcode's DerivedData. Build the app, or set SHEEPSHAVER_APP." >&2; exit 1; }
    fi
    [ -x "$SHEEPSHAVER_APP" ] || { echo "$SHEEPSHAVER_APP is not an executable" >&2; exit 1; }
    if [ "${SHEARS_ALLOW_STALE:-0}" != 1 ]; then
        local f
        for f in SheepShaver/src/shears.cpp SheepShaver/src/rom_patches.cpp SheepShaver/src/thunks.cpp \
                 SheepShaver/src/MacOSX/SheepApp/Shears/ShearsHost.swift SheepShaver/src/MacOSX/SheepApp/Shears/ShearsSession.swift \
                 SheepShaver/src/MacOSX/SheepApp/SheepWindowController.swift; do
            if [ "$ROOT/$f" -nt "$SHEEPSHAVER_APP" ]; then
                echo "The app ($SHEEPSHAVER_APP) is older than $f. Rebuild it in Xcode first (or set SHEARS_ALLOW_STALE=1)." >&2
                exit 1
            fi
        done
    fi
    SHEARS_BASE_DISK="${SHEARS_BASE_DISK:-$HOME/Library/Application Support/SheepShaver/os921/macos921-blank.hfv}"
    [ -f "$SHEARS_BASE_DISK" ] || { echo "No disk image at $SHEARS_BASE_DISK. Set SHEARS_BASE_DISK." >&2; exit 1; }
    SHEARS_CDROM="${SHEARS_CDROM:-$HOME/Downloads/Mac OS 9.2.1.toast}"
    if [ -z "${SHEARS_TEST_PREFS:-}" ] && [ ! -f "$SHEARS_CDROM" ]; then
        echo "No CD image at $SHEARS_CDROM (the ROM is read from it). Set SHEARS_CDROM or SHEARS_TEST_PREFS." >&2
        exit 1
    fi
    [ -x "$PREFIX/bin/hmount" ] || { echo "Retro68 not found at $PREFIX (set RETRO68_PREFIX; see SHEEP-SHEARS-PLAN.md)." >&2; exit 1; }
    if pgrep -x SheepShaver > /dev/null; then
        echo "SheepShaver is running. Quit it first: these tests start their own copy." >&2
        exit 1
    fi
}

# shears_prepare WORKDIR APP.bin...: builds the guest apps, clones the disk into WORKDIR, copies the given
# applications (paths under build/sheepshears) into its Startup Items folder and writes WORKDIR/prefs.
shears_prepare() {
    local work="$1"; shift
    "$ROOT/tools/shears/build.sh" --apps-only > /dev/null || exit 1
    cp -c "$SHEARS_BASE_DISK" "$work/disk.hfv"
    ( export PATH="$PREFIX/bin:$PATH"
      hmount "$work/disk.hfv" > /dev/null && hcd "System Folder" && hcd "Startup Items" || exit 1
      for app in "$@"; do hcopy -m "$ROOT/build/sheepshears/$app" ":" || exit 1; done ) || { echo "could not install the guest apps"; exit 1; }
    if [ -n "${SHEARS_TEST_PREFS:-}" ]; then
        sed "s#^disk .*#disk $work/disk.hfv#" "$SHEARS_TEST_PREFS" > "$work/prefs"
    else
        mkdir -p "$work/share"
        cat > "$work/prefs" <<PREFS
disk $work/disk.hfv
cdrom $SHEARS_CDROM
extfs $work/share
screen win/1024/768
windowmodes 0
screenmodes 0
seriala /dev/null
serialb /dev/null
rom 
bootdrive 0
bootdriver 0
ramsize 536870912
frameskip 1
gfxaccel true
sheepforce true
qtcodec true
nocdrom false
nonet true
nosound true
nogui true
ignoresegv true
ignoreillegal true
jit true
keyboardtype 5
hardcursor false
idlewait true
PREFS
    fi
}
