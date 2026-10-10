/*
 *  SheepHost.swift - Shows the library and the Mac OS picture in one window.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@_silgen_name("HostLaunchBackground")
private func HostLaunchBackground() -> Int32
@_silgen_name("HostRestartAfterQuit")
private func HostRestartAfterQuit()
@_silgen_name("VideoHostRequestQuit")
private func VideoHostRequestQuit()
@_silgen_name("HostLaunchEmbedded")
private func HostLaunchEmbedded() -> Int32
@_silgen_name("VideoHostRequestWindowSink")
private func SheepForceRequestWindowSink(_ view: UnsafeMutableRawPointer)
@_silgen_name("VideoHostRequestShmSink")
private func SheepForceRequestShmSink()
@_silgen_name("VideoHostShmSinkActive")
private func ShmSinkActive() -> Int32
@_silgen_name("VideoHostGuestScreen")
private func VideoHostGuestScreen(_ w: UnsafeMutablePointer<Int32>, _ h: UnsafeMutablePointer<Int32>, _ depth: UnsafeMutablePointer<Int32>)
@_silgen_name("HostLaunchVMID")
private func HostLaunchVMID() -> UnsafePointer<CChar>?

@MainActor
enum BootGate {
    static var path: String?
    static var quit = false
}

@MainActor
@objc(SheepHost)
final class SheepHost: NSObject {
    /// The VM's window (a process that runs a VM).
    private static var controller: SheepWindowController?
    /// The library window (the manager process).
    private static var manager: ManagerWindowController?

    /// True in the process that only manages the library (started with no --config): it starts VMs, each in its
    /// own process and window, and never runs one itself.
    static var isManager = false

    /// Started with --background (by the manager or a remote client): the window appears without taking the focus.
    static var launchedInBackground: Bool { HostLaunchBackground() != 0 }
    /// Started with --embedded: this VM has no window; its picture goes to shared memory for the library window and its
    /// keyboard, mouse and cursor travel over the display socket (DisplayServer).
    nonisolated static var isEmbedded: Bool { HostLaunchEmbedded() != 0 }
    /// An embedded VM that has opened its own window (Detach). Written on the main thread, read by the emulator's callbacks.
    nonisolated(unsafe) private static var ownWindow = false
    /// Where the emulator's callbacks (size, cursor, arrow, guest pointer, problems) go: the library window, or the VM's own.
    nonisolated static var routesToLibrary: Bool { isEmbedded && !ownWindow }
    nonisolated static var displayMode: DisplayMode { routesToLibrary ? .embedded : .window }
    /// The library id of this VM when the manager started it (--vm-id), else nil.
    static var launchedVMID: String? { HostLaunchVMID().map { String(cString: $0) } }

    /// Held for the life of the process: an emulator must not be slowed by App Nap when its window is hidden,
    /// covered or on another Space (several VMs run at once, most of them behind other windows).
    private static var activity: NSObjectProtocol?
    private static func keepAwake() {
        guard activity == nil else { return }
        activity = ProcessInfo.processInfo.beginActivity(
            options: [.userInitiated, .latencyCritical, .idleSystemSleepDisabled],
            reason: "Running a Mac OS virtual machine")
    }

    private static var pendingProblems: [String] = []
    /// Problems reported before the window existed, shown once it does.
    private static func showProblems() {
        guard let window = controller?.window else { return }
        for text in pendingProblems {
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = "A problem starting the virtual machine"
            alert.informativeText = text
            alert.beginSheetModal(for: window)
        }
        pendingProblems.removeAll()
    }

    @objc nonisolated class func reportProblemText(_ text: UnsafePointer<CChar>) {
        let message = String(cString: text)
        if routesToLibrary {
            DisplayServer.shared.problem(message)
            return
        }
        DispatchQueue.main.async {
            MainActor.assumeIsolated {
                pendingProblems.append(message)
                showProblems()
            }
        }
    }

    @objc class func waitForConfig() -> UnsafePointer<CChar>? {
        isManager = true
        NSApplication.shared.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        if NSApp.mainMenu == nil {
            NSApp.finishLaunching()
        }
        if nwDiagnosticsOn, let look = ProcessInfo.processInfo.environment["NW_APPEARANCE"] {
            NSApp.appearance = NSAppearance(named: look == "light" ? .aqua : .darkAqua)   // for screenshots
        }
        let library = ManagerWindowController()
        manager = library
        AppMenu.installManager(library)
        library.showWindow(nil)
        MCPService.shared.configure(store: library.store)   // starts the MCP server if the Settings have it on
        if nwDiagnosticsOn, ProcessInfo.processInfo.environment["NW_OPEN_SETTINGS"] == "1" {
            library.openAppSettings(nil)                    // for screenshots of the Settings window
        }
        // Test hook: add these prefs files (comma separated) to the library, as Add Existing… does. Diagnostics only.
        if nwDiagnosticsOn, let list = ProcessInfo.processInfo.environment["NW_MANAGER_ADD"] {
            for path in list.split(separator: ",").map(String.init) {
                do {
                    let doc = try library.store.addExisting(URL(fileURLWithPath: path))
                    NSLog("NW-MANAGER added %@ as %@", path, doc.name)
                } catch {
                    NSLog("NW-MANAGER add failed: %@", error.localizedDescription)
                }
            }
            library.refreshLibrary()
        }
        // Test hook: start the selected library VM (the last one added), as the Start button does. Diagnostics only.
        if nwDiagnosticsOn, ProcessInfo.processInfo.environment["NW_MANAGER_START"] == "1" {
            DispatchQueue.main.async { MainActor.assumeIsolated { library.startSelected(nil) } }
        }
        // Test hook: open a sheet ("new", "settings" for the selected VM) so it can be photographed. Diagnostics only.
        if nwDiagnosticsOn, let sheet = ProcessInfo.processInfo.environment["NW_OPEN_SHEET"] {
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    if sheet == "new" { library.newMachine(nil) } else if sheet == "settings" {
                        library.openMachineSettings(nil)
                        if ProcessInfo.processInfo.environment["NW_SETTINGS_SAVE"] == "1" {    // tools/vms/test.sh settings
                            library.saveSettingsForTest()
                            NSLog("NW-SETTINGS saved")
                        }
                    }
                }
            }
        }
        // Test hook (tools/vms/test.sh manager): start these VMs (prefs paths, comma separated) as soon as the
        // manager is up. Honoured only with diagnostics on.
        if nwDiagnosticsOn, let list = ProcessInfo.processInfo.environment["NW_MANAGER_LAUNCH"] {
            for path in list.split(separator: ",").map(String.init) {
                let id = URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent
                library.store.launch(VirtualMachineDocument(id: id, name: id, prefsPath: path), background: true)
            }
        }
        var chosen: String?
        while chosen == nil {
            if BootGate.quit {
                NSApp.terminate(nil)
                return nil
            }
            if let event = NSApp.nextEvent(
                matching: .any,
                until: Date.distantFuture,
                inMode: .default,
                dequeue: true
            ) {
                NSApp.sendEvent(event)
            }
            chosen = BootGate.path
        }
        return chosen.flatMap { strdup($0) }.map { UnsafePointer($0) }
    }

    /// Detach: the embedded VM opens its own window and draws into it (the library window shows a placeholder).
    /// Main thread. False when it is not embedded or already has its window.
    @discardableResult
    static func detachToWindow() -> Bool {
        guard isEmbedded, !ownWindow else { return false }
        NSApplication.shared.setActivationPolicy(.regular)
        let wc = ensureWindow()
        wc.booted = true
        wc.installMenus()
        let path = PrefsBridge.path()
        let store = wc.store
        let known = store.machines.first { $0.prefsPath == path }?.name
        wc.setVMName(known ?? (path.isEmpty ? "SheepShaver" : LibraryImport.suggestedName(for: URL(fileURLWithPath: path))))
        var w: Int32 = 0, h: Int32 = 0, depth: Int32 = 0
        VideoHostGuestScreen(&w, &h, &depth)
        wc.display.setGuestSize(width: Int(w), height: Int(h))
        wc.display.inputEnabled = true
        wc.window?.layoutIfNeeded()
        wc.display.applyMousePrefs()
        wc.display.applyMacCursor()
        ownWindow = true                                 // callbacks go to the window from now on
        SheepForceRequestWindowSink(Unmanaged.passUnretained(wc.display).toOpaque())
        wc.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
        wc.window?.makeFirstResponder(wc.display)
        DisplayServer.shared.emit(.displayMode(inOwnWindow: true))
        if nwDiagnosticsOn { fputs("Display: detached to a window\n", stdout); fflush(stdout) }
        return true
    }

    /// Attach: the VM goes back to the library window. The window closes once the emulator has switched to shared memory.
    /// Main thread. False when it is not detached.
    @discardableResult
    static func attachToLibrary() -> Bool {
        guard isEmbedded, ownWindow, let wc = controller else { return false }
        wc.display.releaseCapture("attach")
        SheepForceRequestShmSink()
        func finish(_ tries: Int) {
            if ShmSinkActive() == 0 && tries < 150 {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.02) { MainActor.assumeIsolated { finish(tries + 1) } }
                return
            }
            ownWindow = false                            // callbacks go to the library window again
            wc.window?.close()                           // close() does not ask the delegate: no shutdown prompt
            controller = nil
            NSApplication.shared.setActivationPolicy(.accessory)
            var w: Int32 = 0, h: Int32 = 0, depth: Int32 = 0
            VideoHostGuestScreen(&w, &h, &depth)
            DisplayServer.shared.guestModeChanged(width: Int(w), height: Int(h))
            DisplayServer.shared.cursorChanged()
            DisplayServer.shared.emit(.displayMode(inOwnWindow: false))
            if nwDiagnosticsOn { fputs("Display: attached to the library window\n", stdout); fflush(stdout) }
        }
        finish(0)
        return true
    }

    /// Start-up of an embedded VM: the same services as `show` (App Nap opt-out, Sheep Shears, the control socket) but no
    /// window and no Dock icon; the display socket takes the viewer's input. Returns the name of the shared-memory
    /// region the renderer should create (the caller frees it).
    @objc class func showEmbedded(withWidth width: Int32, height: Int32) -> UnsafePointer<CChar>? {
        if !Thread.isMainThread {
            var name: UnsafePointer<CChar>?
            DispatchQueue.main.sync {
                name = showEmbedded(withWidth: width, height: height)
            }
            return name
        }
        keepAwake()
        NSApplication.shared.setActivationPolicy(.accessory)        // no Dock icon, no menu bar: the library window shows this VM
        if NSApp.mainMenu == nil {
            NSApp.finishLaunching()
        }
        ShearsHost.shared.install()
        let path = PrefsBridge.path()
        let fallback = path.isEmpty ? "pid-\(getpid())" : URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent
        let vmID = launchedVMID ?? fallback
        let store = VirtualMachineStore()
        let known = store.machines.first { $0.prefsPath == path }?.name
        let name = known ?? (path.isEmpty ? "SheepShaver" : LibraryImport.suggestedName(for: URL(fileURLWithPath: path)))
        VMControlServer.shared.start(VMControlContext(id: vmID, name: { name }))
        DisplayServer.shared.start(vmID: vmID)
        // Test hook (tools/vms/test.sh embedded): restart the guest N seconds after the first start, as Special > Restart does
        if nwDiagnosticsOn, let after = ProcessInfo.processInfo.environment["NW_RESTART_AFTER"].flatMap(Double.init),
           ProcessInfo.processInfo.environment["NW_RESTARTED"] == nil {
            setenv("NW_RESTARTED", "1", 1)                      // the restarted process does not restart again
            DispatchQueue.main.asyncAfter(deadline: .now() + after) { HostRestartAfterQuit(); VideoHostRequestQuit() }
        }
        return strdup(DisplayPaths.sharedMemoryName(vmID: vmID)).map { UnsafePointer($0) }
    }

    @objc class func show(withWidth width: Int32, height: Int32) -> NSView {
        if !Thread.isMainThread {
            var view: NSView?
            DispatchQueue.main.sync {
                view = show(withWidth: width, height: height)
            }
            return view!
        }
        keepAwake()
        NSApplication.shared.setActivationPolicy(.regular)
        if !launchedInBackground {
            NSApp.activate(ignoringOtherApps: true)
        }
        if NSApp.mainMenu == nil {
            NSApp.finishLaunching()
        }
        let wc = ensureWindow()
        wc.booted = true
        wc.installMenus()
        if launchedInBackground {
            wc.window?.orderFront(nil)          // in front of nothing: shown, but the user's focus stays where it is
        } else {
            wc.showWindow(nil)
        }
        wc.display.setGuestSize(width: Int(width), height: Int(height))
        wc.display.inputEnabled = true
        ShearsHost.shared.install()
        wc.window?.layoutIfNeeded()
        wc.display.applyMousePrefs()
        wc.logDisplayGeometry()
        wc.window?.makeFirstResponder(wc.display)
        DispatchQueue.main.async {
            wc.window?.makeFirstResponder(wc.display)
            wc.display.applyMousePrefs()
        }
        let path = PrefsBridge.path()
        if !path.isEmpty {
            BootGate.path = path
            wc.store.markRunning(prefsPath: path)
        }
        // The window is titled with the VM's name: its library name, else the name of its prefs file or folder
        let known = wc.store.document(id: wc.store.runningID)?.name
        wc.setVMName(known ?? (path.isEmpty ? "SheepShaver" : LibraryImport.suggestedName(for: URL(fileURLWithPath: path))))
        showProblems()
        // The control socket of this VM (the manager's MCP server connects to it)
        let vmID = launchedVMID ?? URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent
        VMControlServer.shared.start(VMControlContext(id: vmID.isEmpty ? "pid-\(getpid())" : vmID, name: { [weak wc] in
            DispatchQueue.main.sync { MainActor.assumeIsolated { wc?.store.document(id: wc?.store.runningID ?? wc?.store.selection)?.name ?? "" } }
        }))
        return wc.display
    }

    @objc nonisolated class func setGuestWidth(_ width: Int32, height: Int32) {
        if routesToLibrary {
            DisplayServer.shared.guestModeChanged(width: Int(width), height: Int(height))
            return
        }
        hop {
            guard let controller else { return }
            controller.display.setGuestSize(width: Int(width), height: Int(height))
            controller.window?.layoutIfNeeded()
        }
    }

    @objc nonisolated class func applyMacCursor() {
        if routesToLibrary {
            DisplayServer.shared.cursorChanged()
            return
        }
        hop {
            controller?.display.applyMacCursor()
        }
    }

    @objc nonisolated class func moveMacCursorX(_ x: Int32, y: Int32, visible: Int32) {
        if routesToLibrary {
            DisplayServer.shared.arrowMoved(x: Int(x), y: Int(y), visible: visible != 0)
            return
        }
        hop {
            controller?.display.setGuestArrow(x: Int(x), y: Int(y), visible: visible != 0)
        }
    }

    /// A pointer position from the guest tool (Sheep Shears). The view decides whether it releases the grab.
    class func guestPointerReported(_ report: ShearsPointerReport, at time: Double) {
        if routesToLibrary {
            DisplayServer.shared.shearsPointer(x: report.x, y: report.y, width: report.width, height: report.height, buttons: report.buttons)
            return
        }
        controller?.display.guestPointerReported(report, at: time)
    }

    @objc nonisolated class func reloadEdgeGrab() {
        hop {
            controller?.display.applyMousePrefs()
        }
    }

    private static func ensureWindow() -> SheepWindowController {
        if let controller {
            return controller
        }
        let wc = SheepWindowController()
        controller = wc
        return wc
    }

    nonisolated private static func hop(_ body: @escaping @MainActor () -> Void) {
        if Thread.isMainThread {
            MainActor.assumeIsolated(body)
        } else {
            DispatchQueue.main.async {
                MainActor.assumeIsolated(body)
            }
        }
    }
}
