/*
 *  VMControlServer.swift - The control socket of a running VM. The manager's MCP server connects to it to take
 *  screenshots, move and click the pointer, type, and shut the guest down. One server per VM process; the socket is
 *  /tmp/sheepshaver-<uid>/<vm id>.sock (mode 0700 directory, 0600 socket), removed when the process ends.
 *
 *  Requests are newline-delimited JSON (see VMControlProtocol.swift). Input is injected through the same
 *  VideoHost* entry points the window uses, so it works whether or not the window is key or the pointer grabbed. Button and key events
 *  are posted from the main thread, like the window's own, because the emulator's input rings have one producer.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Darwin

@_silgen_name("VideoHostSnapshot")
private func VideoHostSnapshot(_ info: UnsafeMutablePointer<Int32>, _ palette: UnsafeMutablePointer<UInt8>,
                               _ pixels: UnsafeMutablePointer<UnsafeMutablePointer<UInt8>?>, _ length: UnsafeMutablePointer<UInt32>) -> Int32
@_silgen_name("VideoHostSnapshotFree")
private func VideoHostSnapshotFree(_ pixels: UnsafeMutablePointer<UInt8>?)
@_silgen_name("VideoHostGuestMouse")
private func VideoHostGuestMouse(_ x: UnsafeMutablePointer<Int32>, _ y: UnsafeMutablePointer<Int32>)
@_silgen_name("VideoHostGuestScreen")
private func VideoHostGuestScreen(_ w: UnsafeMutablePointer<Int32>, _ h: UnsafeMutablePointer<Int32>, _ depth: UnsafeMutablePointer<Int32>)
@_silgen_name("VideoHostMouseMove")
private func VideoHostMouseMove(_ dx: Int32, _ dy: Int32)
@_silgen_name("VideoHostMouseButton")
private func VideoHostMouseButton(_ button: Int32, _ down: Int32)
@_silgen_name("VideoHostKey")
private func VideoHostKey(_ code: Int32, _ down: Int32)
@_silgen_name("VideoHostRequestQuit")
private func VideoHostRequestQuit()

/// Everything the server needs to know about this VM that lives in the app (kept as closures so the server has no
/// dependency on the window classes).
struct VMControlContext: Sendable {
    var id: String
    var name: @Sendable () -> String
}

final class VMControlServer: @unchecked Sendable {
    static let shared = VMControlServer()

    private var listener: Int32 = -1
    private var path = ""
    private var context = VMControlContext(id: "", name: { "" })
    private let input = DispatchQueue(label: "sheepshaver.control.input")     // one input action at a time

    /// Starts listening. Safe to call once the guest video is up; a second call does nothing.
    func start(_ context: VMControlContext) {
        guard listener < 0 else { return }
        self.context = context
        let socketPath = ControlProtocol.socketPath(vmID: context.id)
        let directory = (socketPath as NSString).deletingLastPathComponent
        try? FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
        chmod(directory, 0o700)
        // A socket left by a VM that died: nobody answers on it, so it can be replaced. One that answers is a VM
        // that is really running with this id, and is left alone.
        if Self.connect(socketPath) >= 0 { return }
        unlink(socketPath)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return }
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(socketPath.utf8)
        guard bytes.count < MemoryLayout.size(ofValue: address.sun_path) else { close(fd); return }
        withUnsafeMutableBytes(of: &address.sun_path) { buffer in
            for (i, b) in bytes.enumerated() { buffer[i] = b }
            buffer[bytes.count] = 0
        }
        let bound = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size)) }
        }
        guard bound == 0, listen(fd, 8) == 0 else { close(fd); return }
        chmod(socketPath, 0o600)
        listener = fd
        path = socketPath
        atexit { VMControlServer.shared.removeSocket() }
        let thread = Thread { [self] in acceptLoop() }
        thread.name = "sheepshaver.control.accept"
        thread.start()
        if nwDiagnosticsOn {
            fputs("Control: listening on \(socketPath)\n", stdout)
            fflush(stdout)
        }
    }

    fileprivate func removeSocket() {
        if !path.isEmpty { unlink(path) }
    }

    /// A connected client socket for a socket path, or -1.
    static func connect(_ socketPath: String) -> Int32 {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return -1 }
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(socketPath.utf8)
        guard bytes.count < MemoryLayout.size(ofValue: address.sun_path) else { close(fd); return -1 }
        withUnsafeMutableBytes(of: &address.sun_path) { buffer in
            for (i, b) in bytes.enumerated() { buffer[i] = b }
            buffer[bytes.count] = 0
        }
        let result = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { Darwin.connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size)) }
        }
        if result != 0 { close(fd); return -1 }
        return fd
    }

    // MARK: connections

    private func acceptLoop() {
        while true {
            let client = accept(listener, nil, nil)
            if client < 0 {
                if errno == EINTR { continue }
                return
            }
            var one: Int32 = 1
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
            let thread = Thread { [self] in serve(client) }
            thread.name = "sheepshaver.control.client"
            thread.start()
        }
    }

    private func serve(_ client: Int32) {
        defer { close(client) }
        var buffer = Data()
        var chunk = [UInt8](repeating: 0, count: 65536)
        while true {
            let n = read(client, &chunk, chunk.count)
            if n <= 0 { return }
            buffer.append(chunk, count: n)
            if buffer.count > ControlProtocol.maxLine * 2 { return }
            for line in ControlProtocol.takeLines(from: &buffer) {
                let reply: Data
                switch ControlProtocol.parse(line) {
                case .success(let request):
                    do {
                        reply = ControlProtocol.success(id: request.id, result: try perform(request.op))
                    } catch let error as ControlError {
                        reply = ControlProtocol.failure(id: request.id, error.message)
                    } catch {
                        reply = ControlProtocol.failure(id: request.id, "failed")
                    }
                case .failure(let error):
                    reply = ControlProtocol.failure(id: ControlProtocol.id(of: line), error.message)
                }
                if !Self.writeAll(client, reply) { return }
            }
        }
    }

    private static func writeAll(_ fd: Int32, _ data: Data) -> Bool {
        var offset = 0
        return data.withUnsafeBytes { raw -> Bool in
            while offset < data.count {
                let n = write(fd, raw.baseAddress!.advanced(by: offset), data.count - offset)
                if n < 0 { if errno == EINTR { continue }; return false }
                offset += n
            }
            return true
        }
    }

    // MARK: operations

    private func perform(_ op: ControlOp) throws -> [String: Any] {
        switch op {
        case .ping:
            return ["pong": true]
        case .status:
            return status()
        case .screenshot(let maxWidth):
            return try screenshot(maxWidth: maxWidth)
        case .shutdown(let force):
            return try shutdown(force: force)
        default:
            return try input.sync { try performInput(op) }
        }
    }

    private func screen() -> (w: Int, h: Int, depth: Int) {
        var w: Int32 = 0, h: Int32 = 0, d: Int32 = 0
        VideoHostGuestScreen(&w, &h, &d)
        return (Int(w), Int(h), Int(d))
    }

    private func cursor() -> (x: Int, y: Int) {
        var x: Int32 = 0, y: Int32 = 0
        VideoHostGuestMouse(&x, &y)
        return (Int(x), Int(y))
    }

    private func status() -> [String: Any] {
        let s = screen(), c = cursor()
        let shears = ShearsHost.shared.status
        var shutdown = "none"
        switch shears.shutdown {
        case .idle: shutdown = "none"
        case .requested: shutdown = "requested"
        case .sent: shutdown = "sent"
        case .failed(let code): shutdown = "failed (\(code))"
        }
        return ["id": context.id, "name": context.name(), "state": "running",
                "width": s.w, "height": s.h, "depth": s.depth, "cursor": ["x": c.x, "y": c.y],
                "sheep_shears": ["running": shears.toolRunning, "version": Int(shears.toolVersion), "shutdown": shutdown]]
    }

    private func screenshot(maxWidth: Int?) throws -> [String: Any] {
        var info = [Int32](repeating: 0, count: 4)
        var palette = [UInt8](repeating: 0, count: 256 * 3)
        var pixels: UnsafeMutablePointer<UInt8>?
        var length: UInt32 = 0
        guard VideoHostSnapshot(&info, &palette, &pixels, &length) == 0, let pixels else {
            throw ControlError("the guest did not answer in time (is it running and started?)")
        }
        defer { VideoHostSnapshotFree(pixels) }
        let frame = GuestFrame(width: Int(info[0]), height: Int(info[1]), rowBytes: Int(info[2]), depth: Int(info[3]),
                               pixels: Array(UnsafeBufferPointer(start: pixels, count: Int(length))), palette: palette)
        guard let scaled = frame.scaledRGBA(maxWidth: maxWidth),
              let png = GuestFrame.png(width: scaled.width, height: scaled.height, rgba: scaled.rgba) else {
            throw ControlError("the guest screen could not be read (\(frame.width)x\(frame.height), \(frame.depth)-bit)")
        }
        return ["png_base64": png.base64EncodedString(), "width": scaled.width, "height": scaled.height,
                "guest_width": frame.width, "guest_height": frame.height]
    }

    private func shutdown(force: Bool) throws -> [String: Any] {
        if force {
            VideoHostRequestQuit()
            return ["method": "force", "note": "the VM is being stopped without saving, like pulling the plug"]
        }
        guard ShearsHost.shared.status.toolRunning else {
            throw ControlError("a clean shutdown needs the Sheep Shears tool running in the guest, and it is not; install it (Guest > Install Sheep Shears) or pass force=true to stop the VM immediately without saving")
        }
        guard ShearsHost.shared.requestShutdown() else {
            throw ControlError("a shutdown was already requested; wait for the guest, or pass force=true")
        }
        return ["method": "clean", "note": "Mac OS was asked to shut down; applications with unsaved work may ask about it first, and the VM process ends when the guest has powered off"]
    }

    // MARK: input (always on the `input` queue)

    private static let step: UInt32 = 12_000       // µs between pointer reports
    private static let nearStep: UInt32 = 30_000   // the guest applies a report and updates Mouse before we look again

    private func onMain(_ body: () -> Void) {
        if Thread.isMainThread { body() } else { DispatchQueue.main.sync(execute: body) }
    }

    private func key(_ code: Int, down: Bool) {
        onMain { VideoHostKey(Int32(code), down ? 1 : 0) }
    }

    private func button(_ down: Bool) {
        onMain { VideoHostMouseButton(0, down ? 1 : 0) }
    }

    /// Walks the guest pointer to (x, y): relative moves, then read where it is, until it is there.
    private func move(toX x: Int, toY y: Int) throws {
        let s = screen()
        let target = (x: min(max(x, 0), max(0, s.w - 1)), y: min(max(y, 0), max(0, s.h - 1)))
        for _ in 0..<800 {
            let here = cursor()
            guard let m = MouseStepPlanner.next(from: here, to: target) else { return }
            VideoHostMouseMove(Int32(m.dx), Int32(m.dy))
            let near = abs(m.dx) <= 1 && abs(m.dy) <= 1
            usleep(near ? Self.nearStep : Self.step)
        }
        let end = cursor()
        throw ControlError("the pointer stopped at \(end.x),\(end.y) and did not reach \(target.x),\(target.y)")
    }

    private func press(_ button: ControlButton, down: Bool) {
        if button == .right { key(GuestKeys.control, down: true) }       // Control-click: the classic Mac OS right click
        if down { self.button(true) } else { self.button(false) }
        if button == .right { key(GuestKeys.control, down: false) }
    }

    private func stroke(_ s: GuestKeys.Stroke) {
        if s.shift { key(GuestKeys.shift, down: true) }
        key(s.code, down: true)
        usleep(25_000)
        key(s.code, down: false)
        if s.shift { key(GuestKeys.shift, down: false) }
        usleep(25_000)
    }

    private func performInput(_ op: ControlOp) throws -> [String: Any] {
        switch op {
        case .mouseMove(let x, let y):
            try move(toX: x, toY: y)
        case .mouseClick(let x, let y, let b, let count):
            if let x, let y { try move(toX: x, toY: y) }
            for i in 0..<count {
                press(b, down: true)
                usleep(50_000)
                press(b, down: false)
                if i + 1 < count { usleep(90_000) }       // inside the guest's double-click time
            }
        case .mouseDown(let b):
            press(b, down: true)
        case .mouseUp(let b):
            press(b, down: false)
        case .mouseDrag(let fx, let fy, let tx, let ty, let b):
            try move(toX: fx, toY: fy)
            press(b, down: true)
            usleep(80_000)
            do { try move(toX: tx, toY: ty) } catch { press(b, down: false); throw error }
            usleep(60_000)
            press(b, down: false)
        case .typeText(let text):
            for c in text { if let s = GuestKeys.stroke(for: c) { stroke(s) } }
        case .pressKey(let name, let modifiers):
            let codes = modifiers.compactMap { GuestKeys.modifierCode($0) }
            for m in codes { key(m, down: true) }
            usleep(20_000)
            if let code = GuestKeys.code(forKey: name) {
                var strokeNeedsShift = false
                if name.count == 1, name.first!.isUppercase { strokeNeedsShift = true }
                stroke(GuestKeys.Stroke(code: code, shift: strokeNeedsShift && !codes.contains(GuestKeys.shift)))
            }
            for m in codes.reversed() { key(m, down: false) }
        default:
            throw ControlError("not an input operation")
        }
        let c = cursor()
        return ["cursor": ["x": c.x, "y": c.y]]
    }
}
