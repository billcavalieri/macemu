/*
 *  DisplayProtocol.swift - The binary messages between the library window (which shows a VM's picture and takes the
 *  keyboard and mouse) and the VM process (which owns the emulator). Pure: no sockets, no AppKit, so it is unit-tested.
 *
 *  Every message is  [length: UInt8][kind: UInt8][payload...]  (length counts kind + payload, so 1...255). Numbers are
 *  little-endian. Input messages are small (3 to 10 bytes) because they are sent the moment AppKit delivers an event;
 *  there is no batching delay. The picture itself does not travel here: it is in shared memory (see sheepforce_remote.mm).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

/// Library window -> VM process.
enum DisplayInput: Equatable, Sendable {
    case key(code: UInt8, down: Bool)                   // ADB key code
    case mouseMove(dx: Int16, dy: Int16)                // relative, the guest's own units
    case mouseAbs(x: Int16, y: Int16)                   // a position in guest pixels (the grab)
    case button(number: UInt8, down: Bool)              // 0 = left, 1 = right
    case setRelativeMouse(Bool)
    case pointerWanted(Bool)                            // the guest tool should report the pointer position
}

/// VM process -> library window.
enum DisplayEvent: Equatable, Sendable {
    case guestMode(width: UInt16, height: UInt16, depth: UInt8, generation: UInt32)
    case cursor(image: [UInt8])                         // the 16x16 Mac cursor as VideoHostCursorBytes gives it (68 bytes)
    case cursorHidesHost(Bool)
    case arrow(x: Int16, y: Int16, visible: Bool)       // the guest arrow position (drawn over the picture when grabbed)
    case shearsPointer(x: Int16, y: Int16, width: UInt16, height: UInt16, buttons: UInt32)
    case shearsStatus(toolRunning: Bool, version: UInt8)
    case displayMode(inOwnWindow: Bool)                 // the VM opened its own window (true) or is back in the library window
    case features(host: UInt8, guest: UInt8)            // Sheep Shears switches: the host's menu and the guest's control panel (bit 0 edge release, bit 1 clipboard)
    case edgeThreshold(milli: UInt32)                   // the pointer distance (in guest pixels x 1000) that releases at an edge; 0 = off
    case problem(text: String)
}

enum DisplayWire {
    static let cursorBytes = 68
    static let maxProblemText = 200

    private enum Kind {
        static let key: UInt8 = 1, mouseMove: UInt8 = 2, mouseAbs: UInt8 = 3, button: UInt8 = 4
        static let setRelative: UInt8 = 5, pointerWanted: UInt8 = 6
        static let guestMode: UInt8 = 0x81, cursor: UInt8 = 0x82, cursorHides: UInt8 = 0x83, arrow: UInt8 = 0x84
        static let shearsPointer: UInt8 = 0x85, shearsStatus: UInt8 = 0x86, problem: UInt8 = 0x87, edgeThreshold: UInt8 = 0x88, features: UInt8 = 0x89, displayMode: UInt8 = 0x8a
    }

    // MARK: encoding

    private static func frame(_ kind: UInt8, _ payload: [UInt8]) -> [UInt8] {
        precondition(payload.count + 1 <= 255)
        return [UInt8(payload.count + 1), kind] + payload
    }

    private static func le16(_ v: Int16) -> [UInt8] { let u = UInt16(bitPattern: v); return [UInt8(u & 255), UInt8(u >> 8)] }
    private static func le16(_ v: UInt16) -> [UInt8] { [UInt8(v & 255), UInt8(v >> 8)] }
    private static func le32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8((v >> (8 * UInt32($0))) & 255) } }

    static func encode(_ input: DisplayInput) -> [UInt8] {
        switch input {
        case .key(let code, let down): return frame(Kind.key, [code, down ? 1 : 0])
        case .mouseMove(let dx, let dy): return frame(Kind.mouseMove, le16(dx) + le16(dy))
        case .mouseAbs(let x, let y): return frame(Kind.mouseAbs, le16(x) + le16(y))
        case .button(let n, let down): return frame(Kind.button, [n, down ? 1 : 0])
        case .setRelativeMouse(let on): return frame(Kind.setRelative, [on ? 1 : 0])
        case .pointerWanted(let on): return frame(Kind.pointerWanted, [on ? 1 : 0])
        }
    }

    static func encode(_ event: DisplayEvent) -> [UInt8] {
        switch event {
        case .guestMode(let w, let h, let depth, let generation):
            return frame(Kind.guestMode, le16(w) + le16(h) + [depth] + le32(generation))
        case .cursor(let image):
            precondition(image.count == cursorBytes)
            return frame(Kind.cursor, image)
        case .cursorHidesHost(let hides): return frame(Kind.cursorHides, [hides ? 1 : 0])
        case .arrow(let x, let y, let visible): return frame(Kind.arrow, le16(x) + le16(y) + [visible ? 1 : 0])
        case .shearsPointer(let x, let y, let w, let h, let buttons): return frame(Kind.shearsPointer, le16(x) + le16(y) + le16(w) + le16(h) + le32(buttons))
        case .shearsStatus(let running, let version): return frame(Kind.shearsStatus, [running ? 1 : 0, version])
        case .edgeThreshold(let milli): return frame(Kind.edgeThreshold, le32(milli))
        case .features(let host, let guest): return frame(Kind.features, [host, guest])
        case .displayMode(let own): return frame(Kind.displayMode, [own ? 1 : 0])
        case .problem(let text):
            var bytes = Array(text.utf8)
            if bytes.count > maxProblemText { bytes = Array(bytes.prefix(maxProblemText)) }
            return frame(Kind.problem, bytes)
        }
    }

    // MARK: decoding

    /// Takes whole messages off the front of `buffer` (left with any partial message) and hands each to `body` as
    /// (kind, payload). A zero length byte is a protocol error: the stream cannot be trusted after it, so it
    /// throws `DisplayWireError.badLength` and the connection should be closed.
    static func take(_ buffer: inout [UInt8], _ body: (UInt8, ArraySlice<UInt8>) -> Void) throws {
        var offset = 0
        while offset < buffer.count {
            let length = Int(buffer[offset])
            if length == 0 { throw DisplayWireError.badLength }
            if offset + 1 + length > buffer.count { break }
            body(buffer[offset + 1], buffer[(offset + 2)..<(offset + 1 + length)])
            offset += 1 + length
        }
        buffer.removeFirst(offset)
    }

    private static func s16(_ p: ArraySlice<UInt8>, _ at: Int) -> Int16 {
        Int16(bitPattern: UInt16(p[p.startIndex + at]) | UInt16(p[p.startIndex + at + 1]) << 8)
    }
    private static func u16(_ p: ArraySlice<UInt8>, _ at: Int) -> UInt16 {
        UInt16(p[p.startIndex + at]) | UInt16(p[p.startIndex + at + 1]) << 8
    }

    /// An input message, or nil for an unknown kind or a payload of the wrong size (ignored, never trusted).
    static func decodeInput(_ kind: UInt8, _ p: ArraySlice<UInt8>) -> DisplayInput? {
        switch (kind, p.count) {
        case (Kind.key, 2): return .key(code: p[p.startIndex] & 0x7f, down: p[p.startIndex + 1] != 0)
        case (Kind.mouseMove, 4): return .mouseMove(dx: s16(p, 0), dy: s16(p, 2))
        case (Kind.mouseAbs, 4): return .mouseAbs(x: s16(p, 0), y: s16(p, 2))
        case (Kind.button, 2): return p[p.startIndex] <= 1 ? .button(number: p[p.startIndex], down: p[p.startIndex + 1] != 0) : nil
        case (Kind.setRelative, 1): return .setRelativeMouse(p[p.startIndex] != 0)
        case (Kind.pointerWanted, 1): return .pointerWanted(p[p.startIndex] != 0)
        default: return nil
        }
    }

    static func decodeEvent(_ kind: UInt8, _ p: ArraySlice<UInt8>) -> DisplayEvent? {
        switch (kind, p.count) {
        case (Kind.guestMode, 9):
            let generation = UInt32(p[p.startIndex + 5]) | UInt32(p[p.startIndex + 6]) << 8
                | UInt32(p[p.startIndex + 7]) << 16 | UInt32(p[p.startIndex + 8]) << 24
            return .guestMode(width: u16(p, 0), height: u16(p, 2), depth: p[p.startIndex + 4], generation: generation)
        case (Kind.cursor, cursorBytes): return .cursor(image: Array(p))
        case (Kind.cursorHides, 1): return .cursorHidesHost(p[p.startIndex] != 0)
        case (Kind.arrow, 5): return .arrow(x: s16(p, 0), y: s16(p, 2), visible: p[p.startIndex + 4] != 0)
        case (Kind.shearsPointer, 12):
            let buttons = UInt32(p[p.startIndex + 8]) | UInt32(p[p.startIndex + 9]) << 8 | UInt32(p[p.startIndex + 10]) << 16 | UInt32(p[p.startIndex + 11]) << 24
            return .shearsPointer(x: s16(p, 0), y: s16(p, 2), width: u16(p, 4), height: u16(p, 6), buttons: buttons)
        case (Kind.shearsStatus, 2): return .shearsStatus(toolRunning: p[p.startIndex] != 0, version: p[p.startIndex + 1])
        case (Kind.displayMode, 1): return .displayMode(inOwnWindow: p[p.startIndex] != 0)
        case (Kind.features, 2): return .features(host: p[p.startIndex], guest: p[p.startIndex + 1])
        case (Kind.edgeThreshold, 4):
            let v = UInt32(p[p.startIndex]) | UInt32(p[p.startIndex + 1]) << 8 | UInt32(p[p.startIndex + 2]) << 16 | UInt32(p[p.startIndex + 3]) << 24
            return .edgeThreshold(milli: v)
        case (Kind.problem, 0...maxProblemText): return .problem(text: String(decoding: p, as: UTF8.self))
        default: return nil
        }
    }
}

enum DisplayWireError: Error { case badLength }

/// Where the shared frame memory and the display socket of a VM live (the same short per-user folder as the control socket).
enum DisplayPaths {
    static func socketPath(vmID: String, uid: uid_t = getuid()) -> String {
        ControlProtocol.socketPath(vmID: vmID, uid: uid).replacingOccurrences(of: ".sock", with: ".disp")
    }
    /// POSIX shared memory names are short and flat: "/sheep.<uid>.<id≤12 chars>".
    static func sharedMemoryName(vmID: String, uid: uid_t = getuid()) -> String {
        let safe = String(vmID.filter { $0.isLetter || $0.isNumber || $0 == "-" }.prefix(12))
        return "/sheep.\(uid).\(safe.isEmpty ? "vm" : safe)"
    }
}
