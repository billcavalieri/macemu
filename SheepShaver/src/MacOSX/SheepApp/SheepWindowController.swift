/*
 *  SheepWindowController.swift - Chrome around the Metal guest surface.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import SwiftUI

@_silgen_name("VideoHostRequestQuit")
private func VideoHostRequestQuit()

@MainActor
final class SheepWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSSplitViewDelegate {
    let pane = GuestPaneView(frame: .zero)
    let store = VirtualMachineStore()
    var booted = false
    private var settingsWindow: NSWindow?
    private var newSheet: NewVMSheet?
    private let split = NSSplitView()
    private var sidebarHost: NSHostingView<VMLibraryView>!
    private var sidebarWidth: CGFloat = 220
    private var sidebarCollapsed = false

    var display: GuestDisplayView { pane.display }

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 80, y: 80, width: 1244, height: 768),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "SheepShaver"
        window.titleVisibility = .visible
        window.titlebarAppearsTransparent = false
        window.titlebarSeparatorStyle = .none
        window.isRestorable = false
        super.init(window: window)
        window.delegate = self
        window.styleMask.remove(.fullSizeContentView)

        let sidebarHost = NSHostingView(rootView: VMLibraryView(
            store: store,
            onPlay: { [weak self] doc in self?.play(doc) },
            onSelect: { [weak self] doc in self?.setSubtitle(doc.name) }
        ))
        sidebarHost.sizingOptions = []
        sidebarHost.frame.size.width = sidebarWidth
        self.sidebarHost = sidebarHost

        split.isVertical = true
        split.dividerStyle = .thin
        split.delegate = self
        split.translatesAutoresizingMaskIntoConstraints = false
        split.addArrangedSubview(sidebarHost)
        split.addArrangedSubview(pane)
        split.setHoldingPriority(NSLayoutConstraint.Priority(260), forSubviewAt: 0)
        split.setHoldingPriority(.defaultLow, forSubviewAt: 1)

        let root = NSView()
        window.contentView = root
        root.addSubview(split)
        let guide = window.contentLayoutGuide as! NSLayoutGuide
        NSLayoutConstraint.activate([
            split.topAnchor.constraint(equalTo: guide.topAnchor),
            split.leadingAnchor.constraint(equalTo: guide.leadingAnchor),
            split.trailingAnchor.constraint(equalTo: guide.trailingAnchor),
            split.bottomAnchor.constraint(equalTo: guide.bottomAnchor)
        ])

        let toolbar = NSToolbar(identifier: "SheepToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        window.toolbar = toolbar
        window.toolbarStyle = .expanded
        window.styleMask.remove(.fullSizeContentView)
        if let name = store.document(id: store.selection)?.name {
            window.subtitle = name
        }
        split.setPosition(sidebarWidth, ofDividerAt: 0)
        window.acceptsMouseMovedEvents = true
        sizeWindowToGuest()
    }

    func sizeWindowToGuest() {
        guard let window else { return }
        window.styleMask.remove(.fullSizeContentView)
        let gw = max(pane.guestSize.width, 640)
        let gh = max(pane.guestSize.height, 480)
        let sidebarW = sidebarCollapsed ? 0 : max(sidebarHost.frame.width > 1 ? sidebarHost.frame.width : sidebarWidth, 180)
        let target = NSSize(width: sidebarW + split.dividerThickness + gw, height: gh)
        window.contentMinSize = NSSize(width: 640, height: 480)
        window.setContentSize(target)
        window.layoutIfNeeded()
        split.adjustSubviews()
        if sidebarCollapsed {
            split.setPosition(0, ofDividerAt: 0)
        } else {
            split.setPosition(sidebarWidth, ofDividerAt: 0)
        }
        window.layoutIfNeeded()
        pane.layoutSubtreeIfNeeded()
        if grabLayoutLog < 8 {
            grabLayoutLog += 1
            let pf = pane.frame
            let df = pane.display.frame
            let sf = sidebarHost.frame
            print(String(format: "NW-BOOT mouse layout #%d split=%.0fx%.0f side=%.0f,%.0f %.0fx%.0f pane=%.0f,%.0f %.0fx%.0f display=%.0f,%.0f %.0fx%.0f guest=%.0fx%.0f",
                         grabLayoutLog, split.bounds.width, split.bounds.height,
                         sf.minX, sf.minY, sf.width, sf.height,
                         pf.minX, pf.minY, pf.width, pf.height,
                         df.minX, df.minY, df.width, df.height,
                         pane.guestSize.width, pane.guestSize.height))
        }
    }

    private var grabLayoutLog = 0

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func setSubtitle(_ name: String) {
        window?.subtitle = name
    }

    func play(_ doc: VirtualMachineDocument) {
        store.selection = doc.id
        setSubtitle(doc.name)
        if !booted {
            BootGate.path = doc.prefsPath
            store.runningID = doc.id
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

    func splitView(_ splitView: NSSplitView, constrainMinCoordinate proposedMinimumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        sidebarCollapsed ? 0 : 180
    }

    func splitView(_ splitView: NSSplitView, constrainMaxCoordinate proposedMaximumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        sidebarCollapsed ? 0 : 280
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
            item.image = NSImage(systemSymbolName: "sidebar.left", accessibilityDescription: "Hide Sidebar")
            item.label = "Sidebar"
            item.action = #selector(toggleSidebar)
            item.target = self
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

    @objc private func toggleSidebar() {
        sidebarCollapsed.toggle()
        if sidebarCollapsed {
            sidebarWidth = max(sidebarHost.frame.width, 180)
            sidebarHost.isHidden = true
            split.setPosition(0, ofDividerAt: 0)
        } else {
            sidebarHost.isHidden = false
            split.setPosition(sidebarWidth, ofDividerAt: 0)
        }
        sizeWindowToGuest()
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
        let host = NSHostingController(rootView: VMSettingsView(prefsPath: prefs, live: live) { [weak self] in
            if let sheet = self?.settingsWindow {
                self?.window?.endSheet(sheet)
            }
            self?.settingsWindow = nil
            if live {
                self?.display.setEdgeGrab(PrefsBridge.bool("edgegrab"))
            }
        })
        host.view.frame = NSRect(x: 0, y: 0, width: 720, height: 480)
        let sheet = NSWindow(contentViewController: host)
        sheet.title = "Settings"
        sheet.styleMask = [.titled, .closable]
        settingsWindow = sheet
        window.beginSheet(sheet)
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
