/*
 *  DisplayClient.swift - The library window's connection to one embedded VM: `RemoteGuestLink` is the `GuestLink` the
 *  guest display view uses when the VM is in another process. Input goes out as small binary messages the moment the
 *  view makes it (a non-blocking write: a stuck VM can never freeze the library window); what the guest does to the
 *  cursor, its size and the Sheep Shears tool comes back on a reader thread and is handed to the main thread.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Darwin

/// Reads events off the socket on its own thread and hands each batch to `deliver` (which hops to the main thread).
private final class DisplayEventReader: @unchecked Sendable {
    private let fd: Int32
    private let deliver: @Sendable ([DisplayEvent]) -> Void
    private let finished: @Sendable () -> Void

    init(fd: Int32, deliver: @escaping @Sendable ([DisplayEvent]) -> Void, finished: @escaping @Sendable () -> Void) {
        self.fd = fd
        self.deliver = deliver
        self.finished = finished
    }

    func start() {
        let thread = Thread { [self] in run() }
        thread.name = "sheepshaver.display.reader"
        thread.start()
    }

    private func run() {
        var buffer: [UInt8] = []
        var chunk = [UInt8](repeating: 0, count: 8192)
        loop: while true {
            let n = read(fd, &chunk, chunk.count)
            if n < 0 && errno == EINTR { continue }
            if n <= 0 { break }
            buffer.append(contentsOf: chunk[0..<n])
            var events: [DisplayEvent] = []
            do {
                try DisplayWire.take(&buffer) { kind, payload in
                    if let event = DisplayWire.decodeEvent(kind, payload) { events.append(event) }
                }
            } catch {
                break loop
            }
            if !events.isEmpty { deliver(events) }
        }
        finished()
    }
}

@MainActor
final class RemoteGuestLink: GuestLink {
    let vmID: String
    /// What the VM pushed: the guest cursor, whether the guest draws it, the edge-release distance, the tool's state.
    private(set) var cursor: [UInt8]?
    private(set) var hides = false
    private(set) var threshold = 0.0
    private(set) var hostFeatures: ShearsFeatures = .all
    private(set) var guestFeatures: ShearsFeatures = .all
    private(set) var toolRunning = false
    private(set) var toolVersion = 0
    private(set) var guestSize = CGSize(width: 1024, height: 768)
    private(set) var closed = false

    /// Every event after the link has cached what it needs; the display view and the library window react to it.
    var onEvent: (@MainActor (DisplayEvent) -> Void)?
    var onClosed: (@MainActor () -> Void)?

    private var fd: Int32
    private var prefs: PrefsDocument
    private let prefsPath: String

    /// nil when nothing is listening on the VM's display socket (it is not running, or not embedded, or not yet up).
    init?(vmID: String, prefsPath: String) {
        let fd = VMControlServer.connect(DisplayPaths.socketPath(vmID: vmID))
        guard fd >= 0 else { return nil }
        var one: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
        self.vmID = vmID
        self.fd = fd
        self.prefsPath = prefsPath
        self.prefs = PrefsDocument(text: (try? String(contentsOfFile: prefsPath, encoding: .utf8)) ?? "")
        nonisolated(unsafe) let link = self
        DisplayEventReader(fd: fd, deliver: { events in
            DispatchQueue.main.async { MainActor.assumeIsolated { link.handle(events) } }
        }, finished: {
            DispatchQueue.main.async { MainActor.assumeIsolated { link.connectionEnded() } }
        }).start()
    }

    /// Reads the VM's prefs again (after its settings were saved).
    func reloadPrefs() {
        prefs = PrefsDocument(text: (try? String(contentsOfFile: prefsPath, encoding: .utf8)) ?? "")
    }

    /// Leaves the VM. The reader thread sees the end of the stream and closes the descriptor.
    func close() {
        guard !closed else { return }
        closed = true
        shutdown(fd, SHUT_RDWR)
    }

    private func connectionEnded() {
        Darwin.close(fd)
        if !closed {
            closed = true
            onClosed?()
        }
    }

    private func handle(_ events: [DisplayEvent]) {
        for event in events {
            switch event {
            case .cursor(let image): cursor = image
            case .cursorHidesHost(let value): hides = value
            case .edgeThreshold(let milli): threshold = Double(milli) / 1000
            case .features(let host, let guest): hostFeatures = ShearsFeatures(guestWord: UInt32(host)); guestFeatures = ShearsFeatures(guestWord: UInt32(guest))
            case .shearsStatus(let running, let version): toolRunning = running; toolVersion = Int(version)
            case .guestMode(let w, let h, _, _): guestSize = CGSize(width: Int(w), height: Int(h))
            default: break
            }
            onEvent?(event)
        }
    }

    // MARK: GuestLink

    private func send(_ input: DisplayInput) {
        guard !closed else { return }
        let bytes = DisplayWire.encode(input)
        // Never block the interface: if the VM is not reading (stuck), the event is dropped
        bytes.withUnsafeBytes { _ = Darwin.send(fd, $0.baseAddress, bytes.count, MSG_DONTWAIT) }
    }

    func key(_ code: Int32, down: Bool) { send(.key(code: UInt8(truncatingIfNeeded: code), down: down)) }
    func mouseAbs(_ x: Int32, _ y: Int32) { send(.mouseAbs(x: Int16(clamping: x), y: Int16(clamping: y))) }
    func mouseMove(_ dx: Int32, _ dy: Int32) { send(.mouseMove(dx: Int16(clamping: dx), dy: Int16(clamping: dy))) }

    // What the VM was last told. The view releases buttons and switches modes whenever it grabs, lets go or is set up, even
    // when nothing changes, and for the guest every one of those is a real ADB input event (a button release nobody
    // pressed, a mode switch) that wakes the emulator out of its idle behaviour. A VM in its own window gets them before
    // the guest has started; a VM that is already booting must not be sent them at all: it ran about 6% slower for it.
    // So only changes go out. (A VM starts with no button down, an absolute mouse and no pointer reports wanted, and
    // returns to that when its last viewer goes away.)
    private var buttonsDown: [Bool] = [false, false]
    private var relativeMouse = false
    private var pointerWanted = false

    func button(_ number: Int32, down: Bool) {
        guard (0...1).contains(number), buttonsDown[Int(number)] != down else { return }
        buttonsDown[Int(number)] = down
        send(.button(number: UInt8(number), down: down))
    }

    func setRelativeMouse(_ on: Bool) {
        guard relativeMouse != on else { return }
        relativeMouse = on
        send(.setRelativeMouse(on))
    }

    func setPointerWanted(_ on: Bool) {
        guard pointerWanted != on else { return }
        pointerWanted = on
        send(.pointerWanted(on))
    }
    var cursorBytes: [UInt8]? { cursor }
    var cursorHidesHost: Bool { hides }
    var edgeReleaseThreshold: Double { threshold }
    func prefString(_ key: String) -> String { prefs.string(key) }
    func prefBool(_ key: String) -> Bool { prefs.bool(key) }
}
