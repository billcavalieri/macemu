/*
 *  VMControlClient.swift - A blocking client for a VM's control socket (see VMControlServer.swift). Used by the MCP
 *  backend, which runs on background threads.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Darwin

enum VMControlClient {
    /// Whether a VM with this id is running and answering on its control socket.
    static func isRunning(vmID: String) -> Bool {
        (try? call(vmID: vmID, op: "ping", args: [:], timeout: 2)) != nil
    }

    /// Sends one request and waits for its reply. Throws ControlError with the VM's own message when it refuses.
    static func call(vmID: String, op: String, args: [String: Any], timeout: TimeInterval = 120) throws -> [String: Any] {
        let fd = VMControlServer.connect(ControlProtocol.socketPath(vmID: vmID))
        guard fd >= 0 else { throw ControlError("the virtual machine is not running") }
        defer { close(fd) }
        var one: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
        var tv = timeval(tv_sec: Int(timeout), tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))

        var request = args
        request["op"] = op
        request["id"] = 1
        guard var line = try? JSONSerialization.data(withJSONObject: request) else { throw ControlError("bad arguments") }
        line.append(0x0a)
        var sent = 0
        while sent < line.count {
            let n = line.withUnsafeBytes { write(fd, $0.baseAddress!.advanced(by: sent), line.count - sent) }
            if n <= 0 { throw ControlError("the virtual machine stopped answering") }
            sent += n
        }
        var buffer = Data()
        var chunk = [UInt8](repeating: 0, count: 65536)
        while true {
            let n = read(fd, &chunk, chunk.count)
            if n < 0 && errno == EINTR { continue }
            if n <= 0 { throw ControlError(n == 0 ? "the virtual machine closed the connection" : "the virtual machine did not answer in time") }
            buffer.append(chunk, count: n)
            if let newline = buffer.firstIndex(of: 0x0a) {
                let reply = buffer[buffer.startIndex..<newline]
                guard let object = try? JSONSerialization.jsonObject(with: reply), let dictionary = object as? [String: Any] else {
                    throw ControlError("unreadable reply from the virtual machine")
                }
                if dictionary["ok"] as? Bool == true { return dictionary["result"] as? [String: Any] ?? [:] }
                throw ControlError(dictionary["error"] as? String ?? "the virtual machine refused")
            }
            if buffer.count > 64 << 20 { throw ControlError("reply too large") }
        }
    }
}
