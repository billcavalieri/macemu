/*
 *  SheepWindowController.swift - One window. Toolbar on the window,
 *  split view with the VM list and the Metal screen.
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
final class SheepWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSSplitViewDelegate, NSMenuDelegate {
    let store = VirtualMachineStore()
    /// Sidebar width the window is built with. A later resize keeps this width
    /// unless the user has dragged the divider, and gives the rest to the picture.
    private let sidebarWidth: CGFloat = 220
    private var savedSidebarWidth: CGFloat = 220
    private var sidebarShown = true
    private var sidebarItem: NSToolbarItem?
    let display = GuestDisplayView(frame: NSRect(x: 0, y: 0, width: 1024, height: 768))
    var booted = false
    private var sidebar: LibrarySidebar!
    private var split: NSSplitView!
    private var settings: SettingsSheet?
    private var newSheet: NewVMSheet?
    private var loggedPlacement = false

    init() {
        let window = SheepWindow(
            contentRect: NSRect(x: 80, y: 120, width: 1245, height: 768),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "SheepShaver — ctrl-g to release"
        window.titleVisibility = .visible
        window.titlebarAppearsTransparent = false
        window.titlebarSeparatorStyle = .none
        window.isRestorable = false
        window.isReleasedWhenClosed = false
        window.styleMask.remove(.fullSizeContentView)
        super.init(window: window)
        window.delegate = self
        window.guestDisplay = display

        sidebar = LibrarySidebar(
            store: store,
            onPlay: { [weak self] doc in self?.play(doc) },
            onSelect: { [weak self] doc in self?.setSubtitle(doc.name) }
        )
        sidebar.frame = NSRect(x: 0, y: 0, width: sidebarWidth, height: 768)
        display.frame = NSRect(x: sidebarWidth + 1, y: 0, width: 1024, height: 768)

        let split = NSSplitView()
        split.isVertical = true
        split.dividerStyle = .thin
        split.delegate = self
        split.translatesAutoresizingMaskIntoConstraints = false
        split.addArrangedSubview(sidebar)
        split.addArrangedSubview(display)
        split.setHoldingPriority(.defaultHigh + 1, forSubviewAt: 0)
        split.setHoldingPriority(.defaultLow, forSubviewAt: 1)
        self.split = split

        let root = NSView()
        root.addSubview(split)
        window.contentView = root
        let guide = window.contentLayoutGuide as! NSLayoutGuide
        NSLayoutConstraint.activate([
            split.topAnchor.constraint(equalTo: guide.topAnchor),
            split.bottomAnchor.constraint(equalTo: guide.bottomAnchor),
            split.leadingAnchor.constraint(equalTo: guide.leadingAnchor),
            split.trailingAnchor.constraint(equalTo: guide.trailingAnchor)
        ])
        updateMinSize()

        let toolbar = NSToolbar(identifier: "SheepToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        window.toolbar = toolbar
        window.toolbarStyle = .expanded
        window.styleMask.remove(.fullSizeContentView)
        // Toolbar is on before the first layout, so the content size used
        // below is the size the window actually has.
        placePanes(in: split, sidebar: sidebarWidth)
        split.setPosition(sidebarWidth, ofDividerAt: 0)
        window.layoutIfNeeded()
        placePanes(in: split, sidebar: sidebarWidth)
        display.syncPointerToPicture()
        installSidebarMenu()
        if let name = store.document(id: store.selection)?.name {
            baseSubtitle = name
            window.subtitle = name
        }
        NotificationCenter.default.addObserver(self, selector: #selector(shearsStatusChanged(_:)),
                                               name: .shearsStatusChanged, object: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    /// The VM name shown under the title, plus "Sheep Shears" while the guest tool is running.
    private var baseSubtitle = ""

    func setSubtitle(_ name: String) {
        baseSubtitle = name
        refreshSubtitle()
    }

    private func refreshSubtitle() {
        let running = booted && ShearsHost.shared.status.toolRunning
        window?.subtitle = running ? "\(baseSubtitle) · Sheep Shears" : baseSubtitle
    }

    @objc private func shearsStatusChanged(_ note: Notification) {
        refreshSubtitle()
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

    func play(_ doc: VirtualMachineDocument) {
        store.selection = doc.id
        setSubtitle(doc.name)
        if !booted {
            BootGate.path = doc.prefsPath
            store.runningID = doc.id
            sidebar.reload()
            return
        }
        store.launch(doc)
    }

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
        guard let split else { return }
        placePanes(in: split, sidebar: sidebarShown ? savedSidebarWidth : 0)
        display.syncPointerToPicture()
    }

    /// Picture fills whatever width is left after the sidebar. At startup the
    /// sidebar is `sidebarWidth`. After a drag, a resize keeps that dragged width.
    /// Hidden, the picture is the full content width and the divider is gone.
    private func placePanes(in splitView: NSSplitView, sidebar side: CGFloat) {
        let height = splitView.bounds.height
        let width = splitView.bounds.width
        guard width > 1, height > 1 else { return }
        if !sidebarShown {
            sidebar.isHidden = true
            sidebar.setFrameOrigin(.zero)
            sidebar.setFrameSize(NSSize(width: 0, height: height))
            display.setFrameOrigin(.zero)
            display.setFrameSize(NSSize(width: width, height: height))
            return
        }
        sidebar.isHidden = false
        let divider = splitView.dividerThickness
        let clamped = min(max(side, 180), min(320, width - divider - 160))
        let picture = max(160, width - divider - clamped)
        sidebar.setFrameOrigin(.zero)
        sidebar.setFrameSize(NSSize(width: clamped, height: height))
        display.setFrameOrigin(NSPoint(x: clamped + divider, y: 0))
        display.setFrameSize(NSSize(width: picture, height: height))
    }

    private func updateMinSize() {
        guard let split, let window else { return }
        let width = sidebarShown ? 180 + split.dividerThickness + 160 : 160
        window.contentMinSize = NSSize(width: width, height: 160)
    }

    func splitView(_ splitView: NSSplitView, resizeSubviewsWithOldSize oldSize: NSSize) {
        let side = sidebarShown ? savedSidebarWidth : 0
        placePanes(in: splitView, sidebar: side)
    }

    func splitView(_ splitView: NSSplitView, constrainMinCoordinate proposedMinimumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        sidebarShown ? 180 : 0
    }

    func splitView(_ splitView: NSSplitView, constrainMaxCoordinate proposedMaximumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        320
    }

    func splitView(
        _ splitView: NSSplitView,
        constrainSplitPosition proposedPosition: CGFloat,
        ofSubviewAt dividerIndex: Int
    ) -> CGFloat {
        guard sidebarShown else { return 0 }
        let minP = self.splitView(splitView, constrainMinCoordinate: proposedPosition, ofSubviewAt: dividerIndex)
        let maxP = self.splitView(splitView, constrainMaxCoordinate: proposedPosition, ofSubviewAt: dividerIndex)
        let clamped = min(max(proposedPosition, minP), maxP)
        savedSidebarWidth = clamped
        return clamped
    }

    func splitView(_ splitView: NSSplitView, canCollapseSubview subview: NSView) -> Bool {
        subview === sidebar
    }

    func splitView(_ splitView: NSSplitView, shouldHideDividerAt dividerIndex: Int) -> Bool {
        !sidebarShown
    }

    func splitViewDidResizeSubviews(_ notification: Notification) {
        if sidebarShown {
            let width = sidebar.frame.width
            if width >= 180 && width <= 320 {
                savedSidebarWidth = width
            }
        }
        display.syncPointerToPicture()
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.toggleSidebar, .plus, .flexibleSpace, .settings]
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.toggleSidebar, .plus, .flexibleSpace, .settings]
    }

    func toolbar(
        _ toolbar: NSToolbar,
        itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier,
        willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {
        switch itemIdentifier {
        case .toggleSidebar:
            let item = NSToolbarItem(itemIdentifier: .toggleSidebar)
            item.image = NSImage(systemSymbolName: "sidebar.leading", accessibilityDescription: "Hide Sidebar")
            item.label = "Toggle Sidebar"
            item.paletteLabel = "Toggle Sidebar"
            item.toolTip = "Hide Sidebar"
            item.isNavigational = true
            item.action = #selector(toggleSidebar(_:))
            item.target = self
            sidebarItem = item
            return item
        case .plus:
            let item = NSToolbarItem(itemIdentifier: .plus)
            item.image = NSImage(systemSymbolName: "plus", accessibilityDescription: "New VM")
            item.label = "New"
            item.action = #selector(newMachine)
            item.target = self
            return item
        case .settings:
            let item = NSToolbarItem(itemIdentifier: .settings)
            item.image = NSImage(systemSymbolName: "gearshape", accessibilityDescription: "Settings")
            item.label = "Settings"
            item.action = #selector(openSettings)
            item.target = self
            return item
        default:
            return nil
        }
    }

    /// Leading toolbar button, and View > Hide Sidebar (⌃⌘S).
    /// The picture takes the full content width while the sidebar is hidden.
    @objc func toggleSidebar(_ sender: Any?) {
        if sidebarShown {
            let width = sidebar.frame.width
            if width >= 180 { savedSidebarWidth = width }
            sidebarShown = false
        } else {
            sidebarShown = true
        }
        guard let split else { return }
        updateMinSize()
        placePanes(in: split, sidebar: sidebarShown ? savedSidebarWidth : 0)
        window?.layoutIfNeeded()
        display.syncPointerToPicture(recenterRelative: true)
        refreshSidebarChrome()
    }

    @objc func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem.action == #selector(toggleSidebar(_:)) {
            menuItem.title = sidebarShown ? "Hide Sidebar" : "Show Sidebar"
        }
        if menuItem.action == #selector(shutDownGuest(_:)) {
            let status = ShearsHost.shared.status
            if case .requested = status.shutdown { return false }
            return booted && status.toolRunning
        }
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
        let alert = NSAlert()
        alert.messageText = "Install Sheep Shears in the guest"
        alert.informativeText = "Sheep Shears releases the mouse at the screen edge and shares the clipboard with this Mac. Its installer is a small disk. It is added to this virtual machine; after the guest next starts, open “Install Sheep Shears” on the “Sheep Shears” disk and click Install. No restart is needed after that, and the disk can stay or be removed."
        alert.addButton(withTitle: "Add Installer Disk")
        alert.addButton(withTitle: "Show Disk in Finder")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard let self else { return }
            switch response {
            case .alertFirstButtonReturn: self.addInstallerDisk(on: window)
            case .alertSecondButtonReturn:
                if let url = try? SheepShearsInstaller.stagedCopy() { NSWorkspace.shared.activateFileViewerSelecting([url]) }
            default: break
            }
        }
    }

    private func addInstallerDisk(on window: NSWindow) {
        func report(_ title: String, _ text: String) {
            let note = NSAlert()
            note.messageText = title
            note.informativeText = text
            note.beginSheetModal(for: window)
        }
        guard let doc = store.document(id: store.selection) ?? store.document(id: store.runningID) else {
            return report("No virtual machine is selected", "Select the virtual machine in the sidebar first.")
        }
        do {
            let url = try SheepShearsInstaller.stagedCopy()
            let added = try SheepShearsInstaller.addDisk(url, toPrefsAt: doc.prefsPath)
            report(added ? "The installer disk was added" : "The installer disk is already added",
                   added ? "Restart “\(doc.name)” (quit and start it again). Then open “Install Sheep Shears” on the “Sheep Shears” disk."
                         : "Open “Install Sheep Shears” on the “Sheep Shears” disk in the guest.")
        } catch {
            report("The installer disk could not be added", error.localizedDescription)
        }
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

    private func refreshSidebarChrome() {
        let title = sidebarShown ? "Hide Sidebar" : "Show Sidebar"
        sidebarItem?.toolTip = title
        sidebarItem?.image?.accessibilityDescription = title
        guard let menu = NSApp.mainMenu?.item(withTitle: "View")?.submenu else { return }
        for item in menu.items where item.action == #selector(toggleSidebar(_:)) {
            item.title = title
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

    func installSidebarMenu() {
        let main = NSApp.mainMenu ?? NSMenu()
        if NSApp.mainMenu == nil {
            NSApp.mainMenu = main
        }
        installGuestMenu(in: main)
        let viewItem: NSMenuItem
        if let existing = main.items.first(where: { $0.title == "View" }) {
            viewItem = existing
        } else {
            viewItem = NSMenuItem()
            viewItem.title = "View"
            viewItem.submenu = NSMenu(title: "View")
            if let index = main.items.firstIndex(where: { $0.title == "Window" }) {
                main.insertItem(viewItem, at: index)
            } else {
                main.addItem(viewItem)
            }
        }
        let menu = viewItem.submenu ?? NSMenu(title: "View")
        viewItem.submenu = menu
        if menu.items.contains(where: { $0.action == #selector(toggleSidebar(_:)) }) {
            refreshSidebarChrome()
            return
        }
        let item = NSMenuItem(
            title: "Hide Sidebar",
            action: #selector(toggleSidebar(_:)),
            keyEquivalent: "s"
        )
        item.keyEquivalentModifierMask = [.command, .control]
        item.target = self
        menu.addItem(item)
    }

    @objc private func newMachine() {
        guard let window else { return }
        let sheet = NewVMSheet { [weak self] name, disk, rom in
            if let win = self?.newSheet?.window {
                self?.window?.endSheet(win)
            }
            self?.newSheet = nil
            if let name, let disk, let rom, !name.isEmpty {
                _ = self?.store.create(name: name, disk: disk, rom: rom)
                self?.sidebar.reload()
            }
        }
        newSheet = sheet
        if let win = sheet.window {
            window.beginSheet(win)
        }
    }

    @objc private func openSettings() {
        guard let window else { return }
        let prefs = store.document(id: store.selection)?.prefsPath
        let live = booted && store.selection == store.runningID
        let sheet = SettingsSheet(prefsPath: prefs, live: live) { [weak self] saved in
            if let win = self?.settings?.window {
                self?.window?.endSheet(win)
            }
            self?.settings = nil
            if saved && live {
                self?.display.applyMousePrefs()
            }
        }
        settings = sheet
        if let win = sheet.window {
            window.beginSheet(win)
        }
    }

}

private extension NSToolbarItem.Identifier {
    static let plus = NSToolbarItem.Identifier("plus")
    static let settings = NSToolbarItem.Identifier("settings")
}

@MainActor
private final class NewVMSheet: NSWindowController {
    private let onDone: (String?, String?, String?) -> Void
    private let nameField = NSTextField(string: "Mac OS 9")
    private let diskField = NSTextField(string: "")
    private let romField = NSTextField(string: "")

    init(onDone: @escaping (String?, String?, String?) -> Void) {
        self.onDone = onDone
        let win = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 420, height: 180),
            styleMask: [.titled],
            backing: .buffered,
            defer: false
        )
        win.title = "New Virtual Machine"
        super.init(window: win)

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.addArrangedSubview(labeled("Name", nameField))
        stack.addArrangedSubview(labeled("Disk", diskField))
        stack.addArrangedSubview(labeled("ROM", romField))
        let buttons = NSStackView()
        buttons.orientation = .horizontal
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelSheet))
        let create = NSButton(title: "Create", target: self, action: #selector(createVM))
        create.keyEquivalent = "\r"
        buttons.addArrangedSubview(NSView())
        buttons.addArrangedSubview(cancel)
        buttons.addArrangedSubview(create)
        stack.addArrangedSubview(buttons)
        win.contentView = stack
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    private func labeled(_ title: String, _ field: NSTextField) -> NSView {
        let row = NSStackView()
        row.orientation = .horizontal
        let label = NSTextField(labelWithString: title)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 48).isActive = true
        field.widthAnchor.constraint(greaterThanOrEqualToConstant: 300).isActive = true
        row.addArrangedSubview(label)
        row.addArrangedSubview(field)
        return row
    }

    @objc private func cancelSheet() {
        onDone(nil, nil, nil)
    }

    @objc private func createVM() {
        onDone(nameField.stringValue, diskField.stringValue, romField.stringValue)
    }
}
