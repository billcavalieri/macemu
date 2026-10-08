/*
 *  VMLibraryView.swift - AppKit sidebar list of VM documents.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class LibrarySidebar: NSView, NSTableViewDataSource, NSTableViewDelegate {
    let store: VirtualMachineStore
    var onPlay: (VirtualMachineDocument) -> Void
    var onSelect: (VirtualMachineDocument) -> Void
    private let table = NSTableView()
    private let scroll = NSScrollView()

    init(store: VirtualMachineStore,
         onPlay: @escaping (VirtualMachineDocument) -> Void,
         onSelect: @escaping (VirtualMachineDocument) -> Void) {
        self.store = store
        self.onPlay = onPlay
        self.onSelect = onSelect
        super.init(frame: NSRect(x: 0, y: 0, width: 220, height: 480))
        let name = NSTableColumn(identifier: LibrarySidebar.nameColumn)
        name.title = "Virtual Machines"
        name.resizingMask = .autoresizingMask
        table.addTableColumn(name)
        let play = NSTableColumn(identifier: LibrarySidebar.playColumn)
        play.title = ""
        play.width = 64
        play.minWidth = 64
        play.maxWidth = 64
        play.resizingMask = []
        table.addTableColumn(play)
        table.headerView = nil
        table.rowHeight = 36
        table.dataSource = self
        table.delegate = self
        table.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle
        table.allowsEmptySelection = false
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scroll)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: topAnchor),
            scroll.leadingAnchor.constraint(equalTo: leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: bottomAnchor)
        ])
        reload()
        NotificationCenter.default.addObserver(forName: .vmRunStateChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.reload() }
        }
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func reload() {
        table.reloadData()
        if let id = store.selection, let row = store.machines.firstIndex(where: { $0.id == id }) {
            table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        store.machines.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let machine = store.machines[row]
        if tableColumn?.identifier == Self.playColumn {
            let button = NSButton(title: store.isRunning(machine.id) ? "Show" : "Play", target: self, action: #selector(playClicked(_:)))
            button.bezelStyle = .rounded
            button.controlSize = .small
            button.tag = row
            return button
        }
        let cell = NSTableCellView()
        let image = NSImageView()
        if let url = Bundle.main.url(forResource: "ClassicMacOS", withExtension: "svg") {
            image.image = NSImage(contentsOf: url)
        }
        image.imageScaling = .scaleProportionallyUpOrDown
        image.translatesAutoresizingMaskIntoConstraints = false
        let title = NSTextField(labelWithString: machine.name)
        title.lineBreakMode = .byTruncatingTail
        title.translatesAutoresizingMaskIntoConstraints = false
        let status = NSTextField(labelWithString: store.isRunning(machine.id) ? "Running" : "")
        status.textColor = .secondaryLabelColor
        status.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        status.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(image)
        cell.addSubview(title)
        cell.addSubview(status)
        NSLayoutConstraint.activate([
            image.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            image.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            image.widthAnchor.constraint(equalToConstant: 20),
            image.heightAnchor.constraint(equalToConstant: 24),
            title.leadingAnchor.constraint(equalTo: image.trailingAnchor, constant: 6),
            title.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
            title.topAnchor.constraint(equalTo: cell.topAnchor, constant: 2),
            status.leadingAnchor.constraint(equalTo: title.leadingAnchor),
            status.topAnchor.constraint(equalTo: title.bottomAnchor),
            status.trailingAnchor.constraint(equalTo: title.trailingAnchor)
        ])
        cell.textField = title
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = table.selectedRow
        guard row >= 0, row < store.machines.count else { return }
        let machine = store.machines[row]
        store.selection = machine.id
        onSelect(machine)
    }

    @objc private func playClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < store.machines.count else { return }
        let machine = store.machines[row]
        store.selection = machine.id
        table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        onPlay(machine)
    }

    private static let nameColumn = NSUserInterfaceItemIdentifier("name")
    private static let playColumn = NSUserInterfaceItemIdentifier("play")
}
