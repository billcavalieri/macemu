/*
 *  MCPService.swift - Owns the MCP server in the library (manager) process: starts and stops it with the Settings,
 *  and is the backend its tools run against (the VM library, the VM processes and their control sockets).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

extension Notification.Name {
    /// The MCP server started, stopped or failed; the Settings window shows `MCPService.shared.status`.
    static let mcpStatusChanged = Notification.Name("SheepMCPStatusChanged")
}

/// The tools' view of the app. Tool calls arrive on background threads; the VM library lives on the main actor, so each
/// access to it hops there for the moment it needs it.
final class MCPAppBackend: MCPBackend, @unchecked Sendable {
    private let store: VirtualMachineStore
    init(store: VirtualMachineStore) { self.store = store }

    private func onMain<T: Sendable>(_ body: @MainActor @Sendable () -> T) -> T {
        if Thread.isMainThread { return MainActor.assumeIsolated(body) }
        return DispatchQueue.main.sync { MainActor.assumeIsolated(body) }
    }

    func allowedVMs() -> [MCPVM] {
        let allowed = MCPSettings.allowedIDs
        let documents = onMain { store.machines.filter { allowed.contains($0.id) } }
        return documents.map { MCPVM(id: $0.id, name: $0.name, running: VMControlClient.isRunning(vmID: $0.id)) }
    }

    func start(vmID: String, waitForReady: Bool, timeoutSeconds: Int) throws -> [String: Any] {
        if VMControlClient.isRunning(vmID: vmID) {
            var result: [String: Any] = ["started": false, "already_running": true]
            if waitForReady { result["ready"] = try waitUntilReady(vmID: vmID, timeoutSeconds: timeoutSeconds) }
            return result
        }
        guard let document = onMain({ store.document(id: vmID) }) else { throw ControlError("no such virtual machine") }
        let launched = onMain { store.launch(document, background: true) }
        guard launched else { return ["started": false, "already_running": true] }
        let began = Date()
        // The process is up when its control socket answers
        while !VMControlClient.isRunning(vmID: vmID) {
            if Date().timeIntervalSince(began) > 60 {
                throw ControlError("the virtual machine process did not come up within 60 seconds (is another VM holding its disk image?)")
            }
            usleep(300_000)
        }
        var result: [String: Any] = ["started": true]
        if waitForReady { result["ready"] = try waitUntilReady(vmID: vmID, timeoutSeconds: max(1, timeoutSeconds - Int(Date().timeIntervalSince(began)))) }
        return result
    }

    /// Mac OS has finished starting when the Sheep Shears tool in the guest reports in. False after the timeout (the tool
    /// may not be installed): the VM is still running.
    private func waitUntilReady(vmID: String, timeoutSeconds: Int) throws -> Bool {
        let began = Date()
        while Date().timeIntervalSince(began) < Double(timeoutSeconds) {
            if let status = try? VMControlClient.call(vmID: vmID, op: "status", args: [:], timeout: 10),
               (status["sheep_shears"] as? [String: Any])?["running"] as? Bool == true { return true }
            usleep(500_000)
        }
        return false
    }

    func control(vmID: String, op: String, args: [String: Any]) throws -> [String: Any] {
        try VMControlClient.call(vmID: vmID, op: op, args: args, timeout: op == "screenshot" ? 15 : 120)
    }
}

@MainActor
final class MCPService {
    static let shared = MCPService()
    private let server = MCPServer()
    private var backend: MCPAppBackend?
    private var store: VirtualMachineStore?
    private(set) var status = "Off"

    /// Called once by the library window, which owns the VM library.
    func configure(store: VirtualMachineStore) {
        self.store = store
        backend = MCPAppBackend(store: store)
        server.onStatus = { [weak self] line in
            DispatchQueue.main.async { MainActor.assumeIsolated { self?.setStatus(line) } }
        }
        apply()
    }

    private func setStatus(_ line: String) {
        status = line
        NotificationCenter.default.post(name: .mcpStatusChanged, object: nil)
    }

    /// Starts, restarts or stops the server to match the settings. Call after any of them changes.
    func apply() {
        guard let backend else { return }
        if MCPSettings.enabled {
            let token = MCPSettings.token
            setStatus("Starting…")
            server.start(port: MCPSettings.port, token: { token }, backend: backend)
        } else {
            server.stop()
            setStatus("Off")
        }
    }

    /// A new token takes effect at once (the running server reads the token for every request).
    func regenerateToken() -> String {
        let token = MCPSettings.regenerateToken()
        apply()
        return token
    }
}
