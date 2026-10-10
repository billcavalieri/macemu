/*
 *  VMLibraryView.swift - The library's sidebar: a source list of the virtual machines.
 *
 *  A row is the VM's icon, its name and, while it runs, a green dot. Double-click or Return starts it (or brings
 *  its window forward), Delete removes it from the library, a right-click offers the rest, and a prefs file or
 *  folder dropped on the list is added. Everything that changes the library goes through `LibraryActions`, so the
 *  window controller owns the menus, sheets and alerts.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
struct LibraryActions {
    var start: (VirtualMachineDocument) -> Void = { _ in }
    var openSettings: (VirtualMachineDocument) -> Void = { _ in }
    var remove: (VirtualMachineDocument) -> Void = { _ in }
    var addFiles: ([URL]) -> Void = { _ in }
    var selectionChanged: () -> Void = {}
}

@MainActor
private final class LibraryTable: NSTableView {
    var onReturn: (() -> Void)?
    var onDelete: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 36, 76: onReturn?()                // Return, Enter
        case 51, 117: onDelete?()               // Delete, forward delete
        default: super.keyDown(with: event)
        }
    }
}

@MainActor
final class LibraryViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, NSMenuDelegate, NSTextFieldDelegate {
    let store: VirtualMachineStore
    var actions = LibraryActions()
    private let table = LibraryTable()
    private var observer: NSObjectProtocol?
    private static let icon: NSImage? = {
        guard let url = Bundle.main.url(forResource: "ClassicMacOS", withExtension: "svg") else { return nil }
        return NSImage(contentsOf: url)
    }()

    init(store: VirtualMachineStore) {
        self.store = store
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    override func loadView() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("vm"))
        column.resizingMask = .autoresizingMask
        table.addTableColumn(column)
        table.headerView = nil
        table.style = .sourceList
        table.rowSizeStyle = .default
        table.allowsEmptySelection = true
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        table.doubleAction = #selector(startClicked(_:))
        table.target = self
        table.onReturn = { [weak self] in self?.startSelected() }
        table.onDelete = { [weak self] in self?.removeSelected() }
        table.registerForDraggedTypes([.fileURL])
        table.setAccessibilityLabel("Virtual machines")
        let menu = NSMenu()
        menu.delegate = self
        table.menu = menu

        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.automaticallyAdjustsContentInsets = true
        view = scroll

        observer = NotificationCenter.default.addObserver(forName: .vmRunStateChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.reload() }
        }
        reload()
    }

    // MARK: content

    func reload() {
        table.reloadData()
        if let id = store.selection, let row = store.machines.firstIndex(where: { $0.id == id }) {
            table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        }
    }

    func focus() {
        view.window?.makeFirstResponder(table)
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        store.machines.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let machine = store.machines[row]
        let running = store.isRunning(machine.id)
        let identifier = NSUserInterfaceItemIdentifier("vmCell")
        let cell = (tableView.makeView(withIdentifier: identifier, owner: self) as? NSTableCellView) ?? makeCell(identifier)
        cell.textField?.stringValue = machine.name
        cell.imageView?.image = Self.icon
        if let dot = cell.subviews.compactMap({ $0 as? NSImageView }).first(where: { $0 !== cell.imageView }) {
            dot.isHidden = !running
        }
        cell.setAccessibilityLabel(running ? "\(machine.name), running" : machine.name)
        return cell
    }

    private func makeCell(_ identifier: NSUserInterfaceItemIdentifier) -> NSTableCellView {
        let cell = NSTableCellView()
        cell.identifier = identifier
        let image = NSImageView()
        image.imageScaling = .scaleProportionallyUpOrDown
        image.translatesAutoresizingMaskIntoConstraints = false
        image.setAccessibilityElement(false)
        let title = NSTextField(labelWithString: "")
        title.lineBreakMode = .byTruncatingTail
        title.isEditable = false
        title.delegate = self
        title.translatesAutoresizingMaskIntoConstraints = false
        let dot = NSImageView(image: NSImage(systemSymbolName: "circle.fill", accessibilityDescription: "Running") ?? NSImage())
        dot.contentTintColor = .systemGreen
        dot.symbolConfiguration = .init(pointSize: 7, weight: .regular)
        dot.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(image)
        cell.addSubview(title)
        cell.addSubview(dot)
        cell.imageView = image
        cell.textField = title
        NSLayoutConstraint.activate([
            image.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
            image.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            image.widthAnchor.constraint(equalToConstant: 18),
            image.heightAnchor.constraint(equalToConstant: 22),
            title.leadingAnchor.constraint(equalTo: image.trailingAnchor, constant: 8),
            title.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            title.trailingAnchor.constraint(lessThanOrEqualTo: dot.leadingAnchor, constant: -6),
            dot.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
            dot.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = table.selectedRow
        store.selection = (row >= 0 && row < store.machines.count) ? store.machines[row].id : nil
        actions.selectionChanged()
    }

    // MARK: actions on a row

    private var target: VirtualMachineDocument? {
        let row = table.clickedRow >= 0 ? table.clickedRow : table.selectedRow
        return row >= 0 && row < store.machines.count ? store.machines[row] : nil
    }

    @objc private func startClicked(_ sender: Any?) {
        if table.clickedRow >= 0, let doc = target { actions.start(doc) }
    }

    private func startSelected() {
        if let doc = target { actions.start(doc) }
    }

    private func removeSelected() {
        if let doc = target { actions.remove(doc) }
    }

    @objc private func startFromMenu(_ sender: Any?) { startSelected() }
    @objc private func settingsFromMenu(_ sender: Any?) { if let doc = target { actions.openSettings(doc) } }
    @objc private func removeFromMenu(_ sender: Any?) { removeSelected() }

    @objc private func showInFinder(_ sender: Any?) {
        guard let doc = target else { return }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: doc.prefsPath)])
    }

    /// Edits the name in place (Return or clicking elsewhere ends it; Escape leaves the name as it was).
    @objc func renameSelected(_ sender: Any?) {
        guard let doc = target, let row = store.machines.firstIndex(where: { $0.id == doc.id }),
              let cell = table.view(atColumn: 0, row: row, makeIfNecessary: true) as? NSTableCellView,
              let field = cell.textField else { return }
        field.isEditable = true
        view.window?.makeFirstResponder(field)
    }

    func controlTextDidEndEditing(_ notification: Notification) {
        guard let field = notification.object as? NSTextField else { return }
        field.isEditable = false
        let row = table.row(for: field)
        guard row >= 0, row < store.machines.count else { return }
        let machine = store.machines[row]
        let cancelled = (notification.userInfo?["NSTextMovement"] as? Int) == NSCancelTextMovement
        if !cancelled { store.rename(machine.id, to: field.stringValue) }
        reload()
        actions.selectionChanged()
    }

    // MARK: context menu

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        guard let doc = target else { return }
        let running = store.isRunning(doc.id)
        func add(_ title: String, _ selector: Selector) {
            let item = NSMenuItem(title: title, action: selector, keyEquivalent: "")
            item.target = self
            menu.addItem(item)
        }
        add(running ? "Show Window" : "Start", #selector(startFromMenu(_:)))
        menu.addItem(.separator())
        add("Settings…", #selector(settingsFromMenu(_:)))
        add("Rename", #selector(renameSelected(_:)))
        add("Show in Finder", #selector(showInFinder(_:)))
        menu.addItem(.separator())
        add("Remove from Library…", #selector(removeFromMenu(_:)))
    }

    // MARK: dropping prefs files

    private func droppedURLs(_ info: NSDraggingInfo) -> [URL] {
        (info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL]) ?? []
    }

    func tableView(_ tableView: NSTableView, validateDrop info: NSDraggingInfo, proposedRow row: Int,
                   proposedDropOperation dropOperation: NSTableView.DropOperation) -> NSDragOperation {
        guard !droppedURLs(info).isEmpty else { return [] }
        tableView.setDropRow(-1, dropOperation: .on)        // the whole list takes the drop, not one row
        return .copy
    }

    func tableView(_ tableView: NSTableView, acceptDrop info: NSDraggingInfo, row: Int,
                   dropOperation: NSTableView.DropOperation) -> Bool {
        let urls = droppedURLs(info)
        guard !urls.isEmpty else { return false }
        actions.addFiles(urls)
        return true
    }
}
