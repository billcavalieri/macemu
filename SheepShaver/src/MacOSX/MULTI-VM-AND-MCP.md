# Running several VMs at once, and the MCP server

## Several VMs at once

The emulator core runs one VM per process (a fixed guest address window, one CPU/JIT, one set of devices, a process-wide
fault handler, `exit(0)` on quit). So every VM is its own SheepShaver process with its own window, and the app you open
first is a **manager**: it shows the library and starts VMs, but never runs one itself.

- **Open the app**: you get the library (VM list, Start/Show buttons, Running badges). **Play** starts the VM in its own
  process and window; press it again and that window comes forward. The manager can be closed while VMs keep running.
- **A VM window** is only the VM (the library sidebar is hidden; the toolbar button still shows it). Closing the window
  quits that VM, with the usual "shut down first?" question when the Sheep Shears tool runs.
- **What had to change so VMs can run together**
  - Each library VM keeps its NVRAM (and the New World boot flash) in its own folder, `.../SheepShaver/VMs/<id>/nvram`,
    instead of one shared `~/.sheepshaver_nvram`. The first start copies the shared file, so existing VMs keep their state.
  - Two VMs cannot write to the same disk image (the file is locked). The second now gets a message saying so, instead of
    silently booting to the "?" floppy. Read-only images (an install CD) take a shared lock and can be used by several VMs.
  - VM processes opt out of App Nap, so a VM behind other windows keeps running at full speed.
- Not solved: bridged Ethernet for two VMs at once (one packet-capture device, one MAC), shared `extfs` folders and `redir`
  ports with the same value (the second VM fails to bind), and one serial device in two VMs. Give each VM its own.

Process options (used by the manager; also handy from a terminal): `--config <prefs>`, `--vm-id <id>`, `--vm-dir <folder>`
(where the NVRAM goes), `--background` (show the window without taking the focus), `--log <file>` (diagnostics).

### Adding VMs to the library

The **+** button in the manager's toolbar is a menu:

- **New Virtual Machine…** asks for a name, a disk image and a ROM (each with a Browse button) and writes a library entry with default settings.
- **Add Existing…** takes a prefs file (for example the one you start with `--config`), or a folder that has one called `prefs`. The prefs are *copied* into a new library folder, so later edits to the original are not followed. Relative `disk`/`cdrom`/`floppy`/`rom`/`extfs` paths are made absolute first, so the disks stay where they are. Adding the same file twice selects the first entry.

A VM started with `--config` and no `--vm-id` is a single-VM window, not the manager.

## A VM inside the library window (and Detach)

