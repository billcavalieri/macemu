/*
 *  NewVMSheet.swift - The New Virtual Machine sheet: a name, the startup disk and, optionally, a ROM file.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class NewVMSheet: NSWindowController, NSTextFieldDelegate {
    /// Called with the entered values, or with nils when the sheet was cancelled.
    private let onDone: (String?, String?, String?) -> Void
    private let nameField = NSTextField(string: "Mac OS 9")
    private let diskField = NSTextField(string: "")
    private let romField = NSTextField(string: "")
    private let create = NSButton(title: "Create", target: nil, action: nil)

    init(onDone: @escaping (String?, String?, String?) -> Void) {
        self.onDone = onDone
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 10),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)

        let heading = NSTextField(labelWithString: "New Virtual Machine")
        heading.font = .preferredFont(forTextStyle: .headline)
        let intro = NSTextField(wrappingLabelWithString: "Choose the disk image Mac OS starts from. Every setting can be changed afterwards.")
        intro.textColor = .secondaryLabelColor
        intro.preferredMaxLayoutWidth = 520

        nameField.placeholderString = "Name"
        diskField.placeholderString = "Disk image (.hfv, .img, .qcow2 …)"
        romField.placeholderString = "Optional"
        for field in [nameField, diskField, romField] {
            field.delegate = self
            field.lineBreakMode = .byTruncatingMiddle
            field.setContentHuggingPriority(.defaultLow, for: .horizontal)
        }
        nameField.setAccessibilityLabel("Name")
        diskField.setAccessibilityLabel("Startup disk image")
        romField.setAccessibilityLabel("ROM file")
        let chooseDisk = NSButton(title: "Choose…", target: self, action: #selector(chooseDisk))
        let chooseROM = NSButton(title: "Choose…", target: self, action: #selector(chooseROM))
        let romNote = NSTextField(wrappingLabelWithString: "Leave the ROM empty to use the “Mac OS ROM” file in the startup disk's System Folder.")
        romNote.textColor = .secondaryLabelColor
        romNote.font = .preferredFont(forTextStyle: .caption1)
        romNote.preferredMaxLayoutWidth = 360

        let grid = NSGridView(views: [
            [label("Name:"), nameField, NSGridCell.emptyContentView],
            [label("Startup disk:"), diskField, chooseDisk],
            [label("ROM file:"), romField, chooseROM],
            [NSGridCell.emptyContentView, romNote, NSGridCell.emptyContentView],
        ])
        grid.rowSpacing = 8
        grid.columnSpacing = 8
        grid.column(at: 0).xPlacement = .trailing

        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.keyEquivalent = "\u{1b}"
        create.target = self
        create.action = #selector(createVM)
        create.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [spacer, cancel, create])
        buttons.spacing = 12

        let stack = NSStackView(views: [heading, intro, grid, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14
        stack.setCustomSpacing(18, after: intro)
        stack.setCustomSpacing(22, after: grid)
        stack.translatesAutoresizingMaskIntoConstraints = false
        buttons.translatesAutoresizingMaskIntoConstraints = false
        let root = NSView()
        root.addSubview(stack)
        NSLayoutConstraint.activate([
            root.widthAnchor.constraint(equalToConstant: 580),
            stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 22),
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -24),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -20),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor),
            grid.widthAnchor.constraint(equalTo: stack.widthAnchor),
        ])
        window.contentView = root
        window.initialFirstResponder = nameField
        validate()
        window.layoutIfNeeded()
        window.setContentSize(root.fittingSize)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    private func label(_ text: String) -> NSTextField {
        let l = NSTextField(labelWithString: text)
        l.alignment = .right
        return l
    }

    private var diskExists: Bool {
        var isDir: ObjCBool = false
        let path = (diskField.stringValue as NSString).expandingTildeInPath
        return !path.isEmpty && FileManager.default.fileExists(atPath: path, isDirectory: &isDir) && !isDir.boolValue
    }

    /// Create is available once there is a name and a startup disk that exists.
    private func validate() {
        create.isEnabled = !nameField.stringValue.trimmingCharacters(in: .whitespaces).isEmpty && diskExists
    }

    func controlTextDidChange(_ obj: Notification) {
        validate()
    }

    private func choose(into field: NSTextField, title: String) {
        guard let window else { return }
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.beginSheetModal(for: window) { [weak self] response in
            guard response == .OK, let url = panel.url else { return }
            MainActor.assumeIsolated {
                field.stringValue = url.path
                if field === self?.diskField, let name = self?.nameField, name.stringValue == "Mac OS 9" || name.stringValue.isEmpty {
                    name.stringValue = url.deletingPathExtension().lastPathComponent
                }
                self?.validate()
            }
        }
    }

    @objc private func chooseDisk() { choose(into: diskField, title: "Choose the startup disk image") }
    @objc private func chooseROM() { choose(into: romField, title: "Choose the ROM file") }

    @objc private func cancel() {
        onDone(nil, nil, nil)
    }

    @objc private func createVM() {
        guard create.isEnabled else { return }
        onDone(nameField.stringValue.trimmingCharacters(in: .whitespaces),
               (diskField.stringValue as NSString).expandingTildeInPath,
               (romField.stringValue as NSString).expandingTildeInPath)
    }
}
