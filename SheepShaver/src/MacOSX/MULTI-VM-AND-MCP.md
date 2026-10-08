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

`tools/vms/test.sh [unit|nvram|manager|control|mcp|all]`: `unit` is seconds (pixel conversion at every depth, key tables, the pointer
planner, the control protocol, HTTP parsing and access rules, every MCP tool against a fake backend). The others boot copies of
your Mac OS 9 disk: two VMs at once with separate NVRAM, the manager starting VMs, one VM driven through its socket (menus open
under a held button, Cmd-F and typed text appear on screen), and the MCP server end to end including the stdio bridge.
