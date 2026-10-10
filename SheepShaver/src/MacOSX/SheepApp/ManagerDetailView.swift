/*
 *  ManagerDetailView.swift - What the library window shows next to the VM list: the selected virtual machine (its
 *  state, what it is made of and the buttons to start, shut down and configure it), or, while the library is empty,
 *  an explanation with the two ways to add one.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
struct DetailActions {
    var start: (VirtualMachineDocument) -> Void = { _ in }
    var shutDown: (VirtualMachineDocument) -> Void = { _ in }
    var openSettings: (VirtualMachineDocument) -> Void = { _ in }
    /// Moves a VM that has its own window back into the library window.
    var attach: (VirtualMachineDocument) -> Void = { _ in }
    /// Whether the VM is shown in its own window (it runs embedded but was detached).
    var isDetached: (VirtualMachineDocument) -> Bool = { _ in false }
    var newMachine: () -> Void = {}
    var addExisting: () -> Void = {}
}

@MainActor
final class VMDetailViewController: NSViewController {
    private let store: VirtualMachineStore
    var actions = DetailActions()

    private let emptyStack = NSStackView()
    private let detailStack = NSStackView()
    private let icon = NSImageView()
    private let name = NSTextField(labelWithString: "")
    private let statusDot = NSImageView()
    private let statusText = NSTextField(labelWithString: "")
    private let form = NSGridView()
    private let primary = NSButton(title: "Start", target: nil, action: nil)
    private let shutDown = NSButton(title: "Shut Down", target: nil, action: nil)
    private let settings = NSButton(title: "Settings…", target: nil, action: nil)
    private let attachButton = NSButton(title: "Attach to Library", target: nil, action: nil)
    private var observer: NSObjectProtocol?

    init(store: VirtualMachineStore) {
        self.store = store
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    private var current: VirtualMachineDocument? { store.document(id: store.selection) }

    override func loadView() {
        let root = NSView()
        view = root

        // Empty library
        let emptyImage = NSImageView(image: NSImage(systemSymbolName: "desktopcomputer", accessibilityDescription: nil) ?? NSImage())
        emptyImage.symbolConfiguration = .init(pointSize: 56, weight: .light)
        emptyImage.contentTintColor = .tertiaryLabelColor
        let emptyTitle = NSTextField(labelWithString: "No Virtual Machines")
        emptyTitle.font = .preferredFont(forTextStyle: .title2)
        let emptyText = NSTextField(wrappingLabelWithString: "Create a new virtual machine, or add one you already have: a SheepShaver prefs file, or the folder it is in. You can also drop one on the list.")
        emptyText.textColor = .secondaryLabelColor
        emptyText.alignment = .center
        emptyText.preferredMaxLayoutWidth = 340
        let newButton = NSButton(title: "New Virtual Machine…", target: self, action: #selector(newMachine(_:)))
        newButton.bezelStyle = .push
        newButton.keyEquivalent = "\r"
        let addButton = NSButton(title: "Add Existing…", target: self, action: #selector(addExisting(_:)))
        addButton.bezelStyle = .push
        let emptyButtons = NSStackView(views: [addButton, newButton])
        emptyButtons.spacing = 12
        emptyStack.setViews([emptyImage, emptyTitle, emptyText, emptyButtons], in: .top)
        emptyStack.orientation = .vertical
        emptyStack.alignment = .centerX
        emptyStack.spacing = 12
        emptyStack.setCustomSpacing(20, after: emptyText)

        // One virtual machine
        icon.imageScaling = .scaleProportionallyUpOrDown
        icon.setAccessibilityElement(false)
        if let url = Bundle.main.url(forResource: "ClassicMacOS", withExtension: "svg") {
            icon.image = NSImage(contentsOf: url)
        }
        name.font = .preferredFont(forTextStyle: .largeTitle)
        name.lineBreakMode = .byTruncatingTail
        name.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        statusDot.symbolConfiguration = .init(pointSize: 8, weight: .regular)
        statusDot.setAccessibilityElement(false)
        statusText.textColor = .secondaryLabelColor
        statusText.font = .preferredFont(forTextStyle: .body)
        let status = NSStackView(views: [statusDot, statusText])
        status.spacing = 5
        let heading = NSStackView(views: [name, status])
        heading.orientation = .vertical
        heading.alignment = .leading
        heading.spacing = 2
        let header = NSStackView(views: [icon, heading])
        header.orientation = .horizontal
        header.alignment = .centerY
        header.spacing = 16

        primary.bezelStyle = .push
        primary.keyEquivalent = "\r"
        primary.target = self
        primary.action = #selector(startOrShow(_:))
        primary.controlSize = .large
        shutDown.bezelStyle = .push
        shutDown.target = self
        shutDown.action = #selector(shutDownClicked(_:))
        shutDown.controlSize = .large
        settings.bezelStyle = .push
        settings.target = self
        settings.action = #selector(settingsClicked(_:))
        settings.controlSize = .large
        attachButton.bezelStyle = .push
        attachButton.target = self
        attachButton.action = #selector(attachClicked(_:))
        attachButton.controlSize = .large
        let buttons = NSStackView(views: [primary, attachButton, shutDown, settings])
        buttons.spacing = 10

        form.rowSpacing = 8
        form.columnSpacing = 14
        form.xPlacement = .leading
        form.rowAlignment = .firstBaseline

        detailStack.setViews([header, buttons, form], in: .top)
        detailStack.orientation = .vertical
        detailStack.alignment = .leading
        detailStack.spacing = 22

        for stack in [emptyStack, detailStack] {
            stack.translatesAutoresizingMaskIntoConstraints = false
            root.addSubview(stack)
        }
        NSLayoutConstraint.activate([
            icon.widthAnchor.constraint(equalToConstant: 48),
            icon.heightAnchor.constraint(equalToConstant: 56),
            emptyStack.centerXAnchor.constraint(equalTo: root.centerXAnchor),
            emptyStack.centerYAnchor.constraint(equalTo: root.centerYAnchor),
            detailStack.leadingAnchor.constraint(equalTo: root.safeAreaLayoutGuide.leadingAnchor, constant: 40),
            detailStack.trailingAnchor.constraint(lessThanOrEqualTo: root.safeAreaLayoutGuide.trailingAnchor, constant: -40),
            detailStack.topAnchor.constraint(equalTo: root.safeAreaLayoutGuide.topAnchor, constant: 36),
        ])
        observer = NotificationCenter.default.addObserver(forName: .vmRunStateChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        refresh()
    }

    /// Call when the selection changes, the library changes, or a VM starts or stops.
    func refresh() {
        guard isViewLoaded else { return }
        guard let doc = current else {
            emptyStack.isHidden = false
            detailStack.isHidden = true
            return
        }
        emptyStack.isHidden = true
        detailStack.isHidden = false
        let running = store.isRunning(doc.id)
        name.stringValue = doc.name
        statusDot.image = NSImage(systemSymbolName: "circle.fill", accessibilityDescription: nil)
        statusDot.contentTintColor = running ? .systemGreen : .tertiaryLabelColor
        let detached = running && actions.isDetached(doc)
        statusText.stringValue = detached ? "Running in its own window" : (running ? "Running" : "Stopped")
        attachButton.isHidden = !detached
        primary.title = running ? "Show Window" : "Start"
        shutDown.isHidden = !running
        fillForm(for: doc)
    }

    private func fillForm(for doc: VirtualMachineDocument) {
        form.subviews.forEach { $0.removeFromSuperview() }      // removing a row does not remove its views
        while form.numberOfRows > 0 { form.removeRow(at: 0) }
        let text = (try? String(contentsOfFile: doc.prefsPath, encoding: .utf8)) ?? ""
        let summary = VMSummary(prefs: PrefsDocument(text: text))
        func add(_ label: String, _ values: [String]) {
            guard !values.isEmpty else { return }
            let title = NSTextField(labelWithString: label)
            title.textColor = .secondaryLabelColor
            title.alignment = .right
            let value = NSTextField(wrappingLabelWithString: values.joined(separator: "\n"))
            value.isSelectable = true
            value.preferredMaxLayoutWidth = 360
            form.addRow(with: [title, value])
        }
        add("Memory", [summary.memory])
        add("Display", [summary.display])
        add(summary.disks.count > 1 ? "Disks" : "Disk", summary.disks)
        add("CD-ROM", summary.cdImages)
        add("ROM", [summary.rom])
        add("Shared folder", summary.sharedFolder.map { [$0] } ?? [])
        form.column(at: 0).xPlacement = .trailing
    }

    // MARK: actions

    @objc private func startOrShow(_ sender: Any?) { if let doc = current { actions.start(doc) } }
    @objc private func shutDownClicked(_ sender: Any?) { if let doc = current { actions.shutDown(doc) } }
    @objc private func settingsClicked(_ sender: Any?) { if let doc = current { actions.openSettings(doc) } }
    @objc private func attachClicked(_ sender: Any?) { if let doc = current { actions.attach(doc) } }
    @objc private func newMachine(_ sender: Any?) { actions.newMachine() }
    @objc private func addExisting(_ sender: Any?) { actions.addExisting() }
}
