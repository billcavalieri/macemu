/*
 *  SheepHost.swift - Shows the library and the Mac OS picture in one window.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@_silgen_name("HostLaunchBackground")
private func HostLaunchBackground() -> Int32
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
    private static var controller: SheepWindowController?

    /// True in the process that only manages the library (started with no --config): it starts VMs, each in its
    /// own process and window, and never runs one itself.
    static var isManager = false

    /// Started with --background (by the manager or a remote client): the window appears without taking the focus.
    static var launchedInBackground: Bool { HostLaunchBackground() != 0 }
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
        let wc = ensureWindow()
        wc.installSidebarMenu()
        wc.showWindow(nil)
        MCPService.shared.configure(store: wc.store)       // starts the MCP server if the Settings have it on
        if nwDiagnosticsOn, ProcessInfo.processInfo.environment["NW_OPEN_SETTINGS"] == "1" {
            wc.openAppSettings(nil)                         // for screenshots of the Settings window
        }
        // Test hook: add these prefs files (comma separated) to the library, as Add Existing… does. Diagnostics only.
        if nwDiagnosticsOn, let list = ProcessInfo.processInfo.environment["NW_MANAGER_ADD"] {
            for path in list.split(separator: ",").map(String.init) {
                do {
                    let doc = try wc.store.addExisting(URL(fileURLWithPath: path))
                    NSLog("NW-MANAGER added %@ as %@", path, doc.name)
                } catch {
                    NSLog("NW-MANAGER add failed: %@", error.localizedDescription)
                }
            }
            wc.refreshLibrary()
        }
        // Test hook (tools/vms/test.sh manager): start these VMs (prefs paths, comma separated) as soon as the
        // manager is up. Honoured only with diagnostics on.
        if nwDiagnosticsOn, let list = ProcessInfo.processInfo.environment["NW_MANAGER_LAUNCH"] {
            for path in list.split(separator: ",").map(String.init) {
                let id = URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent
                wc.store.launch(VirtualMachineDocument(id: id, name: id, prefsPath: path), background: true)
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
        wc.installSidebarMenu()
        if launchedVMID != nil, wc.sidebarIsShown {
            wc.toggleSidebar(nil)               // the manager owns the library; a VM's window is just the VM
        }
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
        if let name = wc.store.document(id: wc.store.runningID ?? wc.store.selection)?.name {
            wc.setSubtitle(name)
        } else if !path.isEmpty {
            wc.setSubtitle(URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent)
        }
        showProblems()
        // The control socket of this VM (the manager's MCP server connects to it)
        let vmID = launchedVMID ?? URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent
        VMControlServer.shared.start(VMControlContext(id: vmID.isEmpty ? "pid-\(getpid())" : vmID, name: { [weak wc] in
            DispatchQueue.main.sync { MainActor.assumeIsolated { wc?.store.document(id: wc?.store.runningID ?? wc?.store.selection)?.name ?? "" } }
        }))
        return wc.display
    }

    @objc nonisolated class func setGuestWidth(_ width: Int32, height: Int32) {
        hop {
            guard let controller else { return }
            controller.display.setGuestSize(width: Int(width), height: Int(height))
            controller.window?.layoutIfNeeded()
        }
    }

    @objc nonisolated class func applyMacCursor() {
        hop {
            controller?.display.applyMacCursor()
        }
    }

    @objc nonisolated class func moveMacCursorX(_ x: Int32, y: Int32, visible: Int32) {
        hop {
            controller?.display.setGuestArrow(x: Int(x), y: Int(y), visible: visible != 0)
        }
    }

    /// A pointer position from the guest tool (Sheep Shears). The view decides whether it releases the grab.
    class func guestPointerReported(_ report: ShearsPointerReport, at time: Double) {
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
