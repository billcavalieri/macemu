/*
 *  ManagerDetailView.swift - What the library window shows next to the VM list while it is the manager (the
 *  process that starts VMs but does not run one): the selected VM, whether it runs, and a Start/Show button.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class ManagerDetailView: NSView {
    private let store: VirtualMachineStore
    private let onPlay: (VirtualMachineDocument) -> Void
    private let name = NSTextField(labelWithString: "")
    private let state = NSTextField(labelWithString: "")
    private let hint = NSTextField(wrappingLabelWithString: "Each virtual machine runs in its own window. Several can run at the same time.")
    private let button = NSButton(title: "Start", target: nil, action: nil)

    init(store: VirtualMachineStore, onPlay: @escaping (VirtualMachineDocument) -> Void) {
        self.store = store
        self.onPlay = onPlay
        super.init(frame: .zero)
        name.font = .systemFont(ofSize: 22, weight: .semibold)
        state.textColor = .secondaryLabelColor
        hint.textColor = .tertiaryLabelColor
        hint.alignment = .center
        button.bezelStyle = .rounded
        button.controlSize = .large
        button.keyEquivalent = "\r"
        button.target = self
        button.action = #selector(play)
        let stack = NSStackView(views: [name, state, button, hint])
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
            hint.widthAnchor.constraint(lessThanOrEqualToConstant: 320)
        ])
        NotificationCenter.default.addObserver(forName: .vmRunStateChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        refresh()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    override var isOpaque: Bool { true }
    override func draw(_ dirtyRect: NSRect) {
        NSColor.windowBackgroundColor.setFill()
        dirtyRect.fill()
    }

    private var current: VirtualMachineDocument? {
        store.document(id: store.selection)
    }

    /// Call when the selection changes or a VM starts or stops.
    func refresh() {
        guard let doc = current else {
            name.stringValue = "No virtual machine"
            state.stringValue = "Use the + button to create one, or choose Add Existing…"
            button.isHidden = true
            return
        }
        let running = store.isRunning(doc.id)
        name.stringValue = doc.name
        state.stringValue = running ? "Running" : "Stopped"
        button.title = running ? "Show Window" : "Start"
        button.isHidden = false
    }

    @objc private func play() {
        if let doc = current { onPlay(doc) }
    }
}
