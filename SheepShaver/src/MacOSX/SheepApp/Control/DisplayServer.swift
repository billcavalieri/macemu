/*
 *  DisplayServer.swift - The VM process's side of showing its picture in the library window (another process).
 *
 *  The picture itself is in shared memory (display_shm.h, written by sheepforce_metal.mm). This server carries the rest
 *  over a Unix socket, /tmp/sheepshaver-<uid>/<vm id>.disp (see DisplayProtocol.swift): keyboard and mouse input coming
 *  in, and what the guest does to the cursor, its size and the Sheep Shears tool going out. A viewer that connects late
 *  is first told the current state (mode, cursor, arrow, tool status).
 *
 *  Input is applied on the main thread through the same VideoHost* entry points the VM's own window uses, because the
 *  emulator's ADB input rings have exactly one producer. The messages arrive the moment the viewer sends them; this
 *  side adds no delay and does no batching beyond the messages that arrived together.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Darwin

@_silgen_name("VideoHostKey")
private func VideoHostKey(_ code: Int32, _ down: Int32)
@_silgen_name("VideoHostMouseAbs")
private func VideoHostMouseAbs(_ x: Int32, _ y: Int32)
@_silgen_name("VideoHostMouseMove")
private func VideoHostMouseMove(_ dx: Int32, _ dy: Int32)
@_silgen_name("VideoHostMouseButton")
private func VideoHostMouseButton(_ button: Int32, _ down: Int32)
@_silgen_name("VideoHostSetRelMouse")
private func VideoHostSetRelMouse(_ on: Int32)
@_silgen_name("VideoHostCursorBytes")
private func VideoHostCursorBytes() -> UnsafePointer<UInt8>?
@_silgen_name("VideoGuestCursorHidesHost")
private func VideoGuestCursorHidesHost() -> Int32
@_silgen_name("VideoHostGuestScreen")
private func VideoHostGuestScreen(_ w: UnsafeMutablePointer<Int32>, _ h: UnsafeMutablePointer<Int32>, _ depth: UnsafeMutablePointer<Int32>)

final class DisplayServer: @unchecked Sendable {
    static let shared = DisplayServer()

    private let lock = NSLock()
    private var listener: Int32 = -1
    private var path = ""
    private var clients: [Int32] = []
    private let writer = DispatchQueue(label: "sheepshaver.display.write")
    /// The last message of each state-carrying kind, replayed to a viewer that connects later.
    private var sticky: [UInt8: [UInt8]] = [:]
    private var problems: [[UInt8]] = []
    private var generation: UInt32 = 0
    private var lastHides: Bool?
    private var lastThreshold: UInt32?
    private var lastFeatures: UInt16?
    private var hidesTimer: DispatchSourceTimer?

    // MARK: start

    func start(vmID: String) {
        lock.lock()
        let already = listener >= 0
        lock.unlock()
        guard !already else { return }
        let socketPath = DisplayPaths.socketPath(vmID: vmID)
        let directory = (socketPath as NSString).deletingLastPathComponent
        try? FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
        chmod(directory, 0o700)
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
        guard bound == 0, listen(fd, 4) == 0 else { close(fd); return }
        _ = fcntl(fd, F_SETFD, FD_CLOEXEC)              // a restarted guest re-executes this process: nothing may leak into it
        chmod(socketPath, 0o600)
        lock.lock()
        listener = fd
        path = socketPath
        lock.unlock()
        atexit { DisplayServer.shared.removeSocket() }
        let thread = Thread { [self] in acceptLoop() }
        thread.name = "sheepshaver.display.accept"
        thread.start()
        // The guest turns its software cursor on after boot; there is no callback for it, so look a few times a second
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .utility))
        timer.schedule(deadline: .now() + 0.2, repeating: 0.2)
        timer.setEventHandler { [self] in checkCursorHides() }
        timer.resume()
        hidesTimer = timer
        NotificationCenter.default.addObserver(forName: .shearsStatusChanged, object: nil, queue: nil) { [self] _ in
            let status = ShearsHost.shared.status
            emit(.shearsStatus(toolRunning: status.toolRunning, version: UInt8(truncatingIfNeeded: status.toolVersion)))
        }
        if nwDiagnosticsOn {
            fputs("Display: listening on \(socketPath)\n", stdout)
            fflush(stdout)
        }
    }

    fileprivate func removeSocket() {
        lock.lock(); let p = path; lock.unlock()
        if !p.isEmpty { unlink(p) }
    }

    // MARK: what the guest does (called from the emulator's callbacks, any thread)

    func guestModeChanged(width: Int, height: Int) {
        var w: Int32 = 0, h: Int32 = 0, depth: Int32 = 32
        VideoHostGuestScreen(&w, &h, &depth)
        lock.lock()
        generation &+= 1
        let g = generation
        lock.unlock()
        emit(.guestMode(width: UInt16(clamping: width), height: UInt16(clamping: height), depth: UInt8(clamping: Int(depth)), generation: g))
    }

    func cursorChanged() {
        guard let raw = VideoHostCursorBytes() else { return }
        emit(.cursor(image: Array(UnsafeBufferPointer(start: raw, count: DisplayWire.cursorBytes))))
        checkCursorHides()
    }

    func arrowMoved(x: Int, y: Int, visible: Bool) {
        emit(.arrow(x: Int16(clamping: x), y: Int16(clamping: y), visible: visible))
    }

    func shearsPointer(x: Int, y: Int, width: Int, height: Int, buttons: UInt32) {
        emit(.shearsPointer(x: Int16(clamping: x), y: Int16(clamping: y), width: UInt16(clamping: width), height: UInt16(clamping: height), buttons: buttons))
    }

    func problem(_ text: String) {
        emit(.problem(text: text))
    }

    private func checkCursorHides() {
        let hides = VideoGuestCursorHidesHost() != 0
        let threshold = UInt32(max(0, min(ShearsHost.shared.edgeReleaseThreshold * 1000, 4_000_000)))
        lock.lock()
        let changed = lastHides != hides
        lastHides = hides
        let thresholdChanged = lastThreshold != threshold
        lastThreshold = threshold
        lock.unlock()
        if changed { emit(.cursorHidesHost(hides)) }
        if thresholdChanged { emit(.edgeThreshold(milli: threshold)) }
        let host = UInt8(truncatingIfNeeded: ShearsHost.shared.hostSwitches.rawValue), guest = UInt8(truncatingIfNeeded: ShearsHost.shared.guestSwitches.rawValue)
        let packed = UInt16(host) << 8 | UInt16(guest)
        lock.lock()
        let featuresChanged = lastFeatures != packed
        lastFeatures = packed
        lock.unlock()
        if featuresChanged { emit(.features(host: host, guest: guest)) }
    }

    // MARK: sending

    func emit(_ event: DisplayEvent) {
        let bytes = DisplayWire.encode(event)
        lock.lock()
        switch event {
        case .shearsPointer: break                              // a stream, not state
        case .problem: problems.append(bytes)
        default: sticky[bytes[1]] = bytes
        }
        let targets = clients
        lock.unlock()
        guard !targets.isEmpty else { return }
        writer.async { [self] in
            for fd in targets where !Self.writeAll(fd, bytes) { drop(fd) }
        }
    }

    private static func writeAll(_ fd: Int32, _ bytes: [UInt8]) -> Bool {
        var sent = 0
        while sent < bytes.count {
            let n = bytes.withUnsafeBytes { write(fd, $0.baseAddress!.advanced(by: sent), bytes.count - sent) }
            if n < 0 && errno == EINTR { continue }
            if n <= 0 { return false }
            sent += n
        }
        return true
    }

    private func drop(_ fd: Int32) {
        lock.lock()
        clients.removeAll { $0 == fd }
        let none = clients.isEmpty
        lock.unlock()
        close(fd)
        if none { viewerGone() }
    }

    // MARK: viewers

    private func acceptLoop() {
        while true {
            let client = accept(listener, nil, nil)
            if client < 0 {
                if errno == EINTR { continue }
                return
            }
            var one: Int32 = 1
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
            _ = fcntl(client, F_SETFD, FD_CLOEXEC)
            let thread = Thread { [self] in serve(client) }
            thread.name = "sheepshaver.display.client"
            thread.start()
        }
    }

    private func serve(_ client: Int32) {
        // Tell the new viewer where things stand, then let it join the stream (under the lock, so nothing is missed)
        lock.lock()
        let replay = sticky.values.flatMap { $0 } + problems.flatMap { $0 }
        problems.removeAll()
        clients.append(client)
        lock.unlock()
        writer.async { [self] in
            if !replay.isEmpty, !Self.writeAll(client, replay) { drop(client) }
        }
        var buffer: [UInt8] = []
        var chunk = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = read(client, &chunk, chunk.count)
            if n < 0 && errno == EINTR { continue }
            if n <= 0 { break }
            buffer.append(contentsOf: chunk[0..<n])
            var batch: [DisplayInput] = []
            do {
                try DisplayWire.take(&buffer) { kind, payload in
                    if let input = DisplayWire.decodeInput(kind, payload) { batch.append(input) }
                }
            } catch {
                break                                           // a damaged stream: drop the viewer
            }
            if buffer.count > 4096 { break }
            if !batch.isEmpty { apply(batch) }
        }
        drop(client)
    }

    /// Posts the messages that arrived together, in order, from the main thread (the ADB rings have one producer).
    private func apply(_ inputs: [DisplayInput]) {
        DispatchQueue.main.async {
            for input in inputs {
                switch input {
                case .key(let code, let down): VideoHostKey(Int32(code), down ? 1 : 0)
                case .mouseMove(let dx, let dy): VideoHostMouseMove(Int32(dx), Int32(dy))
                case .mouseAbs(let x, let y): VideoHostMouseAbs(Int32(x), Int32(y))
                case .button(let number, let down): VideoHostMouseButton(Int32(number), down ? 1 : 0)
                case .setRelativeMouse(let on): VideoHostSetRelMouse(on ? 1 : 0)
                case .pointerWanted(let on): ShearsHost.shared.setPointerWanted(on)
                }
            }
        }
    }

    /// The last viewer is gone (closed, crashed or detached): leave the guest as the window's own release would, with no
    /// button held and the pointer in absolute mode.
    private func viewerGone() {
        DispatchQueue.main.async {
            VideoHostMouseButton(0, 0)
            VideoHostMouseButton(1, 0)
            VideoHostSetRelMouse(0)
            ShearsHost.shared.setPointerWanted(false)
        }
    }
}
