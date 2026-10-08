/*
 *  MCPServer.swift - The MCP endpoint: HTTP on 127.0.0.1 only. Parsing and every access rule are in MCPHTTP.swift and
 *  MCPProtocol.swift (pure, unit tested); this file only moves bytes with Network.framework.
 *
 *  One request per connection (Connection: close). A client that sends nothing for 15 seconds is dropped; once a request
 *  has arrived its answer may take as long as the tool needs (starting a VM can wait minutes for Mac OS to boot).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Network

final class MCPServer: @unchecked Sendable {
    private var listener: NWListener?
    private let queue = DispatchQueue(label: "sheepshaver.mcp.listener")
    private let work = DispatchQueue(label: "sheepshaver.mcp.work", attributes: .concurrent)
    private let lock = NSLock()
    private var open = 0
    static let maxConnections = 16

    private(set) var port: UInt16 = 0
    /// Called (on an arbitrary queue) with a line for the Settings window: "Listening on ..." or the reason it is not.
    var onStatus: (@Sendable (String) -> Void)?

    func start(port: UInt16, token: @escaping @Sendable () -> String, backend: MCPBackend) {
        stop()
        self.port = port
        guard let nwPort = NWEndpoint.Port(rawValue: port) else { onStatus?("Invalid port"); return }
        let parameters = NWParameters.tcp
        parameters.requiredInterfaceType = .loopback
        parameters.requiredLocalEndpoint = .hostPort(host: .ipv4(.loopback), port: nwPort)
        parameters.allowLocalEndpointReuse = true
        do {
            let listener = try NWListener(using: parameters)
            listener.stateUpdateHandler = { [weak self] state in
                switch state {
                case .ready: self?.onStatus?("Listening on http://127.0.0.1:\(port)/mcp")
                case .failed(let error): self?.onStatus?("Could not listen on port \(port): \(error.localizedDescription)")
                case .cancelled: break
                default: break
                }
            }
            listener.newConnectionHandler = { [weak self] connection in
                self?.accept(connection, token: token, backend: backend)
            }
            self.listener = listener
            listener.start(queue: queue)
        } catch {
            onStatus?("Could not listen on port \(port): \(error.localizedDescription)")
        }
    }

    func stop() {
        listener?.cancel()
        listener = nil
    }

    private func accept(_ connection: NWConnection, token: @escaping @Sendable () -> String, backend: MCPBackend) {
        lock.lock()
        let tooMany = open >= Self.maxConnections
        if !tooMany { open += 1 }
        lock.unlock()
        if tooMany { connection.cancel(); return }
        let port = self.port
        connection.start(queue: queue)
        nonisolated(unsafe) let timeout = DispatchWorkItem { connection.cancel() }
        queue.asyncAfter(deadline: .now() + 15, execute: timeout)
        receive(connection, buffer: Data()) { [weak self] request in
            timeout.cancel()
            guard let request else { self?.finish(connection, nil); return }
            self?.work.async {
                let response = MCPHTTP.respond(to: request, token: token(), port: port, backend: backend)
                self?.finish(connection, response)
            }
        }
    }

    private func receive(_ connection: NWConnection, buffer: Data, done: @escaping @Sendable (HTTPRequest?) -> Void) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            var buffer = buffer
            if let data { buffer.append(data) }
            switch MCPHTTP.parse(buffer) {
            case .request(let request):
                done(request)
            case .failure(let response):
                connection.send(content: response.serialized(), completion: .contentProcessed { _ in connection.cancel() })
                done(nil)
            case .needMore:
                if error != nil || isComplete { connection.cancel(); done(nil); return }
                self?.receive(connection, buffer: buffer, done: done)
            }
        }
    }

    private func finish(_ connection: NWConnection, _ response: HTTPResponse?) {
        defer { lock.lock(); open -= 1; lock.unlock() }
        guard let response else { connection.cancel(); return }
        connection.send(content: response.serialized(), completion: .contentProcessed { _ in connection.cancel() })
    }
}
