/*
 *  ShearsProtocol.swift - Sheep Shears mailbox format (see include/shears.h and guest/sheepshears/).
 *
 *  Pure value types and bounds-checked decoding, no AppKit and no emulator: this file is compiled on its own by
 *  tools/shears/test.sh unit. Every number comes from the guest and is untrusted.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum Shears {
    static let magic: UInt32 = 0x5348_5253          // 'SHRS'
    static let version: UInt32 = 1
    static let mailboxSize = 4096
    static let headerSize = 32
    static let maxPayload = mailboxSize - headerSize
    /// Capability bits in HELLO. A host feature is available when both sides have its bit.
    static let capPointer: UInt32 = 1 << 0
    static let capShutdown: UInt32 = 1 << 1
    /// Flag bits in the second word of POLL and PTR_POS replies (the first is the poll interval in ticks).
    /// Bit 0 is reserved for clipboard change notification.
    static let flagShutdownRequested: UInt32 = 1 << 1
    /// `action` values in SYS_RESULT.
    static let actionShutdown: UInt32 = 1
}

/// What the user can switch on or off. Everything is on unless something says otherwise.
struct ShearsFeatures: OptionSet, Equatable {
    let rawValue: UInt32
    static let edgeRelease = ShearsFeatures(rawValue: 1 << 0)
    static let clipboard = ShearsFeatures(rawValue: 1 << 1)
    static let all: ShearsFeatures = [.edgeRelease, .clipboard]

    /// From a word the guest sent: unknown bits are dropped, so a newer tool cannot turn on what this host lacks.
    init(guestWord: UInt32) { rawValue = guestWord & ShearsFeatures.all.rawValue }
    init(rawValue: UInt32) { self.rawValue = rawValue }
}

enum ShearsCommand: UInt32 {
    case hello = 1
    case poll = 2
    case log = 3
    case ptrPos = 4
    /// The tool reports the outcome of something the host asked for: payload is action, then a Mac OS error code.
    case sysResult = 5
    /// The guest's settings (control panel or the tool reading its preferences file): payload is a ShearsFeatures
    /// word, the features the user left on. The reply carries the word that is now in effect.
    case settings = 6
}

enum ShearsStatus: UInt32 {
    case ok = 0
    case unsupported = 1
    case badRequest = 2
    case disabled = 3
}

enum ShearsDecodeError: Error, Equatable {
    case tooShort
    case badMagic
    case badVersion
    case lengthMismatch
}

struct ShearsRequest: Equatable {
    var seq: UInt32
    var command: UInt32
    var payload: [UInt8]
}

/// Reads big-endian words from a payload. A read past the end returns nil, never traps.
struct ShearsReader {
    private let bytes: [UInt8]
    private var pos = 0

    init(_ bytes: [UInt8]) { self.bytes = bytes }

    var remaining: Int { bytes.count - pos }

    mutating func u32() -> UInt32? {
        guard remaining >= 4 else { return nil }
        let v = bytes[pos..<pos + 4].reduce(UInt32(0)) { $0 << 8 | UInt32($1) }
        pos += 4
        return v
    }

    mutating func i32() -> Int32? {
        u32().map { Int32(bitPattern: $0) }
    }

    mutating func rest() -> [UInt8] {
        defer { pos = bytes.count }
        return Array(bytes[pos...])
    }
}

enum ShearsCodec {
    static func decode(_ message: [UInt8]) throws -> ShearsRequest {
        guard message.count >= Shears.headerSize else { throw ShearsDecodeError.tooShort }
        var r = ShearsReader(message)
        guard r.u32() == Shears.magic else { throw ShearsDecodeError.badMagic }
        guard let version = r.u32(), version >= 1 else { throw ShearsDecodeError.badVersion }
        let seq = r.u32()!
        let command = r.u32()!
        _ = r.u32()                                  // status, written by the host
        let length = r.u32()!
        guard length <= UInt32(Shears.maxPayload),
              message.count == Shears.headerSize + Int(length) else { throw ShearsDecodeError.lengthMismatch }
        _ = r.u32(); _ = r.u32()                     // reserved
        return ShearsRequest(seq: seq, command: command, payload: r.rest())
    }

    static func put(_ out: inout [UInt8], _ v: UInt32) {
        out.append(UInt8(truncatingIfNeeded: v >> 24))
        out.append(UInt8(truncatingIfNeeded: v >> 16))
        out.append(UInt8(truncatingIfNeeded: v >> 8))
        out.append(UInt8(truncatingIfNeeded: v))
    }

    /// A whole reply image: header (host version, echoed seq and command) plus payload.
    static func encodeReply(seq: UInt32, command: UInt32, status: ShearsStatus, payload: [UInt8] = []) -> [UInt8] {
        let body = Array(payload.prefix(Shears.maxPayload))
        var out: [UInt8] = []
        out.reserveCapacity(Shears.headerSize + body.count)
        put(&out, Shears.magic)
        put(&out, Shears.version)
        put(&out, seq)
        put(&out, command)
        put(&out, status.rawValue)
        put(&out, UInt32(body.count))
        put(&out, 0)
        put(&out, 0)
        out.append(contentsOf: body)
        return out
    }
}

/// PTR_POS: where the guest says its pointer is, in guest pixels, and the size of the main screen.
struct ShearsPointerReport: Equatable {
    var x: Int
    var y: Int
    var width: Int
    var height: Int
    var buttons: UInt32

    var buttonDown: Bool { buttons & 1 != 0 }

    /// Nil when the payload is short or the screen size is not plausible. The position itself may be anywhere:
    /// the detector treats anything beyond the screen as being on that edge.
    init?(payload: [UInt8]) {
        var r = ShearsReader(payload)
        guard let x = r.i32(), let y = r.i32(), let w = r.i32(), let h = r.i32(), let b = r.u32() else { return nil }
        guard (1...16384).contains(Int(w)), (1...16384).contains(Int(h)) else { return nil }
        self.x = Int(x)
        self.y = Int(y)
        self.width = Int(w)
        self.height = Int(h)
        self.buttons = b
    }

    init(x: Int, y: Int, width: Int, height: Int, buttons: UInt32 = 0) {
        self.x = x; self.y = y; self.width = width; self.height = height; self.buttons = buttons
    }
}
