/*
 *  VMSettingsView.swift - AppKit settings sheet. Reads and writes the prefs file.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class SettingsSheet: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let prefsPath: String?
    private let live: Bool
    private let onClose: (Bool) -> Void
    private let pages: [(title: String, rows: [PrefRow])]
    private let pageTable = NSTableView()
    private let detail = NSStackView()
    private var controls: [String: NSControl] = [:]
    private var pageRows: [[NSView]] = []
    private var saved = false

    init(prefsPath: String?, live: Bool, onClose: @escaping (Bool) -> Void) {
        self.prefsPath = prefsPath
        self.live = live
        self.onClose = onClose
        pages = Self.makePages()
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 720, height: 520),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = "Settings"
        super.init(window: window)
        build(window)
        pageRows = pages.map { page in page.rows.map { rowView($0) } }
        load()
        pageTable.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        showPage(0)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func numberOfRows(in tableView: NSTableView) -> Int { pages.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let cell = NSTableCellView()
        let label = NSTextField(labelWithString: pages[row].title)
        label.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(label)
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 8),
            label.centerYAnchor.constraint(equalTo: cell.centerYAnchor)
        ])
        cell.textField = label
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = pageTable.selectedRow
        if row >= 0 { showPage(row) }
    }

    private func build(_ window: NSWindow) {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("page"))
        column.width = 160
        pageTable.addTableColumn(column)
        pageTable.headerView = nil
        pageTable.dataSource = self
        pageTable.delegate = self
        let pageScroll = NSScrollView()
        pageScroll.documentView = pageTable
        pageScroll.hasVerticalScroller = true
        pageScroll.translatesAutoresizingMaskIntoConstraints = false

        detail.orientation = .vertical
        detail.alignment = .leading
        detail.spacing = 8
        detail.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        detail.translatesAutoresizingMaskIntoConstraints = false
        let detailScroll = NSScrollView()
        detailScroll.documentView = detail
        detailScroll.hasVerticalScroller = true
        detailScroll.drawsBackground = false
        detailScroll.translatesAutoresizingMaskIntoConstraints = false

        let split = NSSplitView()
        split.isVertical = true
        split.dividerStyle = .thin
        split.translatesAutoresizingMaskIntoConstraints = false
        split.addArrangedSubview(pageScroll)
        split.addArrangedSubview(detailScroll)

        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelSheet))
        let save = NSButton(title: "Save", target: self, action: #selector(saveSheet))
        save.keyEquivalent = "\r"
        let buttons = NSStackView(views: [NSView(), cancel, save])
        buttons.orientation = .horizontal
        buttons.translatesAutoresizingMaskIntoConstraints = false

        let root = NSView()
        root.addSubview(split)
        root.addSubview(buttons)
        window.contentView = root
        NSLayoutConstraint.activate([
            split.topAnchor.constraint(equalTo: root.topAnchor),
            split.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            split.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            buttons.topAnchor.constraint(equalTo: split.bottomAnchor, constant: 8),
            buttons.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 16),
            buttons.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -16),
            buttons.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -12),
            pageScroll.widthAnchor.constraint(equalToConstant: 180),
            detail.widthAnchor.constraint(greaterThanOrEqualToConstant: 480)
        ])
    }

    private func showPage(_ index: Int) {
        for view in detail.arrangedSubviews {
            detail.removeArrangedSubview(view)
            view.removeFromSuperview()
        }
        guard index >= 0, index < pageRows.count else { return }
        for view in pageRows[index] {
            detail.addArrangedSubview(view)
        }
    }

    private func rowView(_ row: PrefRow) -> NSView {
        let line = NSStackView()
        line.orientation = .horizontal
        line.alignment = .centerY
        let label = NSTextField(labelWithString: row.label)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 160).isActive = true
        line.addArrangedSubview(label)
        let control = makeControl(row)
        controls[row.key] = control
        line.addArrangedSubview(control)
        if row.key == "mouse" {
            let wrap = NSStackView()
            wrap.orientation = .vertical
            wrap.alignment = .leading
            wrap.addArrangedSubview(line)
            let note = NSTextField(wrappingLabelWithString: "Click the picture to grab. ctrl-g releases.")
            note.textColor = .secondaryLabelColor
            note.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
            note.preferredMaxLayoutWidth = 360
            wrap.addArrangedSubview(note)
            return wrap
        }
        return line
    }

    private func makeControl(_ row: PrefRow) -> NSControl {
        switch row.kind {
        case .toggle, .mouse:
            let button = NSButton(checkboxWithTitle: "", target: nil, action: nil)
            button.setButtonType(.switch)
            return button
        case .text, .integer, .ramMB:
            let field = NSTextField(string: "")
            field.widthAnchor.constraint(greaterThanOrEqualToConstant: 280).isActive = true
            if row.kind != .text {
                let formatter = NumberFormatter()
                formatter.allowsFloats = false
                field.formatter = formatter
            }
            return field
        }
    }

    private func load() {
        let values = live ? nil : prefsPath.map { PrefsFile.load($0) }
        for page in pages {
            for row in page.rows {
                guard let control = controls[row.key] else { continue }
                switch row.kind {
                case .text:
                    (control as? NSTextField)?.stringValue = readString(row.key, row.fallback, values)
                case .integer:
                    (control as? NSTextField)?.stringValue = "\(readInt(row.key, Int(row.fallback) ?? 0, values))"
                case .ramMB:
                    let bytes = readInt("ramsize", 536870912, values)
                    (control as? NSTextField)?.stringValue = "\(max(bytes / (1024 * 1024), 1))"
                case .toggle:
                    (control as? NSButton)?.state = readBool(row.key, row.fallback == "true", values) ? .on : .off
                case .mouse:
                    (control as? NSButton)?.state = readString("mouse", "absolute", values) == "relative" ? .on : .off
                }
            }
        }
    }

    private func readString(_ key: String, _ fallback: String, _ values: [String: String]?) -> String {
        if live { return PrefsBridge.string(key).isEmpty ? fallback : PrefsBridge.string(key) }
        return PrefsFile.string(values ?? [:], key, fallback)
    }

    private func readInt(_ key: String, _ fallback: Int, _ values: [String: String]?) -> Int {
        if live { return PrefsBridge.int(key) }
        return PrefsFile.int(values ?? [:], key, fallback)
    }

    private func readBool(_ key: String, _ fallback: Bool, _ values: [String: String]?) -> Bool {
        if live { return PrefsBridge.bool(key) }
        return PrefsFile.bool(values ?? [:], key, fallback)
    }

    @objc private func cancelSheet() {
        saved = false
        onClose(false)
    }

    @objc private func saveSheet() {
        var document: [String: String] = [:]
        for page in pages {
            for row in page.rows {
                guard let control = controls[row.key] else { continue }
                switch row.kind {
                case .text:
                    let value = (control as? NSTextField)?.stringValue ?? ""
                    document[row.key] = value
                    if live { PrefsBridge.setString(row.key, value) }
                case .integer:
                    let value = Int((control as? NSTextField)?.stringValue ?? "") ?? 0
                    document[row.key] = "\(value)"
                    if live { PrefsBridge.setInt(row.key, value) }
                case .ramMB:
                    let mb = Int((control as? NSTextField)?.stringValue ?? "") ?? 1
                    let bytes = max(mb, 1) * 1024 * 1024
                    document["ramsize"] = "\(bytes)"
                    if live { PrefsBridge.setInt("ramsize", bytes) }
                case .toggle:
                    let on = (control as? NSButton)?.state == .on
                    document[row.key] = on ? "true" : "false"
                    if live { PrefsBridge.setBool(row.key, on) }
                case .mouse:
                    let relative = (control as? NSButton)?.state == .on
                    document["mouse"] = relative ? "relative" : "absolute"
                    if live { PrefsBridge.setString("mouse", relative ? "relative" : "absolute") }
                }
            }
        }
        if live { PrefsBridge.save() }
        if let prefsPath { PrefsFile.save(prefsPath, document) }
        saved = true
        onClose(true)
    }

    private static func makePages() -> [(title: String, rows: [PrefRow])] {
        [
            ("Drives", [
                PrefRow("disk", "disk", .text, ""),
                PrefRow("cdrom", "cdrom", .text, ""),
                PrefRow("extfs", "extfs", .text, ""),
                PrefRow("bootdrive", "bootdrive", .integer, "0"),
                PrefRow("bootdriver", "bootdriver", .integer, "0"),
                PrefRow("nocdrom", "nocdrom", .toggle, "false")
            ]),
            ("Display", [
                PrefRow("screen", "screen", .text, "win/1024/768"),
                PrefRow("windowmodes", "windowmodes", .integer, "0"),
                PrefRow("screenmodes", "screenmodes", .integer, "0"),
                PrefRow("frameskip", "frameskip", .integer, "1"),
                PrefRow("gfxaccel", "gfxaccel", .toggle, "true"),
                PrefRow("sheepforce", "sheepforce", .toggle, "true"),
                PrefRow("qtcodec", "qtcodec", .toggle, "true"),
                PrefRow("hardcursor", "hardcursor", .toggle, "false"),
                PrefRow("scale_nearest", "scale_nearest", .toggle, "false"),
                PrefRow("scale_integer", "scale_integer", .toggle, "false"),
                PrefRow("init_grab", "init_grab", .toggle, "false")
            ]),
            ("Sound", [
                PrefRow("nosound", "nosound", .toggle, "false"),
                PrefRow("bootchime", "bootchime", .toggle, "true"),
                PrefRow("sound_buffer", "sound_buffer", .integer, "0"),
                PrefRow("dsp", "dsp", .text, "/dev/dsp"),
                PrefRow("mixer", "mixer", .text, "/dev/mixer")
            ]),
            ("System", [
                PrefRow("ramsize", "ramsize (MB)", .ramMB, "512"),
                PrefRow("rom", "rom", .text, ""),
                PrefRow("jit", "jit", .toggle, "true"),
                PrefRow("jit68k", "jit68k", .toggle, "false"),
                PrefRow("ignoresegv", "ignoresegv", .toggle, "true"),
                PrefRow("ignoreillegal", "ignoreillegal", .toggle, "true"),
                PrefRow("cpuclock", "cpuclock", .integer, "0"),
                PrefRow("yearofs", "yearofs", .integer, "0"),
                PrefRow("dayofs", "dayofs", .integer, "0"),
                PrefRow("nogui", "nogui", .toggle, "false"),
                PrefRow("noclipconversion", "noclipconversion", .toggle, "false"),
                PrefRow("nonet", "nonet", .toggle, "false"),
                PrefRow("ether", "ether", .text, ""),
                PrefRow("idlewait", "idlewait", .toggle, "true"),
                PrefRow("name_encoding", "name_encoding", .integer, "0")
            ]),
            ("Input", [
                PrefRow("seriala", "seriala", .text, "/dev/null"),
                PrefRow("serialb", "serialb", .text, "/dev/null"),
                PrefRow("keyboardtype", "keyboardtype", .integer, "5"),
                PrefRow("hotkey", "hotkey", .integer, "0"),
                PrefRow("swap_opt_cmd", "swap_opt_cmd", .toggle, "false"),
                PrefRow("keycodes", "keycodes", .toggle, "false"),
                PrefRow("keycodefile", "keycodefile", .text, ""),
                PrefRow("mousewheelmode", "mousewheelmode", .integer, "1"),
                PrefRow("mousewheellines", "mousewheellines", .integer, "3"),
                PrefRow("mouse", "optimize mouse for games", .mouse, "absolute")
            ])
        ]
    }
}

private struct PrefRow {
    enum Kind { case text, integer, toggle, ramMB, mouse }
    let key: String
    let label: String
    let kind: Kind
    let fallback: String
    init(_ key: String, _ label: String, _ kind: Kind, _ fallback: String) {
        self.key = key
        self.label = label
        self.kind = kind
        self.fallback = fallback
    }
}
