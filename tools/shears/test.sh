#!/bin/bash
# Runs the Sheep Shears tests.   tools/shears/test.sh [unit|install|extfs|settings|shutdown|clip|mic|recdlg|sndchan|all]   (default: unit)
#   unit      the Swift checks for the protocol, pointer rule and session logic: no emulator, a few seconds
#   install   boots a disk copy and runs the installer headless
#   extfs     the same, installing from the extfs download unzipped into a shared folder
#   settings  boots a disk copy three times with different control panel settings
#   shutdown  the host shuts the guest down through the tool
#   clip      clipboard round trip (replaces your clipboard for about a minute, then restores it)
#   mic       sound input: records a test tone through the Sound Manager's SPB calls in a guest self-test
#   recdlg    sound input: a guest self-test opens the Sound Manager's record window as SimpleSound does and cancels it
#   sndchan   sound output: a guest self-test opens a sound channel, plays a tone and disposes of it, four times.
#             Not in "all": it brings out a known intermittent crash in the output teardown (see the script)
#   all       every test above, in that order
# The boot tests need the SheepShaver app built in Xcode, a Mac OS 9 disk image and the Retro68 toolchain; set-up and
# overrides are described at the top of tools/shears/lib.sh. Nothing is clicked or typed.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)/tests"
run() { echo "== $1"; "$DIR/$1.sh"; }
case "${1:-unit}" in
    extfs) echo "== extfs"; "$DIR/install.sh" extfs ;;
    unit|install|settings|shutdown|clip|mic|recdlg|sndchan) run "$1" ;;
    all) rc=0; for t in unit install extfs settings shutdown clip mic recdlg; do "$0" $t || rc=1; done; exit $rc ;;
    *) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
