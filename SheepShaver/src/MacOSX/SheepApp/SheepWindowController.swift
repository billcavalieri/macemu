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
final class SheepWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSSplitViewDelegate {
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
            window.subtitle = name
        }
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func setSubtitle(_ name: String) {
        window?.subtitle = name
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
        return true
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

    func installSidebarMenu() {
        let main = NSApp.mainMenu ?? NSMenu()
        if NSApp.mainMenu == nil {
            NSApp.mainMenu = main
        }
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
