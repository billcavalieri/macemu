/*
 *  SheepWindowController.swift - The window of one running virtual machine: the Mac OS picture, a small toolbar
 *  (shut down, settings) and the Guest menu. The library lives in the manager process (ManagerWindowController).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@_silgen_name("VideoHostRequestQuit")
private func VideoHostRequestQuit()

@MainActor
final class SheepWindow: NSWindow {
    weak var guestDisplay: GuestDisplayView?

    override func sendEvent(_ event: NSEvent) {
        if event.type == .keyDown, let guestDisplay, guestDisplay.releaseByHotkey(event) {
            return
        }
        if event.type == .keyUp, let guestDisplay, guestDisplay.consumeHotkeyUp(event) {
            return
        }
        if let guestDisplay, guestDisplay.claimMouse(event) {
            return
        }
        super.sendEvent(event)
    }
}

@MainActor
final class SheepWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSMenuDelegate, NSMenuItemValidation, NSToolbarItemValidation {
    let store = VirtualMachineStore()
    let display = GuestDisplayView(frame: NSRect(x: 0, y: 0, width: 1024, height: 768))
    var booted = false
    private var settings: SettingsSheet?
    private var loggedPlacement = false

    private static let attachItem = NSToolbarItem.Identifier("attach")
    private static let shutDownItem = NSToolbarItem.Identifier("shutdown")
    private static let settingsItem = NSToolbarItem.Identifier("settings")

    init() {
        let window = SheepWindow(
            contentRect: NSRect(x: 80, y: 120, width: 1024, height: 768),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "SheepShaver"
        window.isRestorable = false
        window.isReleasedWhenClosed = false
        super.init(window: window)
        window.delegate = self
        window.guestDisplay = display
        window.contentView = display
        window.contentMinSize = NSSize(width: 320, height: 240)

        let toolbar = NSToolbar(identifier: "SheepShaverVMToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        window.toolbar = toolbar
        window.layoutIfNeeded()
        display.syncPointerToPicture()
        NotificationCenter.default.addObserver(self, selector: #selector(shearsStatusChanged(_:)),
                                               name: .shearsStatusChanged, object: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    // MARK: title

    /// The VM's name: its library name, else the name of its prefs file or folder.
    private(set) var vmName = "SheepShaver"

    func setVMName(_ name: String) {
        vmName = name
        window?.title = name
        refreshSubtitle()
    }

    /// "Sheep Shears" under the title while the guest tool is running.
    private func refreshSubtitle() {
        let running = booted && ShearsHost.shared.status.toolRunning
        window?.subtitle = running ? "Sheep Shears" : ""
    }

    @objc private func shearsStatusChanged(_ note: Notification) {
        refreshSubtitle()
        window?.toolbar?.validateVisibleItems()
    }

    func logDisplayGeometry() {
        guard !loggedPlacement, let window else { return }
        window.layoutIfNeeded()
        display.layoutSubtreeIfNeeded()
        loggedPlacement = true
        let bounds = display.bounds
        let inWindow = display.convert(bounds, to: nil)
        let line = String(
            format: "NW-BOOT mouse view=%.0f,%.0f %.0fx%.0f inWindow=%.0f,%.0f %.0fx%.0f\n",
            bounds.minX, bounds.minY, bounds.width, bounds.height,
            inWindow.minX, inWindow.minY, inWindow.width, inWindow.height
        )
        if nwDiagnosticsOn {
            fputs(line, stdout)
            fflush(stdout)
        }
    }

    // MARK: window

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if booted, ShearsHost.shared.status.toolRunning {
            askShutDownOnClose(sender)
            return false
        }
        if booted {
            VideoHostRequestQuit()
        } else {
            BootGate.quit = true
        }
        return true
    }

    func windowDidResize(_ notification: Notification) {
        display.syncPointerToPicture()
    }

    // MARK: toolbar

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        // A VM that was started embedded can go back to the library window
        SheepHost.isEmbedded ? [.flexibleSpace, Self.attachItem, Self.shutDownItem, Self.settingsItem]
                             : [.flexibleSpace, Self.shutDownItem, Self.settingsItem]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        func make(_ label: String, _ symbol: String, _ tip: String, _ action: Selector) -> NSToolbarItem {
            let item = NSToolbarItem(itemIdentifier: id)
            item.label = label
            item.paletteLabel = label
            item.toolTip = tip
            item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)
            item.target = self
            item.action = action
            return item
        }
        switch id {
        case Self.attachItem:
            return make("Attach", "arrow.down.backward.square", "Move this virtual machine back into the library window", #selector(attachToLibrary(_:)))
        case Self.shutDownItem:
            return make("Shut Down", "power", "Shut down the guest (needs Sheep Shears in the guest)", #selector(shutDownGuest(_:)))
        case Self.settingsItem:
            return make("Settings", "gearshape", "Settings for this virtual machine", #selector(openSettings(_:)))
        default:
            return nil
        }
    }

    func validateToolbarItem(_ item: NSToolbarItem) -> Bool {
        item.action == #selector(shutDownGuest(_:)) ? canShutDownGuest : true
    }

    private var canShutDownGuest: Bool {
        let status = ShearsHost.shared.status
        if case .requested = status.shutdown { return false }
        return booted && status.toolRunning
    }

    @objc func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem.action == #selector(shutDownGuest(_:)) { return canShutDownGuest }
        return true
    }

    /// Text for the first item of the Guest menu: whether the guest tool is running, and any shutdown in progress.
    private func guestStatusLine() -> String {
        guard booted else { return "Guest not started" }
        let status = ShearsHost.shared.status
        guard status.toolRunning else { return "Sheep Shears: not running in the guest" }
        var line = "Sheep Shears: running (version \(status.toolVersion))"
        switch status.shutdown {
        case .idle: break
        case .requested: line += " · shutdown requested"
        case .sent: line += " · the guest was asked to shut down"
        case .failed(let code): line += " · shutdown failed (error \(code))"
        }
        return line
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.items.first?.title = guestStatusLine()
        let host = ShearsHost.shared.hostSwitches
        let guest = ShearsHost.shared.guestSwitches
        for item in menu.items {
            guard let feature = Self.features[item.action.map(NSStringFromSelector) ?? ""] else { continue }
            item.state = host.contains(feature.flag) ? .on : .off
            item.title = feature.title + (guest.contains(feature.flag) ? "" : " (off in the guest's Sheep Shears control panel)")
        }
    }

    private static let features: [String: (flag: ShearsFeatures, title: String)] = [
        "toggleEdgeRelease:": (.edgeRelease, "Release the Mouse at the Screen Edge"),
        "toggleClipboard:": (.clipboard, "Share the Clipboard"),
    ]

    @objc private func toggleEdgeRelease(_ sender: Any?) { ShearsHost.shared.hostSwitches.formSymmetricDifference(.edgeRelease) }
    @objc private func toggleClipboard(_ sender: Any?) { ShearsHost.shared.hostSwitches.formSymmetricDifference(.clipboard) }

    /// Guest > Install Sheep Shears. The installer is a disk image bundled in the app. It is copied next to the other
    /// SheepShaver files and added to this VM's disks (read-only); after the next start of the guest, the user opens
    /// the installer on it. A running guest cannot gain a disk, so the sheet says when it takes effect.
    @objc private func installSheepShears(_ sender: Any?) {
        guard let window else { return }
        ShearsInstallFlow.run(on: window, prefsPath: PrefsBridge.path(), vmName: vmName)
    }

    /// Closing the window while Sheep Shears runs: offer a proper shutdown first. "Quit Now" is the old behaviour.
    private func askShutDownOnClose(_ window: NSWindow) {
        let alert = NSAlert()
        alert.messageText = "Shut down the guest before closing?"
        alert.informativeText = "Quitting now stops the guest without saving, like pulling the plug. A shutdown asks every open application to quit and save first; this window closes when Mac OS has finished."
        alert.addButton(withTitle: "Shut Down")
        alert.addButton(withTitle: "Quit Now")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            switch response {
            case .alertFirstButtonReturn:
                if !ShearsHost.shared.requestShutdown() {
                    // Already asked, or the tool just went away: the user decides again on the next close.
                    let note = NSAlert()
                    note.messageText = "The guest could not be asked to shut down"
                    note.informativeText = "A shutdown request may already be waiting. Close the window again to quit now."
                    note.beginSheetModal(for: window)
                }
            case .alertSecondButtonReturn:
                VideoHostRequestQuit()
                window.close()
            default:
                break
            }
        }
    }

    /// Guest > Shut Down Guest. The tool in the guest asks the Finder to shut down, so Mac OS asks every application
    /// to quit and save as it would for Special > Shut Down; the emulator quits when Mac OS has finished. Needs the
    /// Sheep Shears tool running (the item is disabled otherwise).
    @objc private func shutDownGuest(_ sender: Any?) {
        guard booted, let window else { return }
        let alert = NSAlert()
        alert.messageText = "Shut down the guest?"
        alert.informativeText = "Mac OS asks every open application to quit, as for Special > Shut Down. An application with unsaved work asks about it in the guest window. This window closes when Mac OS has finished."
        alert.addButton(withTitle: "Shut Down")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            guard response == .alertFirstButtonReturn else { return }
            if !ShearsHost.shared.requestShutdown() {
                let note = NSAlert()
                note.messageText = "The guest could not be asked to shut down"
                note.informativeText = "Sheep Shears is not running in the guest, or a shutdown request is already waiting. Use Special > Shut Down in the guest instead."
                note.beginSheetModal(for: window)
            }
        }
    }

    private func installGuestMenu(in main: NSMenu) {
        guard !main.items.contains(where: { $0.title == "Guest" }) else { return }
        let item = NSMenuItem()
        item.title = "Guest"
        let menu = NSMenu(title: "Guest")
        menu.delegate = self
        menu.autoenablesItems = true
        let status = NSMenuItem(title: guestStatusLine(), action: nil, keyEquivalent: "")
        status.isEnabled = false
        menu.addItem(status)
        menu.addItem(.separator())
        for (selector, title) in [(#selector(toggleEdgeRelease(_:)), "Release the Mouse at the Screen Edge"),
                                  (#selector(toggleClipboard(_:)), "Share the Clipboard")] {
            let toggle = NSMenuItem(title: title, action: selector, keyEquivalent: "")
            toggle.target = self
            menu.addItem(toggle)
        }
        menu.addItem(.separator())
        let install = NSMenuItem(title: "Install Sheep Shears…", action: #selector(installSheepShears(_:)), keyEquivalent: "")
        install.target = self
        menu.addItem(install)
        let shutdown = NSMenuItem(title: "Shut Down Guest…", action: #selector(shutDownGuest(_:)), keyEquivalent: "")
        shutdown.target = self
        menu.addItem(shutdown)
        item.submenu = menu
        if let index = main.items.firstIndex(where: { $0.title == "Window" }) {
            main.insertItem(item, at: index)
        } else {
            main.addItem(item)
        }
    }


    // MARK: menus

    /// The VM's menu bar: the standard menus (no shortcuts, see AppMenu) with the Guest menu before Window.
    func installMenus() {
        let main = AppMenu.installVM(self)
        installGuestMenu(in: main)
        AppMenu.reportVM(main)
    }

    /// Back to the library window (View > Attach to Library Window, or the toolbar button).
    @objc func attachToLibrary(_ sender: Any?) {
        SheepHost.attachToLibrary()
    }

    @objc func closeMachine(_ sender: Any?) {
        window?.performClose(sender)
    }

    @objc func openSettings(_ sender: Any?) {
        guard let window, settings == nil else { return }
        let prefs = PrefsBridge.path()
        let sheet = SettingsSheet(prefsPath: prefs.isEmpty ? nil : prefs, running: booted, live: booted) { [weak self] saved in
            guard let self else { return }
            if let win = self.settings?.window { self.window?.endSheet(win) }
            self.settings = nil
            if saved && self.booted { self.display.applyMousePrefs() }
        }
        settings = sheet
        if let win = sheet.window { window.beginSheet(win) }
    }
}
