/*
 *  AppSettingsWindowController.swift - SheepShaver's own Settings window (the library window's menu: SheepShaver >
 *  Settings…, Command-,), for settings that belong to the app and not to one virtual machine. Today: the MCP server.
 *
 *  The MCP server lets a program that speaks the Model Context Protocol (an AI assistant, for instance) list, start, stop,
 *  watch (screenshots) and control (mouse, keyboard) the virtual machines checked here. It is off until turned on here.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
final class AppSettingsWindowController: NSWindowController, NSTextFieldDelegate {
    private let store: VirtualMachineStore
    private let enable = NSButton(checkboxWithTitle: "Enable the MCP server", target: nil, action: nil)
    private let portField = NSTextField(string: "")
    private let status = NSTextField(labelWithString: "")
    private let tokenField = NSTextField(string: "")
    private let vmStack = NSStackView()
    private let httpSnippet = NSTextField(wrappingLabelWithString: "")
    private let stdioSnippet = NSTextField(wrappingLabelWithString: "")
    private var observer: NSObjectProtocol?

    init(store: VirtualMachineStore) {
        self.store = store
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 600, height: 560),
                              styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = "SheepShaver Settings"
        window.isReleasedWhenClosed = false
        super.init(window: window)
        build(in: window)
        observer = NotificationCenter.default.addObserver(forName: .mcpStatusChanged, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refreshStatus() }
        }
        refresh()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    func present() {
        store.reload()
        refresh()
        window?.center()
        showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    // MARK: layout

    private func heading(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        return label
    }

    private func note(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        label.textColor = .secondaryLabelColor
        label.preferredMaxLayoutWidth = 540
        return label
    }

    private func monospaced(_ field: NSTextField) {
        field.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        field.isSelectable = true
        field.isEditable = false
        field.preferredMaxLayoutWidth = 540
    }

    private func row(_ views: [NSView]) -> NSStackView {
        let r = NSStackView(views: views)
        r.orientation = .horizontal
        r.spacing = 8
        r.alignment = .centerY
        return r
    }

    private func build(in window: NSWindow) {
        enable.target = self
        enable.action = #selector(toggleEnabled(_:))
        portField.delegate = self
        portField.alignment = .right
        portField.widthAnchor.constraint(equalToConstant: 70).isActive = true
        status.textColor = .secondaryLabelColor
        tokenField.isEditable = false
        tokenField.isSelectable = true
        tokenField.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        tokenField.widthAnchor.constraint(equalToConstant: 250).isActive = true
        let copyToken = NSButton(title: "Copy", target: self, action: #selector(copyToken(_:)))
        let regenerate = NSButton(title: "New Token…", target: self, action: #selector(regenerateToken(_:)))
        let copyHTTP = NSButton(title: "Copy", target: self, action: #selector(copyHTTP(_:)))
        let copyStdio = NSButton(title: "Copy", target: self, action: #selector(copyStdio(_:)))
        monospaced(httpSnippet)
        monospaced(stdioSnippet)
        vmStack.orientation = .vertical
        vmStack.alignment = .leading
        vmStack.spacing = 4

        let stack = NSStackView(views: [
            heading("MCP server"),
            note("Lets an AI assistant or any other Model Context Protocol client take screenshots of, move the mouse in, type into, start and shut down the virtual machines you allow below. It listens on this Mac only and needs the token. It is off until you turn it on."),
            enable,
            row([NSTextField(labelWithString: "Port    "), portField, status]),
            row([NSTextField(labelWithString: "Token"), tokenField, copyToken, regenerate]),
            heading("Virtual machines it may use"),
            vmStack,
            note("A client can see and control everything on the machines you tick, as if it were sitting at them. Machines you leave unticked are invisible to it."),
            heading("Connect a client"),
            note("Claude Code, over HTTP (paste in Terminal):"),
            httpSnippet,
            row([copyHTTP]),
            note("Claude Desktop and other clients that start a program (add to the client's MCP configuration; the token is read from this app, so it is not in the file):"),
            stdioSnippet,
            row([copyStdio]),
        ])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 20, right: 24)
        let root = NSView()
        root.addSubview(stack)
        window.contentView = root
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: root.topAnchor),
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            root.widthAnchor.constraint(equalToConstant: 600),
        ])
    }

    // MARK: state

    private func refresh() {
        enable.state = MCPSettings.enabled ? .on : .off
        portField.stringValue = String(MCPSettings.port)
        let token = MCPSettings.token
        tokenField.stringValue = token
        refreshStatus()
        vmStack.arrangedSubviews.forEach { vmStack.removeArrangedSubview($0); $0.removeFromSuperview() }
        if store.machines.isEmpty {
            vmStack.addArrangedSubview(note("No virtual machines yet."))
        }
        for machine in store.machines {
            let box = NSButton(checkboxWithTitle: machine.name, target: self, action: #selector(toggleVM(_:)))
            box.identifier = NSUserInterfaceItemIdentifier(machine.id)
            box.state = MCPSettings.allowedIDs.contains(machine.id) ? .on : .off
            box.toolTip = machine.id
            vmStack.addArrangedSubview(box)
        }
        let port = MCPSettings.port
        httpSnippet.stringValue = "claude mcp add --transport http sheepshaver http://127.0.0.1:\(port)\(MCPHTTP.endpoint) --header \"Authorization: Bearer \(token)\""
        let config: [String: Any] = ["mcpServers": ["sheepshaver": ["command": Bundle.main.executablePath ?? "SheepShaver", "args": ["--mcp-stdio"]]]]
        let data = (try? JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])) ?? Data()
        stdioSnippet.stringValue = String(decoding: data, as: UTF8.self)
        window?.layoutIfNeeded()
        window?.setContentSize(window?.contentView?.fittingSize ?? NSSize(width: 600, height: 560))
    }

    private func refreshStatus() {
        status.stringValue = MCPService.shared.status
    }

    // MARK: actions

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
            refresh()
            if MCPSettings.enabled { MCPService.shared.apply() }
        }
    }

    @objc private func toggleVM(_ sender: NSButton) {
        guard let id = sender.identifier?.rawValue else { return }
        var allowed = MCPSettings.allowedIDs
        if sender.state == .on { allowed.insert(id) } else { allowed.remove(id) }
        MCPSettings.allowedIDs = allowed
    }

    private func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    @objc private func copyToken(_ sender: Any?) { copy(tokenField.stringValue) }
    @objc private func copyHTTP(_ sender: Any?) { copy(httpSnippet.stringValue) }
    @objc private func copyStdio(_ sender: Any?) { copy(stdioSnippet.stringValue) }

    @objc private func regenerateToken(_ sender: Any?) {
        guard let window else { return }
        let alert = NSAlert()
        alert.messageText = "Make a new token?"
        alert.informativeText = "Clients using the old token stop working until you give them the new one (a client that starts SheepShaver --mcp-stdio picks it up when it restarts)."
        alert.addButton(withTitle: "Make New Token")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { [weak self] response in
            guard response == .alertFirstButtonReturn else { return }
            MainActor.assumeIsolated {
                _ = MCPService.shared.regenerateToken()
                self?.refresh()
            }
        }
    }
}
