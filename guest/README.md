# Mac OS 9 guest software

Everything here runs **inside** the emulated Mac OS 9 and is built with Retro68, not Xcode.

## Sheep Shears (`sheepshears/`)

| Source | Becomes |
|---|---|
| `sheepshears.c` | **Sheep Shears Tool**: background application in Startup Items; talks to the host (mouse position, shutdown, settings) |
| `panel.c` | **Sheep Shears**: control panel with the two switches (edge release, clipboard) |
| `installer.c` | **Install Sheep Shears**: copies the two above into the System Folder, starts the tool, can remove them |
| `shears_client.h`, `shears_ui.h` | shared guest code (mailbox calls, settings file, dialog loop) |
| `scraptest.c` | clipboard self-test used by `tools/shears/test.sh clip`; not shipped |

The host side is `SheepShaver/src/MacOSX/SheepApp/Shears/` (Swift) and `SheepShaver/src/shears.cpp`.

## SheepBench (`sheepbench/`)

A benchmark app you open inside the guest. It reports emulator measurements (raw units, JIT on vs off, percent of
native ARM64 speed, SheepForce GPU on vs off) instead of a comparison with a G3. See
`SheepShaver/src/MacOSX/SHEEPBENCH.md`.

| Source | What it is |
|---|---|
| `main.c`, `textwin.c` | the app: main window, results, Save, Compare, About, auto mode for the tests |
| `bench_host.c` | the link to the host (Sheep Shears mailbox commands 7-10), only used when low memory says the host answers |
| `bench_time.c`, `bench_report.c` | the clock and measurement rule; result rows and the tab-separated report |
| `suite_*.c` | the seven suites: CPU, 68k, memory, MMU, QuickDraw, 3D (RAVE and QuickDraw 3D), disk |
| `kernels/` | C kernels shared with the host (`benchNative` runs the same code natively) and their build-time checksums |
| `kernels68k.s`, `sb_altivec.c` | 68k loops (assembled into the app) and the AltiVec loop |

`tools/sheepbench/build.sh` builds it and the disk the app bundles (`SheepShaver/src/MacOSX/SheepApp/SheepBench.hfv`,
Guest > Add SheepBench Disk...) and `guest/dist/SheepBench-extfs.zip`; Xcode runs it with `--if-needed`, like the
Sheep Shears build. `tools/sheepbench/test.sh` boots a scratch copy of the test disk and checks a run.

## Build and distribute

**Xcode builds it.** The SheepShaver target has a "Build Sheep Shears guest tools" phase that runs
`tools/shears/build.sh --if-needed`: it rebuilds the three applications and the installer disk when something in this
folder is newer than the disk, does nothing otherwise, and only prints a note (never fails the build) when Retro68
is not installed (`RETRO68_PREFIX`, default `~/Retro68-build/toolchain`).

The result is `SheepShaver/src/MacOSX/SheepApp/SheepShearsInstaller.hfv`, which the app bundles; Guest > Install
Sheep Shears... adds it to a virtual machine. **That one committed file is what the app ships.** It is
committed so a machine without Retro68 can still build the app; after changing something here, build once and
commit the refreshed `.hfv`. You can also run `tools/shears/build.sh` by hand.

## For people who share a folder with the guest (extfs)

`guest/dist/SheepShears-extfs.zip` is the same three programs as a plain download: unzip it into the folder you
share with SheepShaver (the `extfs` pref), open the shared volume in the guest and run **Install Sheep Shears**. The
files carry real macOS resource forks and Finder info (that is what SheepShaver's extfs reads on a Mac), so unzip with
Archive Utility or `ditto -x -k`, not a tool that drops them. `tools/shears/build.sh` regenerates the zip
(`make_extfs.py`) next to the installer disk, and the build only changes it when the programs change.
`tools/shears/test.sh extfs` installs from the unpacked zip inside a guest.

## Test

```
tools/shears/test.sh [unit|install|extfs|settings|shutdown|clip|all]
```

`unit` runs in seconds and needs nothing. The others boot a copy of your Mac OS 9 disk and need the app built in Xcode
(see the top of `tools/shears/lib.sh` for what they find on their own and how to override it).
