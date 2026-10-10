/*
 *  AppSettingsWindowController.swift - SheepShaver's own Settings window (SheepShaver > Settings…, Command-,), for
 *  settings that belong to the app and not to one virtual machine: where virtual machines show (General) and the MCP server.
 *
 *  The MCP server lets a program that speaks the Model Context Protocol (an AI assistant, for instance) list, start, stop,
 *  watch (screenshots) and control (mouse, keyboard) the virtual machines checked here. It is off until turned on here.
 *  Tabs: General, MCP Server (on/off, port, token, which virtual machines) and Connect (what to paste into a client).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class AppSettingsWindowController: NSWindowController {
    private let general = GeneralPane()
    private let server: MCPServerPane
    private let connect: MCPConnectPane
    private var observer: NSObjectProtocol?

    init(store: VirtualMachineStore) {
        server = MCPServerPane(store: store)
        connect = MCPConnectPane()
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        for (pane, title, symbol) in [(general as NSViewController, "General", "gearshape"), (server as NSViewController, "MCP Server", "server.rack"),
                                      (connect as NSViewController, "Connect", "link")] {
            pane.title = title
            let item = NSTabViewItem(viewController: pane)
            item.label = title
            item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: title)
            tabs.addTabViewItem(item)
        }
        let window = NSWindow(contentViewController: tabs)
        window.styleMask = [.titled, .closable]
        window.isReleasedWhenClosed = false
        super.init(window: window)
        server.onChange = { [weak self] in self?.refresh() }
        connect.onChange = { [weak self] in self?.refresh() }
        observer = NotificationCenter.default.addObserver(forName: .mcpStatusChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.server.refreshStatus() }
        }
        refresh()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func present() {
        refresh()
        window?.center()
        showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func refresh() {
        server.refresh()
        connect.refresh()
    }
}

// MARK: - helpers shared by both panes

@MainActor
private func note(_ text: String, width: CGFloat = 460) -> NSTextField {
    let label = NSTextField(wrappingLabelWithString: text)
    label.font = .preferredFont(forTextStyle: .callout)
    label.textColor = .secondaryLabelColor
    label.preferredMaxLayoutWidth = width
    return label
}

@MainActor
private func heading(_ text: String) -> NSTextField {
    let label = NSTextField(labelWithString: text)
    label.font = .preferredFont(forTextStyle: .headline)
    return label
}

@MainActor
private func fixedWidthRoot(_ stack: NSStackView, width: CGFloat = 520) -> NSView {
    stack.orientation = .vertical
    stack.alignment = .leading
    stack.spacing = 12
    stack.translatesAutoresizingMaskIntoConstraints = false
    let root = NSView()
    root.addSubview(stack)
    NSLayoutConstraint.activate([
        root.widthAnchor.constraint(equalToConstant: width),
        stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 22),
        stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 30),
        stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -30),
        stack.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -24),
    ])
    return root
}

@MainActor
private func copyToPasteboard(_ text: String) {
    NSPasteboard.general.clearContents()
    NSPasteboard.general.setString(text, forType: .string)
}

// MARK: - General tab

@MainActor
private final class GeneralPane: NSViewController {
    private let inLibrary = NSButton(checkboxWithTitle: "Show virtual machines in the library window", target: nil, action: nil)

    override func loadView() {
        inLibrary.target = self
        inLibrary.action = #selector(toggled(_:))
        inLibrary.state = VirtualMachineStore.opensInLibraryWindow ? .on : .off
        let stack = NSStackView(views: [
            inLibrary,
            note("A virtual machine you start appears next to the list, and you switch between running ones by choosing them. Turn this off to give each its own window. Either way, a virtual machine can be moved into its own window (Detach) and back (Attach) while it runs. The change applies to virtual machines started from now on."),
        ])
        stack.setCustomSpacing(8, after: inLibrary)
        view = fixedWidthRoot(stack)
        preferredContentSize = view.fittingSize
    }

    @objc private func toggled(_ sender: NSButton) {
        UserDefaults.standard.set(sender.state == .on, forKey: "SheepOpenInLibraryWindow")
    }
}

// MARK: - Server tab

@MainActor
private final class MCPServerPane: NSViewController, NSTextFieldDelegate {
    private let store: VirtualMachineStore
    var onChange: () -> Void = {}
    private let enable = NSButton(checkboxWithTitle: "Enable the MCP server", target: nil, action: nil)
    private let portField = NSTextField(string: "")
    private let status = NSTextField(labelWithString: "")
    private let tokenLabel = NSTextField(labelWithString: "")
    private let showToken = NSButton(title: "Show", target: nil, action: nil)
    private let vmStack = NSStackView()
    private var revealed = false

    init(store: VirtualMachineStore) {
        self.store = store
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    override func loadView() {
        enable.target = self
        enable.action = #selector(toggleEnabled(_:))
        portField.delegate = self
        portField.alignment = .right
        portField.widthAnchor.constraint(equalToConstant: 70).isActive = true
        portField.setAccessibilityLabel("Port")
        status.textColor = .secondaryLabelColor
        tokenLabel.font = .monospacedSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular)
        tokenLabel.isSelectable = false
        tokenLabel.lineBreakMode = .byTruncatingMiddle
        tokenLabel.widthAnchor.constraint(equalToConstant: 170).isActive = true
        tokenLabel.setAccessibilityLabel("Token")
        showToken.target = self
        showToken.action = #selector(toggleReveal(_:))
        let copy = NSButton(title: "Copy", target: self, action: #selector(copyToken(_:)))
        let regenerate = NSButton(title: "New Token…", target: self, action: #selector(regenerateToken(_:)))
        vmStack.orientation = .vertical
        vmStack.alignment = .leading
        vmStack.spacing = 6

        func row(_ title: String, _ views: [NSView]) -> NSStackView {
            let label = NSTextField(labelWithString: title)
            label.alignment = .right
            label.widthAnchor.constraint(equalToConstant: 48).isActive = true
            let r = NSStackView(views: [label] + views)
            r.orientation = .horizontal
            r.spacing = 8
            r.alignment = .centerY
            return r
        }
        let stack = NSStackView(views: [
            enable,
            note("Lets an AI assistant or any other Model Context Protocol client take screenshots of, move the mouse in, type into, start and shut down the virtual machines you allow below. It listens on this Mac only and needs the token."),
            row("Port:", [portField, status]),
            row("Token:", [tokenLabel, showToken, copy, regenerate]),
            heading("Virtual Machines It May Use"),
            vmStack,
            note("A client can see and control everything on the machines you tick, as if it were sitting at them. Machines you leave unticked are invisible to it."),
        ])
        stack.setCustomSpacing(18, after: stack.arrangedSubviews[1])
        stack.setCustomSpacing(22, after: stack.arrangedSubviews[3])
        view = fixedWidthRoot(stack)
    }

    func refresh() {
        guard isViewLoaded else { return }
        enable.state = MCPSettings.enabled ? .on : .off
        portField.stringValue = String(MCPSettings.port)
        let token = MCPSettings.token
        tokenLabel.stringValue = revealed ? token : String(repeating: "•", count: 24)
        showToken.title = revealed ? "Hide" : "Show"
        refreshStatus()
        store.reload()
        vmStack.arrangedSubviews.forEach { vmStack.removeArrangedSubview($0); $0.removeFromSuperview() }
        if store.machines.isEmpty {
            vmStack.addArrangedSubview(note("There are no virtual machines yet."))
        }
        for machine in store.machines {
            let box = NSButton(checkboxWithTitle: machine.name, target: self, action: #selector(toggleVM(_:)))
            box.identifier = NSUserInterfaceItemIdentifier(machine.id)
            box.state = MCPSettings.allowedIDs.contains(machine.id) ? .on : .off
            box.toolTip = machine.id
            vmStack.addArrangedSubview(box)
        }
        view.window?.layoutIfNeeded()
        preferredContentSize = view.fittingSize
    }

    func refreshStatus() {
        guard isViewLoaded else { return }
        status.stringValue = MCPService.shared.status
    }

    @objc private func toggleEnabled(_ sender: NSButton) {
        MCPSettings.enabled = sender.state == .on
        MCPService.shared.apply()
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        guard let value = UInt16(portField.stringValue), value >= 1024 else {
            portField.stringValue = String(MCPSettings.port)
            NSSound.beep()
            return
        }
        if value != MCPSettings.port {
            MCPSettings.port = value
            onChange()
            if MCPSettings.enabled { MCPService.shared.apply() }
        }
    }

    @objc private func toggleVM(_ sender: NSButton) {
        guard let id = sender.identifier?.rawValue else { return }
        var allowed = MCPSettings.allowedIDs
        if sender.state == .on { allowed.insert(id) } else { allowed.remove(id) }
        MCPSettings.allowedIDs = allowed
    }

    @objc private func toggleReveal(_ sender: Any?) {
        revealed.toggle()
        onChange()
    }

    @objc private func copyToken(_ sender: Any?) { copyToPasteboard(MCPSettings.token) }

    @objc private func regenerateToken(_ sender: Any?) {
        guard let window = view.window else { return }
        let alert = NSAlert()
        alert.messageText = "Make a new token?"
        alert.informativeText = "Clients using the old token stop working until you give them the new one (a client that starts SheepShaver --mcp-stdio picks it up when it restarts)."
        alert.addButton(withTitle: "Make New Token")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard response == .alertFirstButtonReturn else { return }
            MainActor.assumeIsolated {
                _ = MCPService.shared.regenerateToken()
                self?.onChange()
            }
        }
    }
}

// MARK: - Connect tab

@MainActor
private final class MCPConnectPane: NSViewController {
    var onChange: () -> Void = {}
    private let httpSnippet = NSTextField(wrappingLabelWithString: "")
    private let stdioSnippet = NSTextField(wrappingLabelWithString: "")
    private var httpCommand = ""
    private var stdioConfig = ""

    override func loadView() {
        for snippet in [httpSnippet, stdioSnippet] {
            snippet.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
            snippet.isSelectable = true
            snippet.preferredMaxLayoutWidth = 440
        }
        func box(_ label: NSTextField) -> NSBox {
            let b = NSBox()
            b.boxType = .custom
            b.fillColor = .textBackgroundColor
            b.borderColor = .separatorColor
            b.borderWidth = 1
            b.cornerRadius = 6
            b.contentViewMargins = NSSize(width: 8, height: 8)
            b.contentView = label
            b.translatesAutoresizingMaskIntoConstraints = false
            return b
        }
        let copyHTTP = NSButton(title: "Copy Command", target: self, action: #selector(copyHTTP(_:)))
        let copyStdio = NSButton(title: "Copy Configuration", target: self, action: #selector(copyStdio(_:)))
        let stack = NSStackView(views: [
            heading("Claude Code"),
            note("Copy the command and paste it into Terminal. The copy includes the token, which is left out of what is shown here."),
            box(httpSnippet), copyHTTP,
            heading("Claude Desktop and Other Clients"),
            note("Add this to the client's MCP configuration. It starts SheepShaver to talk to the server, and SheepShaver reads the token itself, so the token is not in the file."),
            box(stdioSnippet), copyStdio,
        ])
        stack.setCustomSpacing(22, after: stack.arrangedSubviews[3])
        view = fixedWidthRoot(stack, width: 540)
    }

    func refresh() {
        guard isViewLoaded else { return }
        let token = MCPSettings.token
        let port = MCPSettings.port
        httpCommand = "claude mcp add --transport http sheepshaver http://127.0.0.1:\(port)\(MCPHTTP.endpoint) --header \"Authorization: Bearer \(token)\""
        httpSnippet.stringValue = httpCommand.replacingOccurrences(of: token, with: "<token>")
        let config: [String: Any] = ["mcpServers": ["sheepshaver": ["command": Bundle.main.executablePath ?? "SheepShaver", "args": ["--mcp-stdio"]]]]
        let data = (try? JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])) ?? Data()
        stdioConfig = String(decoding: data, as: UTF8.self)
        stdioSnippet.stringValue = stdioConfig
        view.window?.layoutIfNeeded()
        preferredContentSize = view.fittingSize
    }

    @objc private func copyHTTP(_ sender: Any?) { copyToPasteboard(httpCommand) }
    @objc private func copyStdio(_ sender: Any?) { copyToPasteboard(stdioConfig) }
}