Every VM is still its own process (the core is not re-entrant). The library starts them **embedded** (`--embedded`, the
default; Settings > General turns it off): the VM has no window and no Dock icon, and the library window shows its picture
and takes its keyboard and mouse. Choosing another running VM in the sidebar shows that one. **Detach** (toolbar, Guest
menu, or the info pane's **Attach to Library** button) gives a VM its own window; **Attach** puts it back. A VM started
with plain `--config` (how the tests and a command-line launch work) always has its own window, exactly as before.

```
library (manager)                                         VM process (--embedded)
  RemoteDisplayView  <== shared memory: 3 frame slots ==  the present pass renders straight into a slot (GPU)
  (CAMetalLayer, display link)  --- display socket --->   keyboard/mouse (main thread posts to the ADB rings)
                                <--- events ----------    guest size, Mac cursor, guest arrow, Sheep Shears pointer/status/
                                                          switches, display mode, problems
```

- **Frames** (`display_shm.h`, written by `sheepforce_metal.mm`, read by `sheepforce_remote.mm`): POSIX shared memory
  `/sheep.<uid>.<id>`, a 16 KB header and three guest-sized BGRA8 slots, wrapped on both sides as no-copy Metal buffers
  and buffer-backed textures, so there is no CPU copy. The VM renders the same present pass into the free slot and
  publishes it from the command buffer's completion handler. The library blits the newest slot into its layer on a
  display link. The viewer writes a heartbeat (and its refresh interval) into the header every tick: **a VM nobody is
  looking at renders nothing**, and the library stops the heartbeat when its window is hidden, minimized or showing
  another VM.
- **Display socket** (`/tmp/sheepshaver-<uid>/<id>.disp`, `DisplayProtocol.swift`): tiny binary messages (3 to 10
  bytes), written the moment AppKit delivers the input. Cursor-hides-host, the edge-release distance and the Sheep
  Shears switches are pushed state cached in the library, so there is no per-move round trip. A viewer that connects
  late is replayed the current state. When the last viewer goes away the VM releases the buttons and the relative mouse.
- **`GuestLink`** (`GuestLink.swift`): the 7 emulator calls the display view used to make directly are now a protocol.
  `LocalGuestLink` (the VM's own window) makes the same calls as before; `RemoteGuestLink` (the library) sends them over
  the socket. Only *changes* of the button, relative-mouse and pointer-wanted state are sent: the view re-asserts them
  on every grab and set-up, and for a VM that is already booting each one is a real ADB event that cost about 6% of its speed.
- **Keys while the VM has the keyboard**: a Command combination reaches the guest and not the library's menu
  (`performKeyEquivalent` in the display view); Ctrl-G lets go and the library's shortcuts work again.
- **Detach/attach in the VM process** (`SheepHost.detachToWindow/attachToLibrary`): the renderer switches target at the
  emulation thread's next present (`SheepForceRequestWindowSink/ShmSink`), so it is never replaced mid-draw; the
  activation policy goes `.regular` while detached and `.accessory` while embedded.
- **A guest restart** (Special > Restart) re-executes the VM process (`execv`, same pid). Descriptors are inherited across
  an exec, so the old control and display listeners and the viewer's connection used to stay open in the new image with
  nobody serving them: the library kept a dead connection and the old, frozen shared-memory mapping (a black or frozen
  picture), and the control socket looked alive but never answered. Now the sockets are close-on-exec, the restart path marks
  every remaining descriptor close-on-exec, and the library reconnects when a link ends while the VM is still running.
  `tools/vms/test.sh embedded` restarts a guest (diagnostics hook `NW_RESTART_AFTER`) and checks all of it.
- **Closing the library** while embedded VMs run asks whether to leave them running (they keep going without a window and
  are picked up again by the next library window); the MCP server stops with the library.
- **Control socket additions**: `display` (read or set `embedded`/`window`), `features` (the Sheep Shears switches),
  and `display` in `status`.

### Speed

Measured as CPU seconds of the VM process to the "QuickDraw hooks installed" line of a Mac OS 9 boot (same disk, 3–4
alternating runs): original build in its own window 26.1 s; new build in its own window 26.4 s; started by the library
in its own window 26.9 s; **started by the library and shown in the library 26.9 s**. The library process itself uses
about 4% of a core (one display link tick and one blit per frame). Present time per frame in the VM is unchanged
(`PLAY presents=59-60`). Socket round trip for a 12-byte message: 2.3 µs one way. Frame age when the library draws it:
about 9 ms on average, 16 ms at most (under one display refresh). A shared-memory buffer texture renders as fast as a
private one (65 µs median for the present pass).

## The windows and menus

- **Library window (the manager).** A standard split view: a source-list sidebar of the virtual machines and, next to
  it, the selected one: its live picture when it runs embedded, else its state, what it is made of and Start / Show
  Window, Shut Down and Settings… buttons (an empty library shows how to add one). Double-click or Return starts a VM; Delete removes it from the library
  (its folder goes to the Trash, disk images are never touched); a right-click offers Rename, Show in Finder and the
  rest; a prefs file or folder dropped on the list is added. The green dot, and the buttons, follow every VM that
  answers on its control socket, however it was started. Shut Down asks Mac OS to shut down (needs the Sheep Shears
  tool) and offers to stop the VM immediately when it cannot.
- **A VM's window** is just the VM: its name is the title, a small toolbar has Shut Down and Settings, and the Guest
  menu has the Sheep Shears switches. There is no sidebar in a VM process.
- **Menus.** The library has the standard menu bar with the usual shortcuts. A VM's menus have **no shortcuts at
  all**: Mac OS 9 applications use Command-Q, -W, -N, -comma and the rest, and a shortcut in the host's menu would
  take them before the guest saw them. (AppKit adds Command-M to a Window menu's Minimize item by itself; the VM's menu
  uses `miniaturize:` instead. `tools/vms/test.sh manager` lists every menu item with diagnostics on and fails if a
  VM's menu has a shortcut.)
- **Settings.** SheepShaver > Settings… (library only) is a toolbar-tab window: Server and Connect. The token is
  masked until Show. A VM's settings are a sheet with pages (General, Storage, Display, Sound, Input, Network,
  Advanced) in plain words; every setting the old sheet had is still there. The sheet edits the prefs file as lines
  (`PrefsDocument`): repeated lines (several `disk` lines), comments and keys it has no control for come back exactly
  as they were, and a key is only written when it differs from its default or is already in the file. (The old sheet
  rewrote the whole file from a dictionary, which kept only the last `disk` line.)

## The control socket (every running VM)

A VM process listens on `/tmp/sheepshaver-<uid>/<id>.sock` (directory 0700, socket 0600), newline-delimited JSON. Operations:
`ping`, `status`, `screenshot {max_width?}`, `mouse_move`, `mouse_click`, `mouse_down`, `mouse_up`, `mouse_drag`, `type_text`,
`press_key`, `shutdown {force?}`. `tools/vms/vmctl.py <id> <op> key=value …` talks to it from a terminal.

- Screenshots read the guest framebuffer (not the window), so they work while the window is hidden or covered. The copy is
  made on the emulation thread after GPU drawing in flight has finished; PNG encoding happens elsewhere.
- Pointer: relative moves in a loop, reading the guest's own pointer position after each, so it lands on the pixel whatever
  acceleration the guest applies (typically 0.2–0.5 s for a full-screen move). Coordinates are guest pixels, origin top left.
  A right click is a Control-click.
- Keyboard: US layout (ASCII, Return, Tab); `press_key` takes names (return, escape, left, f5…) and command/option/control/shift.
- `shutdown` is a clean Mac OS shutdown through the Sheep Shears tool; without the tool it is refused unless `force` is true
  (which quits at once, like pulling the plug).

## The MCP server

Off by default. **SheepShaver > Settings… (⌘,)** in the library window: enable it, choose the port (default 8765), copy the
token, and tick the VMs it may use (none are ticked at first; unticked VMs are invisible to clients). The server lives in the
manager process, so it is up exactly while the library window is open.

Tools: `list_vms`, `start_vm {wait_for_ready?, timeout_seconds?}`, `shutdown_vm {force?}`, `screenshot {max_width?}`,
`mouse_move`, `mouse_click {x?, y?, button?, count?}`, `mouse_down`, `mouse_up`, `mouse_drag`, `type_text`, `press_key`.
Every tool but `list_vms` takes `vm` (a name or id). `start_vm` shows the window without taking the focus; with
`wait_for_ready` it waits until the Sheep Shears tool in the guest reports in, which means Mac OS has finished starting.

- **HTTP**: `POST http://127.0.0.1:<port>/mcp` with `Authorization: Bearer <token>` (JSON-RPC 2.0, MCP protocol versions
  2025-06-18, 2025-03-26 and 2024-11-05, plain JSON replies). Claude Code:
  `claude mcp add --transport http sheepshaver http://127.0.0.1:8765/mcp --header "Authorization: Bearer <token>"`.
- **stdio**: `SheepShaver.app/Contents/MacOS/SheepShaver --mcp-stdio` bridges stdin/stdout to the HTTP endpoint (for Claude
  Desktop and other clients that start a program). It reads the token from the Keychain, so the client's config has no secret.
  Settings shows the exact JSON to paste.
- **Safety**: loopback only; bearer token (Keychain; "New Token…" replaces it); the `Host` header must name the loopback
  address (DNS rebinding) and any `Origin` header is refused (web pages); only ticked VMs are reachable; every argument is
  range-checked before it reaches a VM; no shell or file access is exposed. A client controlling a VM can do anything the
  guest can, including destroying its disk, so tick only machines you are happy for it to drive.

## Tests

`tools/vms/test.sh [unit|settings|nvram|manager|control|display|embedded|mcp|all]`

- `unit`: seconds, nothing booted. Pixel conversion at every depth, key tables, the pointer planner, the control and display
  protocols, HTTP parsing and access rules, every MCP tool against a fake backend, the prefs file model.
- `settings`: the settings sheet is opened and saved without a change; the prefs file must come back byte for byte.
- `nvram`, `manager`, `control`, `mcp`: two VMs at once with separate NVRAM, the manager starting VMs (and the menu bars: a VM's
  menus must have no shortcuts), one VM driven through its socket, the MCP server end to end including the stdio bridge.
- `display`: an embedded VM with no window. A test viewer (`tools/vms/displayclient.py`) maps its shared-memory frames and
  checks: no frames while nobody looks, about 60 a second while somebody does, the frame equals the control socket's screenshot,
  the display socket replays the guest's state, and its input moves the pointer, opens the Apple menu under a held button and
  types into the Find window; frames stop with the heartbeat; the memory and sockets are gone after a shutdown.
- `embedded`: the real library window starts an embedded VM, shows its picture (a window-only screenshot of the library), forwards
  input, notices when the VM ends, picks up a VM that was already running, detaches it into its own window (which shows the
  desktop) and attaches it back, and changes the Sheep Shears switches.

The VM-booting tests refuse to start while another SheepShaver is running (your own library window, for instance) unless
`SHEARS_ALLOW_RUNNING=1`; they only ever stop processes they started.
