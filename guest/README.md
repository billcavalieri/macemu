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

## Build and distribute

**Xcode builds it.** The SheepShaver target has a "Build Sheep Shears guest tools" phase that runs
`tools/shears/build.sh --if-needed`: it rebuilds the three applications and the installer disk when something in this
folder is newer than the disk, does nothing otherwise, and only prints a note (never fails the build) when Retro68
is not installed (`RETRO68_PREFIX`, default `~/Retro68-build/toolchain`).

The result is `SheepShaver/src/MacOSX/SheepApp/SheepShearsInstaller.hfv`, which the app bundles; Guest > Install
Sheep Shears... adds it to a virtual machine. **That one committed file is the only thing that ships.** It is
committed so a machine without Retro68 can still build the app; after changing something here, build once and
commit the refreshed `.hfv`. You can also run `tools/shears/build.sh` by hand.

## Test

```
tools/shears/test.sh [unit|install|settings|shutdown|clip|all]
```

`unit` runs in seconds and needs nothing. The others boot a copy of your Mac OS 9 disk and need the app built in Xcode
(see the top of `tools/shears/lib.sh` for what they find on their own and how to override it).
