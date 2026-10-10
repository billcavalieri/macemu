/*
 *  ManagerWindowController.swift - The library window of the manager process (SheepShaver started with no --config):
 *  a sidebar with the virtual machines and, next to it, the selected one: its picture and keyboard/mouse when it runs
 *  embedded (the default; see RemoteDisplayView), else what it is made of. It starts and shuts down VMs (each runs in
 *  its own process) and never runs one itself.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

/// The right-hand pane: the selected VM's info, or its live picture when it is embedded and running.
@MainActor
final class ManagerContentViewController: NSViewController {
    let info: VMDetailViewController
    let container = DisplayAspectContainer()

    init(info: VMDetailViewController) {
        self.info = info
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    override func loadView() {
        let root = NSView()
        addChild(info)
        for v in [info.view, container] {
            v.frame = root.bounds
            v.autoresizingMask = [.width, .height]
            root.addSubview(v)
        }
        container.isHidden = true
        view = root
    }

    override func viewDidLayout() {
        super.viewDidLayout()
        container.insets = view.safeAreaInsets
    }

    func show(_ display: RemoteDisplayView?) {
        container.show(display)
        container.isHidden = display == nil
        info.view.isHidden = display != nil
    }
}

@MainActor
final class ManagerWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSMenuDelegate, NSMenuItemValidation, NSToolbarItemValidation {
    let store = VirtualMachineStore()
    private let library: LibraryViewController
    private let detail: VMDetailViewController
    private let content: ManagerContentViewController
    private let splitController = NSSplitViewController()
    /// The embedded VMs this window shows, by VM id, and the ones it is waiting to come up.
    private var sessions: [String: RemoteDisplayView] = [:]
    private var connecting: [String: Timer] = [:]
    private var shownDisplay: RemoteDisplayView?
    private var newSheet: NewVMSheet?
    private var settingsSheet: SettingsSheet?
    private var appSettings: AppSettingsWindowController?
    private var pollTimer: Timer?
    private var startItem: NSToolbarItem?
    private var shutDownItem: NSToolbarItem?
    private var detachItem: NSToolbarItem?

    private static let addItem = NSToolbarItem.Identifier("add")
    private static let startItemID = NSToolbarItem.Identifier("start")
    private static let detachItemID = NSToolbarItem.Identifier("detach")
    private static let shutDownItemID = NSToolbarItem.Identifier("shutdown")
    private static let settingsItem = NSToolbarItem.Identifier("settings")

    init() {
        library = LibraryViewController(store: store)
        detail = VMDetailViewController(store: store)
        content = ManagerContentViewController(info: detail)
        let window = SheepWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 580),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
                              backing: .buffered, defer: false)
        super.init(window: window)

        let sidebarItem = NSSplitViewItem(sidebarWithViewController: library)
        sidebarItem.minimumThickness = 200
        sidebarItem.maximumThickness = 320
        sidebarItem.canCollapse = true
        let contentItem = NSSplitViewItem(contentListWithViewController: content)
        contentItem.minimumThickness = 420
        splitController.addSplitViewItem(sidebarItem)
        splitController.addSplitViewItem(contentItem)
        splitController.splitView.autosaveName = "SheepShaverLibrarySplit"

        window.contentViewController = splitController
        window.title = "SheepShaver"
        window.setContentSize(NSSize(width: 900, height: 580))
        window.contentMinSize = NSSize(width: 700, height: 520)
        window.isReleasedWhenClosed = false
        window.isRestorable = false
        window.delegate = self
        if !window.setFrameUsingName("SheepShaverLibrary") { window.center() }
        window.setFrameAutosaveName("SheepShaverLibrary")

        let toolbar = NSToolbar(identifier: "SheepShaverLibraryToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        window.toolbar = toolbar

        library.actions = LibraryActions(
            start: { [weak self] doc in self?.start(doc) },
            openSettings: { [weak self] doc in self?.openSettings(for: doc) },
            remove: { [weak self] doc in self?.confirmRemove(doc) },
            addFiles: { [weak self] urls in self?.add(urls) },
            selectionChanged: { [weak self] in self?.selectionChanged() })
        detail.actions = DetailActions(
            start: { [weak self] doc in self?.start(doc) },
            shutDown: { [weak self] doc in self?.confirmShutDown(doc) },
            openSettings: { [weak self] doc in self?.openSettings(for: doc) },
            attach: { [weak self] doc in self?.setDisplayMode(doc, inOwnWindow: false) },
            isDetached: { [weak self] doc in self?.sessions[doc.id]?.inOwnWindow == true },
            newMachine: { [weak self] in self?.newMachine(nil) },
            addExisting: { [weak self] in self?.addExistingMachine(nil) })

        NotificationCenter.default.addObserver(forName: .vmRunStateChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refreshChrome() }
        }
        pollTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.store.pollRunning() }
        }
        store.pollRunning()
        refreshChrome()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    override func showWindow(_ sender: Any?) {
        super.showWindow(sender)
        library.focus()
    }

    // MARK: window

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if !sessions.isEmpty {
            // Embedded VMs have no window of their own: say what closing the library does to them
            let names = sessions.keys.compactMap { store.document(id: $0)?.name }.sorted()
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = names.count == 1 ? "“\(names[0])” is still running" : "\(names.count) virtual machines are still running"
            alert.informativeText = "Closing the library leaves them running without a window; they are shown again the next time the library opens. The MCP server stops with the library."
            alert.addButton(withTitle: "Close and Keep Running")
            alert.addButton(withTitle: "Cancel")
            alert.beginSheetModal(for: sender) { [weak self] response in
                guard response == .alertFirstButtonReturn else { return }
                MainActor.assumeIsolated { self?.finishClosing() }
            }
            return false
        }
        BootGate.quit = true            // the library is this process's only window: closing it ends the process
        return true
    }

    private func finishClosing() {
        sessions.values.forEach { $0.disconnect() }
        sessions.removeAll()
        BootGate.quit = true
        window?.close()
    }

    /// Redraws everything that shows the library or its state.
    func refreshLibrary() {
        library.reload()
        refreshChrome()
    }

    private func selectionChanged() {
        refreshChrome()
        showSelected()
    }

    // MARK: embedded VMs

    /// Shows the selected VM's picture when it is embedded and connected, else its info.
    private func showSelected() {
        var display = store.selection.flatMap { sessions[$0] }
        if display?.inOwnWindow == true { display = nil }          // it is in its own window: the info pane says so
        if shownDisplay !== display { shownDisplay?.releaseCapture("switched away") }
        shownDisplay = display
        content.show(display)
        (window as? SheepWindow)?.guestDisplay = display
        if let display { window?.makeFirstResponder(display) }
    }

    /// Waits for a VM that was just started embedded to open its display socket, then shows it.
    private func beginConnecting(_ doc: VirtualMachineDocument) {
        guard sessions[doc.id] == nil, connecting[doc.id] == nil else { return }
        var tries = 0
        let timer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                tries += 1
                if self.connect(doc) || tries > 800 || (tries > 100 && !self.store.isRunning(doc.id)) {
                    self.connecting[doc.id]?.invalidate()
                    self.connecting[doc.id] = nil
                }
            }
        }
        connecting[doc.id] = timer
    }

    /// Connects to a running embedded VM (true when it is connected). Also used to pick up VMs that were already running.
    @discardableResult
    private func connect(_ doc: VirtualMachineDocument) -> Bool {
        if sessions[doc.id] != nil { return true }
        guard let link = RemoteGuestLink(vmID: doc.id, prefsPath: doc.prefsPath) else { return false }
        let view = RemoteDisplayView(link: link, vmID: doc.id)
        view.onGuestEvent = { [weak self] event in self?.guestEvent(event, from: doc) }
        link.onClosed = { [weak self, weak view] in
            guard let self, let view else { return }
            if nwDiagnosticsOn { NSLog("NW-MANAGER display closed %@", doc.id) }
            view.disconnect()
            if self.sessions[doc.id] === view { self.sessions[doc.id] = nil }
            self.refreshChrome()
            self.showSelected()
            // The VM is still there: a guest restart re-executes its process, so its sockets vanish for a few seconds.
            // Connect again as soon as the new image is listening.
            if self.store.isRunning(doc.id) { self.beginConnecting(doc) }
        }
        sessions[doc.id] = view
        if nwDiagnosticsOn {
            NSLog("NW-MANAGER display connected %@", doc.id)
            // Test hook (tools/vms/test.sh embedded): move the guest pointer through this window's link
            if let poke = ProcessInfo.processInfo.environment["NW_MANAGER_POKE"] {
                let xy = poke.split(separator: ",").compactMap { Int32($0) }
                if xy.count == 2 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 20) {
                        MainActor.assumeIsolated {
                            link.mouseAbs(xy[0], xy[1])
                            NSLog("NW-MANAGER poked %d,%d", xy[0], xy[1])
                        }
                    }
                }
            }
        }
        refreshChrome()
        if store.selection == doc.id { showSelected() }
        return true
    }

    /// VMs that are running embedded but not yet shown here (started by an earlier library window or by MCP).
    private func adoptRunning() {
        for doc in store.machines where sessions[doc.id] == nil && connecting[doc.id] == nil
            && store.isRunning(doc.id) && VirtualMachineStore.hasDisplaySocket(doc.id) {
            connect(doc)
        }
    }

    private func guestEvent(_ event: DisplayEvent, from doc: VirtualMachineDocument) {
        switch event {
        case .problem(let text):
            guard let window else { return }
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = "A problem starting “\(doc.name)”"
            alert.informativeText = text
            alert.beginSheetModal(for: window)
        case .shearsStatus:
            window?.toolbar?.validateVisibleItems()
        case .displayMode:
            refreshChrome()
            showSelected()
        default:
            break
        }
    }

    private func refreshChrome() {
        adoptRunning()
        detail.refresh()
        let doc = store.document(id: store.selection)
        window?.subtitle = doc?.name ?? ""
        let running = doc.map { store.isRunning($0.id) } ?? false
        startItem?.label = running ? "Show" : "Start"
        startItem?.toolTip = running ? "Show the virtual machine's window" : "Start the virtual machine"
        startItem?.image = NSImage(systemSymbolName: running ? "macwindow" : "play.fill", accessibilityDescription: running ? "Show" : "Start")
        let detached = store.selection.flatMap { sessions[$0]?.inOwnWindow } == true
        detachItem?.label = detached ? "Attach" : "Detach"
        detachItem?.toolTip = detached ? "Move the virtual machine back into this window" : "Open the virtual machine in its own window"
        detachItem?.image = NSImage(systemSymbolName: detached ? "arrow.down.backward.square" : "arrow.up.forward.square", accessibilityDescription: detached ? "Attach" : "Detach")
        window?.toolbar?.validateVisibleItems()
    }

    // MARK: toolbar

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.toggleSidebar, Self.addItem, .sidebarTrackingSeparator, .flexibleSpace, Self.startItemID, Self.detachItemID, Self.shutDownItemID, Self.settingsItem]
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
        case Self.addItem:
            let item = NSMenuToolbarItem(itemIdentifier: id)
            item.label = "Add"
            item.paletteLabel = "Add"
            item.toolTip = "Create a virtual machine, or add one you already have"
            item.image = NSImage(systemSymbolName: "plus", accessibilityDescription: "Add a virtual machine")
            item.showsIndicator = false
            let menu = NSMenu()
            let create = NSMenuItem(title: "New Virtual Machine…", action: #selector(newMachine(_:)), keyEquivalent: "")
            let existing = NSMenuItem(title: "Add Existing…", action: #selector(addExistingMachine(_:)), keyEquivalent: "")
            create.target = self
            existing.target = self
            menu.addItem(create)
            menu.addItem(existing)
            item.menu = menu
            return item
        case Self.startItemID:
            let item = make("Start", "play.fill", "Start the virtual machine", #selector(startSelected(_:)))
            startItem = item
            return item
        case Self.detachItemID:
            let item = make("Detach", "arrow.up.forward.square", "Open the virtual machine in its own window", #selector(toggleDetach(_:)))
            detachItem = item
            return item
        case Self.shutDownItemID:
            let item = make("Shut Down", "power", "Shut down the virtual machine", #selector(shutDownSelected(_:)))
            shutDownItem = item
            return item
        case Self.settingsItem:
            return make("Settings", "gearshape", "Settings for the virtual machine", #selector(openMachineSettings(_:)))
        default:
            return nil
        }
    }

    func validateToolbarItem(_ item: NSToolbarItem) -> Bool {
        validate(action: item.action)
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        if item.action == #selector(startSelected(_:)) {
            item.title = selectedIsRunning ? "Show Window" : "Start"
        }
        return validate(action: item.action)
    }

    private var selected: VirtualMachineDocument? { store.document(id: store.selection) }
    private var selectedIsRunning: Bool { selected.map { store.isRunning($0.id) } ?? false }

    private func validate(action: Selector?) -> Bool {
        switch action {
        case #selector(startSelected(_:)), #selector(openMachineSettings(_:)), #selector(showSelectedInFinder(_:)),
             #selector(renameSelected(_:)):
            return selected != nil
        case #selector(shutDownSelected(_:)):
            return selected != nil && selectedIsRunning
        case #selector(toggleEdgeRelease(_:)), #selector(toggleClipboard(_:)), #selector(toggleDetach(_:)):
            return selectedLink != nil
        case #selector(installSheepShears(_:)):
            return selected != nil
        case #selector(removeSelected(_:)):
            return selected != nil
        default:
            return true
        }
    }

    // MARK: actions

    private func start(_ doc: VirtualMachineDocument) {
        store.selection = doc.id
        if sessions[doc.id] != nil {                // already running here: show it
            library.reload()
            showSelected()
            return
        }
        let embedded = VirtualMachineStore.opensInLibraryWindow
        if store.launch(doc, embedded: embedded), embedded { beginConnecting(doc) }
        refreshChrome()
        showSelected()
    }

    @objc func startSelected(_ sender: Any?) {
        if let doc = selected { start(doc) }
    }

    @objc func newMachine(_ sender: Any?) {
        guard let window, newSheet == nil else { return }
        let sheet = NewVMSheet { [weak self] name, disk, rom in
            guard let self else { return }
            if let win = self.newSheet?.window { self.window?.endSheet(win) }
            self.newSheet = nil
            if let name, let disk, let rom, !name.isEmpty {
                _ = self.store.create(name: name, disk: disk, rom: rom)
                self.refreshLibrary()
            }
        }
        newSheet = sheet
        if let win = sheet.window { window.beginSheet(win) }
    }

    /// Adds VMs that already have a prefs file (for example one started with --config) to the library.
    @objc func addExistingMachine(_ sender: Any?) {
        guard let window else { return }
        let panel = NSOpenPanel()
        panel.title = "Add Existing Virtual Machine"
        panel.message = "Choose a SheepShaver prefs file, or a folder that contains one called “prefs”."
        panel.prompt = "Add"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.treatsFilePackagesAsDirectories = true
        panel.beginSheetModal(for: window) { [weak self] response in
            guard response == .OK else { return }
            MainActor.assumeIsolated { self?.add(panel.urls) }
        }
    }

    private func add(_ urls: [URL]) {
        var failure: String?
        for url in urls {
            do { try store.addExisting(url) } catch { failure = error.localizedDescription }
        }
        refreshLibrary()
        if let failure, let window {
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = "Could not add the virtual machine"
            alert.informativeText = failure
            alert.beginSheetModal(for: window)
        }
    }

    @objc func openMachineSettings(_ sender: Any?) {
        if let doc = selected { openSettings(for: doc) }
    }

    private func openSettings(for doc: VirtualMachineDocument) {
        guard let window, settingsSheet == nil else { return }
        let sheet = SettingsSheet(prefsPath: doc.prefsPath, running: store.isRunning(doc.id), live: false) { [weak self] _ in
            guard let self else { return }
            if let win = self.settingsSheet?.window { self.window?.endSheet(win) }
            self.settingsSheet = nil
            self.refreshLibrary()
        }
        settingsSheet = sheet
        if let win = sheet.window { window.beginSheet(win) }
    }

    /// Test hook: saves the open settings sheet as the Save button would.
    func saveSettingsForTest() { settingsSheet?.saveForTest() }

    @objc func renameSelected(_ sender: Any?) {
        library.renameSelected(sender)
    }

    @objc func showSelectedInFinder(_ sender: Any?) {
        guard let doc = selected else { return }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: doc.prefsPath)])
    }

    @objc func removeSelected(_ sender: Any?) {
        if let doc = selected { confirmRemove(doc) }
    }

    private func confirmRemove(_ doc: VirtualMachineDocument) {
        guard let window else { return }
        let alert = NSAlert()
        if store.isRunning(doc.id) {
            alert.messageText = "“\(doc.name)” is running"
            alert.informativeText = "Shut it down before removing it from the library."
            alert.addButton(withTitle: "OK")
            alert.beginSheetModal(for: window)
            return
        }
        alert.alertStyle = .warning
        alert.messageText = "Remove “\(doc.name)” from the library?"
        alert.informativeText = "Its settings move to the Trash. Its disk images are not deleted."
        let remove = alert.addButton(withTitle: "Move to Trash")
        remove.hasDestructiveAction = true
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard response == .alertFirstButtonReturn else { return }
            MainActor.assumeIsolated {
                guard let self else { return }
                do { try self.store.remove(doc.id) } catch {
                    let failed = NSAlert(error: error)
                    failed.messageText = "“\(doc.name)” could not be removed"
                    failed.beginSheetModal(for: window)
                }
                self.refreshLibrary()
            }
        }
    }

    @objc func shutDownSelected(_ sender: Any?) {
        if let doc = selected { confirmShutDown(doc) }
    }

    /// Asks Mac OS to shut down (through the Sheep Shears tool in the guest). Without the tool the VM says so, and
    /// the user can stop it anyway, like pulling the plug.
    private func confirmShutDown(_ doc: VirtualMachineDocument) {
        guard let window else { return }
        let alert = NSAlert()
        alert.messageText = "Shut down “\(doc.name)”?"
        alert.informativeText = "Mac OS asks every open application to quit and save, as for Special > Shut Down. An application with unsaved work asks about it in the virtual machine's window."
        alert.addButton(withTitle: "Shut Down")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard response == .alertFirstButtonReturn else { return }
            MainActor.assumeIsolated { self?.shutDown(doc, force: false) }
        }
    }

    private func shutDown(_ doc: VirtualMachineDocument, force: Bool) {
        store.shutDown(doc, force: force) { [weak self] failure in
            guard let self, let failure, let window = self.window else { return }
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = force ? "“\(doc.name)” could not be stopped" : "“\(doc.name)” could not be shut down cleanly"
            alert.informativeText = failure.prefix(1).uppercased() + failure.dropFirst() + "."
            if !force {
                let stop = alert.addButton(withTitle: "Stop Immediately")
                stop.hasDestructiveAction = true
                alert.addButton(withTitle: "Cancel")
            } else {
                alert.addButton(withTitle: "OK")
            }
            alert.beginSheetModal(for: window) { response in
                guard !force, response == .alertFirstButtonReturn else { return }
                MainActor.assumeIsolated { self.shutDown(doc, force: true) }
            }
        }
    }

    // MARK: Guest menu (the selected VM, when it is shown in this window)

    private var selectedLink: RemoteGuestLink? { store.selection.flatMap { sessions[$0]?.remoteLink } }

    func menuNeedsUpdate(_ menu: NSMenu) {
        guard menu.title == "Guest" else { return }
        let link = selectedLink
        if let status = menu.items.first {
            if let link {
                status.title = link.toolRunning ? "Sheep Shears: running (version \(link.toolVersion))" : "Sheep Shears: not running in the guest"
            } else {
                status.title = selected == nil ? "No virtual machine selected" : "Guest not shown in this window"
            }
        }
        for item in menu.items {
            let flag: ShearsFeatures
            let title: String
            if item.action == #selector(toggleDetach(_:)) {
                item.title = store.selection.flatMap { sessions[$0]?.inOwnWindow } == true ? "Attach to Library Window" : "Detach into Its Own Window"
                continue
            }
            switch item.action {
            case #selector(toggleEdgeRelease(_:)): flag = .edgeRelease; title = "Release the Mouse at the Screen Edge"
            case #selector(toggleClipboard(_:)): flag = .clipboard; title = "Share the Clipboard"
            default: continue
            }
            item.state = link?.hostFeatures.contains(flag) == true ? .on : .off
            item.title = title + (link?.guestFeatures.contains(flag) == false ? " (off in the guest's Sheep Shears control panel)" : "")
        }
    }

    /// Detach: the VM opens its own window. Attach: it goes back into this one.
    @objc func toggleDetach(_ sender: Any?) {
        guard let doc = selected, let view = sessions[doc.id] else { return }
        setDisplayMode(doc, inOwnWindow: !view.inOwnWindow)
    }

    private func setDisplayMode(_ doc: VirtualMachineDocument, inOwnWindow: Bool) {
        let id = doc.id
        DispatchQueue.global(qos: .userInitiated).async {
            _ = try? VMControlClient.call(vmID: id, op: "display", args: ["mode": inOwnWindow ? "window" : "embedded"], timeout: 10)
        }
    }

    @objc func toggleEdgeRelease(_ sender: Any?) { toggle(.edgeRelease, key: "edge_release") }
    @objc func toggleClipboard(_ sender: Any?) { toggle(.clipboard, key: "clipboard") }

    private func toggle(_ flag: ShearsFeatures, key: String) {
        guard let doc = selected, let link = sessions[doc.id]?.remoteLink else { return }
        let args: [String: Any] = [key: !link.hostFeatures.contains(flag)]
        let id = doc.id
        DispatchQueue.global(qos: .userInitiated).async { _ = try? VMControlClient.call(vmID: id, op: "features", args: args, timeout: 5) }
    }

    @objc func installSheepShears(_ sender: Any?) {
        guard let doc = selected, let window else { return }
        ShearsInstallFlow.run(on: window, prefsPath: doc.prefsPath, vmName: doc.name)
    }

    /// SheepShaver > Quit: closing the library window, which asks what to do about embedded VMs that are still running.
    @objc func quitLibrary(_ sender: Any?) {
        window?.performClose(sender)
    }

    @objc func openAppSettings(_ sender: Any?) {
        if appSettings == nil { appSettings = AppSettingsWindowController(store: store) }
        appSettings?.present()
    }
}
