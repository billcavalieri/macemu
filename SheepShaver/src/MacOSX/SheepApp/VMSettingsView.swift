/*
 *  VMSettingsView.swift - The settings sheet of one virtual machine. Pages in a source list, each a labelled form;
 *  it reads and writes the VM's prefs file through PrefsDocument, so repeated lines (several disks), comments and
 *  keys it has no control for are kept exactly as they were. A key is only written when it differs from its
 *  default or is already in the file, so a prefs file does not fill up with defaults.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

// MARK: - one setting

/// A setting's controls plus how it reads from and writes to a PrefsDocument.
@MainActor
private final class Setting {
    enum Kind { case string, int, bool }
    /// The prefs keys it writes with their kind, for pushing a change into a running VM.
    var keys: [(String, Kind)] = []
    /// Left column (the label) and right column (the control), plus an optional note under the control.
    var label: String?
    var control: NSView
    var help: String?
    var load: (PrefsDocument) -> Void = { _ in }
    var store: (inout PrefsDocument) -> Void = { _ in }
    /// What the control shows, as text. Saved only when it differs from what it showed after loading, so a setting
    /// nobody touched never rewrites its line (an empty `rom` line, a `yes` for `true`, … stay as the file had them).
    var state: () -> String = { "" }
    var loadedState = ""

    init(control: NSView) { self.control = control }
}

private struct Section {
    var title: String?
    var settings: [Setting]
}

private struct Page {
    var title: String
    var symbol: String
    var sections: [Section]
}

// MARK: - the sheet

@MainActor
final class SettingsSheet: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let prefsPath: String?
    private let live: Bool
    private let onClose: (Bool) -> Void
    private var pages: [Page] = []
    private let pageTable = NSTableView()
    private let pageTitle = NSTextField(labelWithString: "")
    private let content = FlippedView()
    private let scroll = NSScrollView()
    private var pageViews: [NSView] = []
    private var original = PrefsDocument()
    /// Keeps each Choose… button's helper alive (a button's target is not retained).
    private var handlers: [PanelHandler] = []

    /// - Parameters:
    ///   - prefsPath: the VM's prefs file.
    ///   - running: the VM is running now, so most changes apply the next time it starts (the sheet says so).
    ///   - live: this process *is* that running VM; changed values are also handed to the emulator.
    init(prefsPath: String?, running: Bool, live: Bool, onClose: @escaping (Bool) -> Void) {
        self.prefsPath = prefsPath
        self.live = live
        self.onClose = onClose
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 470), styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        pages = makePages()
        build(window, running: running)
        load()
        var first = 0
        if nwDiagnosticsOn, let page = ProcessInfo.processInfo.environment["NW_SETTINGS_PAGE"], let n = Int(page), n >= 0, n < pages.count { first = n }
        pageTable.selectRowIndexes(IndexSet(integer: first), byExtendingSelection: false)
        showPage(first)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    // MARK: layout

    private func build(_ window: NSWindow, running: Bool) {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("page"))
        pageTable.addTableColumn(column)
        pageTable.headerView = nil
        pageTable.style = .sourceList
        pageTable.dataSource = self
        pageTable.delegate = self
        pageTable.setAccessibilityLabel("Settings pages")
        let pageScroll = NSScrollView()
        pageScroll.documentView = pageTable
        pageScroll.hasVerticalScroller = false
        pageScroll.drawsBackground = false
        pageScroll.translatesAutoresizingMaskIntoConstraints = false

        pageTitle.font = .preferredFont(forTextStyle: .title2)
        pageTitle.translatesAutoresizingMaskIntoConstraints = false
        content.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = content
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelSheet))
        cancel.keyEquivalent = "\u{1b}"
        let save = NSButton(title: "Save", target: self, action: #selector(saveSheet))
        save.keyEquivalent = "\r"
        let note = NSTextField(wrappingLabelWithString: running ? "This virtual machine is running. Most changes take effect the next time it starts." : "")
        note.textColor = .secondaryLabelColor
        note.font = .preferredFont(forTextStyle: .caption1)
        note.translatesAutoresizingMaskIntoConstraints = false
        let buttons = NSStackView(views: [cancel, save])
        buttons.spacing = 12
        buttons.translatesAutoresizingMaskIntoConstraints = false

        let separator = NSBox()
        separator.boxType = .separator
        separator.translatesAutoresizingMaskIntoConstraints = false
        let footerSeparator = NSBox()
        footerSeparator.boxType = .separator
        footerSeparator.translatesAutoresizingMaskIntoConstraints = false

        let root = NSView()
        for v in [pageScroll, separator, pageTitle, scroll, footerSeparator, note, buttons] { root.addSubview(v) }
        window.contentView = root
        NSLayoutConstraint.activate([
            pageScroll.topAnchor.constraint(equalTo: root.topAnchor),
            pageScroll.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            pageScroll.widthAnchor.constraint(equalToConstant: 180),
            pageScroll.bottomAnchor.constraint(equalTo: footerSeparator.topAnchor),
            separator.leadingAnchor.constraint(equalTo: pageScroll.trailingAnchor),
            separator.topAnchor.constraint(equalTo: root.topAnchor),
            separator.bottomAnchor.constraint(equalTo: footerSeparator.topAnchor),
            separator.widthAnchor.constraint(equalToConstant: 1),
            pageTitle.topAnchor.constraint(equalTo: root.topAnchor, constant: 20),
            pageTitle.leadingAnchor.constraint(equalTo: separator.trailingAnchor, constant: 24),
            scroll.topAnchor.constraint(equalTo: pageTitle.bottomAnchor, constant: 10),
            scroll.leadingAnchor.constraint(equalTo: separator.trailingAnchor),
            scroll.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: footerSeparator.topAnchor),
            content.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
            footerSeparator.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            footerSeparator.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            footerSeparator.bottomAnchor.constraint(equalTo: buttons.topAnchor, constant: -12),
            note.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 20),
            note.centerYAnchor.constraint(equalTo: buttons.centerYAnchor),
            note.trailingAnchor.constraint(lessThanOrEqualTo: buttons.leadingAnchor, constant: -12),
            buttons.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -20),
            buttons.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -14),
        ])
        pageViews = pages.map { page in pageView(page) }
        window.initialFirstResponder = pageTable
    }

    private func pageView(_ page: Page) -> NSView {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 22
        stack.translatesAutoresizingMaskIntoConstraints = false
        // One label column width for the whole page, so the controls of every section line up
        let labelWidth = page.sections.flatMap(\.settings).compactMap(\.label).map {
            NSTextField(labelWithString: $0).fittingSize.width
        }.max() ?? 0
        for section in page.sections {
            let grid = NSGridView()
            grid.rowSpacing = 10
            grid.columnSpacing = 10
            for setting in section.settings {
                let control: NSView
                if let help = setting.help {
                    let note = NSTextField(wrappingLabelWithString: help)
                    note.font = .preferredFont(forTextStyle: .caption1)
                    note.textColor = .secondaryLabelColor
                    note.preferredMaxLayoutWidth = 380
                    let column = NSStackView(views: [setting.control, note])
                    column.orientation = .vertical
                    column.alignment = .leading
                    column.spacing = 4
                    control = column
                } else {
                    control = setting.control
                }
                let label: NSView = setting.label.map { text in
                    let l = NSTextField(labelWithString: text)
                    l.alignment = .right
                    return l
                } ?? NSGridCell.emptyContentView
                grid.addRow(with: [label, control])
            }
            grid.column(at: 0).xPlacement = .trailing
            grid.column(at: 0).width = labelWidth
            grid.column(at: 1).xPlacement = .leading
            grid.rowAlignment = .firstBaseline
            if let title = section.title {
                let heading = NSTextField(labelWithString: title)
                heading.font = .preferredFont(forTextStyle: .headline)
                let group = NSStackView(views: [heading, grid])
                group.orientation = .vertical
                group.alignment = .leading
                group.spacing = 10
                stack.addArrangedSubview(group)
            } else {
                stack.addArrangedSubview(grid)
            }
        }
        return stack
    }

    private func showPage(_ index: Int) {
        guard index >= 0, index < pages.count else { return }
        content.subviews.forEach { $0.removeFromSuperview() }
        let view = pageViews[index]
        content.addSubview(view)
        NSLayoutConstraint.activate([
            view.topAnchor.constraint(equalTo: content.topAnchor, constant: 8),
            view.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            view.trailingAnchor.constraint(lessThanOrEqualTo: content.trailingAnchor, constant: -24),
            view.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -20),
        ])
        pageTitle.stringValue = pages[index].title
        scroll.contentView.scroll(to: .zero)
    }

    // MARK: page list

    func numberOfRows(in tableView: NSTableView) -> Int { pages.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let cell = NSTableCellView()
        let image = NSImageView(image: NSImage(systemSymbolName: pages[row].symbol, accessibilityDescription: nil) ?? NSImage())
        image.translatesAutoresizingMaskIntoConstraints = false
        let label = NSTextField(labelWithString: pages[row].title)
        label.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(image)
        cell.addSubview(label)
        cell.imageView = image
        cell.textField = label
        NSLayoutConstraint.activate([
            image.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            image.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            image.widthAnchor.constraint(equalToConstant: 18),
            label.leadingAnchor.constraint(equalTo: image.trailingAnchor, constant: 8),
            label.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            label.trailingAnchor.constraint(lessThanOrEqualTo: cell.trailingAnchor, constant: -4),
        ])
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = pageTable.selectedRow
        if row >= 0 { showPage(row) }
    }

    // MARK: reading and writing

    private func load() {
        let text = prefsPath.flatMap { try? String(contentsOfFile: $0, encoding: .utf8) } ?? ""
        original = PrefsDocument(text: text)
        for page in pages { for section in page.sections { for setting in section.settings { setting.load(original); setting.loadedState = setting.state() } } }
    }

    /// Test hook (tools/vms/test.sh settings): saves as the Save button would, without any change by hand.
    func saveForTest() { saveSheet() }

    @objc private func cancelSheet() {
        onClose(false)
    }

    @objc private func saveSheet() {
        window?.makeFirstResponder(nil)         // commits a field that is still being edited
        var doc = original
        var settings: [Setting] = []
        for page in pages { for section in page.sections { settings.append(contentsOf: section.settings) } }
        for setting in settings where setting.state() != setting.loadedState { setting.store(&doc) }
        if let prefsPath, doc != original {
            do {
                try doc.text.write(toFile: prefsPath, atomically: true, encoding: .utf8)
            } catch {
                guard let window else { return }
                let alert = NSAlert(error: error)
                alert.messageText = "The settings could not be saved"
                alert.beginSheetModal(for: window)
                return
            }
        }
        if live {
            // Hand what changed to the running emulator too
            for setting in settings {
                for (key, kind) in setting.keys where doc.values(key) != original.values(key) {
                    switch kind {
                    case .string: PrefsBridge.setString(key, doc.string(key))
                    case .int: PrefsBridge.setInt(key, doc.int(key))
                    case .bool: PrefsBridge.setBool(key, doc.bool(key))
                    }
                }
            }
            PrefsBridge.save()
        }
        onClose(true)
    }
}

/// A view whose origin is at the top left, so a scroll view starts at the top of its content.
final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

// MARK: - the pages

extension SettingsSheet {
    private func makePages() -> [Page] {
        [
            Page(title: "General", symbol: "gearshape", sections: [
                Section(title: nil, settings: [memory(), romFile()]),
            ]),
            Page(title: "Storage", symbol: "internaldrive", sections: [
                Section(title: "Disks", settings: [fileList("disk", "The first disk is the one Mac OS starts from. Changes take effect the next time the virtual machine starts.", title: "Choose disk images")]),
                Section(title: "CD-ROM", settings: [fileList("cdrom", "Disc images (.iso, .toast, .cdr) that appear as CDs in the guest.", title: "Choose CD or DVD images"),
                                                     toggle("nocdrom", "Turn off the CD-ROM drive", false)]),
                Section(title: "Shared Folder", settings: [path("extfs", "Folder:", folder: true, help: "A folder on this Mac that appears in the guest as the “Unix” disk.")]),
            ]),
            Page(title: "Display", symbol: "display", sections: [
                Section(title: nil, settings: [resolution()]),
                Section(title: "Graphics", settings: [
                    toggle("sheepforce", "Metal accelerator (SheepForce)", true),
                    toggle("gfxaccel", "3D acceleration (RAVE)", true),
                    toggle("qtcodec", "QuickTime codec acceleration", true),
                ]),
                Section(title: "Scaling", settings: [
                    toggle("scale_nearest", "Sharp pixels when the picture is scaled", false),
                    toggle("scale_integer", "Scale in whole-number steps only", false),
                    toggle("hardcursor", "Use the host's hardware cursor", false),
                ]),
                Section(title: "Performance", settings: [
                    number("frameskip", "Frame skip:", 1, help: "Draw every nth frame. 1 draws every frame."),
                ]),
            ]),
            Page(title: "Sound", symbol: "speaker.wave.2", sections: [
                Section(title: nil, settings: [toggle("nosound", "Play sound", true, inverted: true), toggle("bootchime", "Play the startup chime", true)]),
                Section(title: "Sound Input", settings: [
                    toggle("mic", "Let the virtual machine record from the microphone", false),
                ]),
            ]),
            Page(title: "Input", symbol: "computermouse", sections: [
                Section(title: "Mouse", settings: [
                    mouseForGames(),
                    toggle("init_grab", "Capture the mouse when the virtual machine starts", false),
                    popup("mousewheelmode", "Scroll wheel:", [("0", "Page Up and Page Down"), ("1", "Cursor keys")], "1"),
                    number("mousewheellines", "Lines per scroll step:", 3),
                ]),
                Section(title: "Keyboard", settings: [
                    toggle("swap_opt_cmd", "Swap the Option and Command keys", false),
                    number("keyboardtype", "Keyboard type:", 5),
                    number("hotkey", "Release hot key:", 0),
                ]),
            ]),
            Page(title: "Network", symbol: "network", sections: [
                Section(title: nil, settings: [
                    toggle("nonet", "Enable networking", true, inverted: true),
                    text("ether", "Network interface:", "", placeholder: "Default", help: "Leave empty for the default connection."),
                ]),
            ]),
            Page(title: "Advanced", symbol: "slider.horizontal.3", sections: [
                Section(title: "Processor", settings: [
                    toggle("jit", "Use the JIT compiler (faster)", true),
                    toggle("jit68k", "JIT for 68k code (experimental)", false),
                    toggle("jit68k_host", "Host 68k JIT", true),
                    toggle("idlewait", "Use less processor time while the guest is idle", true),
                    number("cpuclock", "CPU clock:", 0, help: "0 is automatic."),
                ]),
                Section(title: "Compatibility", settings: [
                    toggle("ignoresegv", "Ignore illegal memory accesses", true),
                    toggle("ignoreillegal", "Ignore illegal instructions", true),
                    toggle("noclipconversion", "Do not convert text when copying between Mac OS and macOS", false),
                    toggle("nogui", "Skip the startup configuration window", false),
                    number("name_encoding", "File name encoding:", 0),
                    number("yearofs", "Year offset:", 0),
                    number("dayofs", "Day offset:", 0),
                ]),
                Section(title: "Startup", settings: [
                    number("bootdrive", "Startup drive:", 0, help: "0 is automatic."),
                    number("bootdriver", "Startup driver:", 0, help: "0 is automatic; -62 starts from the CD-ROM."),
                ]),
                Section(title: "Devices", settings: [
                    text("seriala", "Serial port A:", "/dev/null"),
                    text("serialb", "Serial port B:", "/dev/null"),
                    number("windowmodes", "Window video modes:", 0),
                    number("screenmodes", "Full screen video modes:", 0),
                    number("sound_buffer", "Sound buffer size:", 0, help: "0 is automatic."),
                    toggle("keycodes", "Use a key code table", false),
                    path("keycodefile", "Key code file:", folder: false),
                    text("dsp", "Sound device:", "/dev/dsp"),
                    text("mixer", "Mixer device:", "/dev/mixer"),
                ]),
            ]),
        ]
    }

    // MARK: setting builders

    private func toggle(_ key: String, _ title: String, _ def: Bool, inverted: Bool = false) -> Setting {
        let button = NSButton(checkboxWithTitle: title, target: nil, action: nil)
        let s = Setting(control: button)
        s.keys = [(key, .bool)]
        s.load = { p in
            let v = p.bool(key, def)
            button.state = (inverted ? !v : v) ? .on : .off
        }
        s.state = { "\(button.state.rawValue)" }
        s.store = { p in
            let on = button.state == .on
            let v = inverted ? !on : on
            if p.has(key) || v != def { p.set(key, v) }
        }
        return s
    }

    private func number(_ key: String, _ label: String, _ def: Int, help: String? = nil) -> Setting {
        let field = NSTextField(string: "")
        let formatter = NumberFormatter()
        formatter.allowsFloats = false
        formatter.minimum = -2_147_483_648
        formatter.maximum = 2_147_483_647
        field.formatter = formatter
        field.alignment = .right
        field.widthAnchor.constraint(equalToConstant: 90).isActive = true
        field.setAccessibilityLabel(label.replacingOccurrences(of: ":", with: ""))
        let s = Setting(control: field)
        s.label = label
        s.help = help
        s.keys = [(key, .int)]
        s.load = { p in field.stringValue = "\(p.int(key, def))" }
        s.state = { field.stringValue }
        s.store = { p in
            let v = Int(field.stringValue) ?? def
            if p.has(key) || v != def { p.set(key, v) }
        }
        return s
    }

    private func text(_ key: String, _ label: String, _ def: String, placeholder: String? = nil, help: String? = nil) -> Setting {
        let field = NSTextField(string: "")
        field.placeholderString = placeholder
        field.widthAnchor.constraint(equalToConstant: 300).isActive = true
        field.setAccessibilityLabel(label.replacingOccurrences(of: ":", with: ""))
        let s = Setting(control: field)
        s.label = label
        s.help = help
        s.keys = [(key, .string)]
        s.load = { p in field.stringValue = p.string(key, def) }
        s.state = { field.stringValue }
        s.store = { p in
            let v = field.stringValue
            if p.has(key) || v != def { p.set(key, v) }
        }
        return s
    }

    private func popup(_ key: String, _ label: String, _ options: [(String, String)], _ def: String) -> Setting {
        let menu = NSPopUpButton(frame: .zero, pullsDown: false)
        for (value, title) in options {
            menu.addItem(withTitle: title)
            menu.lastItem?.representedObject = value
        }
        menu.setAccessibilityLabel(label.replacingOccurrences(of: ":", with: ""))
        let s = Setting(control: menu)
        s.label = label
        s.keys = [(key, .string)]
        s.state = { "\(menu.indexOfSelectedItem)" }
        s.load = { p in
            let v = p.string(key, def)
            if let index = menu.itemArray.firstIndex(where: { ($0.representedObject as? String) == v }) {
                menu.selectItem(at: index)
            } else {
                menu.addItem(withTitle: "Other (\(v))")      // a value written by hand stays selectable
                menu.lastItem?.representedObject = v
                menu.selectItem(at: menu.numberOfItems - 1)
            }
        }
        s.store = { p in
            let v = (menu.selectedItem?.representedObject as? String) ?? def
            if p.has(key) || v != def { p.set(key, v) }
        }
        return s
    }

    private func path(_ key: String, _ label: String, folder: Bool, help: String? = nil) -> Setting {
        let field = NSTextField(string: "")
        field.placeholderString = "None"
        field.lineBreakMode = .byTruncatingMiddle
        field.widthAnchor.constraint(equalToConstant: 250).isActive = true
        field.setAccessibilityLabel(label.replacingOccurrences(of: ":", with: ""))
        let choose = NSButton(title: "Choose…", target: nil, action: nil)
        let handler = PanelHandler(field: field, folder: folder, title: folder ? "Choose a folder" : "Choose a file", window: { [weak self] in self?.window })
        choose.target = handler
        choose.action = #selector(PanelHandler.choose(_:))
        handlers.append(handler)
        let row = NSStackView(views: [field, choose])
        row.spacing = 8
        let s = Setting(control: row)
        s.label = label
        s.help = help
        s.keys = [(key, .string)]
        s.load = { p in field.stringValue = p.string(key) }
        s.state = { field.stringValue }
        s.store = { p in
            let v = (field.stringValue as NSString).expandingTildeInPath
            if v.isEmpty { if p.has(key) { p.remove(key) } } else { p.set(key, v) }
        }
        return s
    }

    private func romFile() -> Setting {
        let s = path("rom", "ROM file:", folder: false, help: "Leave empty to use the “Mac OS ROM” file in the startup disk's System Folder.")
        return s
    }

    private func memory() -> Setting {
        let combo = NSComboBox()
        combo.addItems(withObjectValues: ["64", "128", "256", "384", "512", "768", "1024", "1536", "2048"])
        combo.completes = false
        let formatter = NumberFormatter()
        formatter.allowsFloats = false
        formatter.minimum = 8
        formatter.maximum = 4096
        combo.formatter = formatter
        combo.alignment = .right
        combo.widthAnchor.constraint(equalToConstant: 90).isActive = true
        combo.setAccessibilityLabel("Memory in megabytes")
        let unit = NSTextField(labelWithString: "MB")
        let row = NSStackView(views: [combo, unit])
        row.spacing = 6
        let s = Setting(control: row)
        s.label = "Memory:"
        s.keys = [("ramsize", .int)]
        s.state = { combo.stringValue }
        let def = 536_870_912
        s.load = { p in combo.stringValue = "\(max(p.int("ramsize", def) / (1024 * 1024), 1))" }
        s.store = { p in
            let bytes = max(Int(combo.stringValue) ?? 512, 8) * 1024 * 1024
            if p.has("ramsize") || bytes != def { p.set("ramsize", bytes) }
        }
        return s
    }

    private func resolution() -> Setting {
        let width = NSTextField(string: "")
        let height = NSTextField(string: "")
        for field in [width, height] {
            let formatter = NumberFormatter()
            formatter.allowsFloats = false
            formatter.minimum = 320
            formatter.maximum = 16_384
            field.formatter = formatter
            field.alignment = .right
            field.widthAnchor.constraint(equalToConstant: 70).isActive = true
        }
        width.setAccessibilityLabel("Width in pixels")
        height.setAccessibilityLabel("Height in pixels")
        let row = NSStackView(views: [width, NSTextField(labelWithString: "×"), height, NSTextField(labelWithString: "pixels")])
        row.spacing = 6
        let s = Setting(control: row)
        s.label = "Resolution:"
        s.help = "The size of the Mac OS screen, in a window. Resizing the window only scales the picture."
        s.keys = [("screen", .string)]
        s.state = { "\(width.stringValue)x\(height.stringValue)" }
        var prefix = "win"
        s.load = { p in
            let parts = p.string("screen", "win/1024/768").split(separator: "/")
            if parts.count == 3, let w = Int(parts[1]), let h = Int(parts[2]) {
                prefix = String(parts[0])
                width.stringValue = "\(w)"
                height.stringValue = "\(h)"
            } else {
                width.stringValue = "1024"
                height.stringValue = "768"
            }
        }
        s.store = { p in
            let w = min(max(Int(width.stringValue) ?? 1024, 320), 16_384)
            let h = min(max(Int(height.stringValue) ?? 768, 240), 16_384)
            let v = "\(prefix)/\(w)/\(h)"
            if p.has("screen") || v != "win/1024/768" { p.set("screen", v) }
        }
        return s
    }

    private func mouseForGames() -> Setting {
        let button = NSButton(checkboxWithTitle: "Optimize the mouse for games", target: nil, action: nil)
        let s = Setting(control: button)
        s.help = "Moves the pointer by relative amounts, as games expect. Click the picture to capture the mouse; Control-G releases it."
        s.keys = [("mouse", .string)]
        s.state = { "\(button.state.rawValue)" }
        s.load = { p in button.state = p.string("mouse", "absolute") == "relative" ? .on : .off }
        s.store = { p in
            let v = button.state == .on ? "relative" : "absolute"
            if p.has("mouse") || v != "absolute" { p.set("mouse", v) }
        }
        return s
    }

    private func fileList(_ key: String, _ help: String, title: String) -> Setting {
        let list = FileListView(title: title, window: { [weak self] in self?.window })
        let s = Setting(control: list)
        s.help = help
        s.keys = []
        s.state = { list.paths.joined(separator: "\n") }
        s.load = { p in list.paths = p.values(key) }
        s.store = { p in
            let values = list.paths.map { ($0 as NSString).expandingTildeInPath }
            if values != p.values(key) { p.setValues(key, values) }
        }
        return s
    }
}

// MARK: - helpers

/// Runs an open panel for a "Choose…" button and puts the answer in a text field.
@MainActor
private final class PanelHandler: NSObject {
    let field: NSTextField
    let folder: Bool
    let title: String
    let window: () -> NSWindow?

    init(field: NSTextField, folder: Bool, title: String, window: @escaping () -> NSWindow?) {
        self.field = field
        self.folder = folder
        self.title = title
        self.window = window
    }

    @objc func choose(_ sender: Any?) {
        guard let window = window() else { return }
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = !folder
        panel.canChooseDirectories = folder
        panel.allowsMultipleSelection = false
        panel.beginSheetModal(for: window) { [field] response in
            guard response == .OK, let url = panel.url else { return }
            MainActor.assumeIsolated { field.stringValue = url.path }
        }
    }
}

/// A list of files with Add, Remove and Move buttons (the disks and CD images of a VM; the order matters).
@MainActor
private final class FileListView: NSStackView, NSTableViewDataSource, NSTableViewDelegate {
    var paths: [String] = [] { didSet { table.reloadData() } }
    private let table = NSTableView()
    private let buttons = NSSegmentedControl()
    private let title: String
    private let windowProvider: () -> NSWindow?

    init(title: String, window: @escaping () -> NSWindow?) {
        self.title = title
        self.windowProvider = window
        super.init(frame: .zero)
        orientation = .vertical
        alignment = .leading
        spacing = 6
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("path"))
        table.addTableColumn(column)
        table.headerView = nil
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = true
        table.setAccessibilityLabel(title)
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        buttons.segmentStyle = .smallSquare
        buttons.trackingMode = .momentary
        buttons.segmentCount = 4
        let symbols = [("plus", "Add"), ("minus", "Remove"), ("chevron.up", "Move Up"), ("chevron.down", "Move Down")]
        for (i, (symbol, label)) in symbols.enumerated() {
            buttons.setImage(NSImage(systemSymbolName: symbol, accessibilityDescription: label), forSegment: i)
            buttons.setWidth(30, forSegment: i)
            buttons.setToolTip(label, forSegment: i)
        }
        buttons.target = self
        buttons.action = #selector(pressed(_:))
        addArrangedSubview(scroll)
        addArrangedSubview(buttons)
        NSLayoutConstraint.activate([
            scroll.widthAnchor.constraint(equalToConstant: 400),
            scroll.heightAnchor.constraint(equalToConstant: 96),
        ])
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func numberOfRows(in tableView: NSTableView) -> Int { paths.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let cell = NSTableCellView()
        let label = NSTextField(labelWithString: paths[row])
        label.lineBreakMode = .byTruncatingMiddle
        label.toolTip = paths[row]
        label.translatesAutoresizingMaskIntoConstraints = false
        cell.addSubview(label)
        cell.textField = label
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            label.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
            label.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return cell
    }

    @objc private func pressed(_ sender: NSSegmentedControl) {
        let row = table.selectedRow
        switch sender.selectedSegment {
        case 0: add()
        case 1:
            guard row >= 0 else { return }
            paths.remove(at: row)
            table.selectRowIndexes(IndexSet(integer: min(row, paths.count - 1)), byExtendingSelection: false)
        case 2:
            guard row > 0 else { return }
            paths.swapAt(row, row - 1)
            table.selectRowIndexes(IndexSet(integer: row - 1), byExtendingSelection: false)
        case 3:
            guard row >= 0, row < paths.count - 1 else { return }
            paths.swapAt(row, row + 1)
            table.selectRowIndexes(IndexSet(integer: row + 1), byExtendingSelection: false)
        default: break
        }
    }

    private func add() {
        guard let window = windowProvider() else { return }
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = true
        panel.beginSheetModal(for: window) { [weak self] response in
            guard response == .OK else { return }
            MainActor.assumeIsolated {
                guard let self else { return }
                self.paths.append(contentsOf: panel.urls.map(\.path).filter { !self.paths.contains($0) })
                self.table.selectRowIndexes(IndexSet(integer: self.paths.count - 1), byExtendingSelection: false)
            }
        }
    }
}
