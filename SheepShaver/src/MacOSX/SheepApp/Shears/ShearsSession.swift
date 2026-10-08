/*
 *  ShearsSession.swift - What the host knows about the guest tool: is it running, and where a shutdown request stands.
 *
 *  Pure state, no AppKit and no locking (ShearsHost wraps it in a lock), so it is unit tested on its own. The tool
 *  polls the host a few times a second, so "running" means "has called recently"; the host cannot reach into the
 *  guest to look. A shutdown is a request flagged in the poll replies until the tool acknowledges it.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum ShearsShutdownPhase: Equatable {
    case idle
    /// Flagged to the guest, not yet acknowledged.
    case requested(since: Double)
    /// The tool has asked the Finder to shut down. The guest may still be waiting on a "Save changes?" dialog.
    case sent(since: Double)
    /// The tool could not ask (a Mac OS error code).
    case failed(code: Int32)
}

struct ShearsToolStatus: Equatable {
    var toolRunning: Bool
    var toolVersion: UInt32
    var capabilities: UInt32
    var shutdown: ShearsShutdownPhase
}

struct ShearsSession {
    /// The tool wakes at least four times a second, but a foreground application that never yields can starve it,
    /// so a gap this long is needed before it counts as gone.
    static let liveWindow: Double = 5
    /// A second shutdown request is accepted this long after the first was sent (the Finder may have been busy).
    static let retryAfter: Double = 10

    private(set) var greeted = false
    private(set) var version: UInt32 = 0
    private(set) var capabilities: UInt32 = 0
    private(set) var lastCall: Double = -.infinity
    private(set) var shutdown: ShearsShutdownPhase = .idle

    func isRunning(now: Double) -> Bool {
        greeted && now - lastCall <= Self.liveWindow
    }

    func status(now: Double) -> ShearsToolStatus {
        ShearsToolStatus(toolRunning: isRunning(now: now), toolVersion: version, capabilities: capabilities, shutdown: shutdown)
    }

    /// Any valid call from the tool.
    mutating func called(now: Double) {
        lastCall = now
    }

    /// HELLO. Returns true when this is a new tool or its version or capabilities changed (worth a log line).
    @discardableResult
    mutating func hello(version: UInt32, capabilities: UInt32, now: Double) -> Bool {
        let changed = !greeted || self.version != version || self.capabilities != capabilities
        greeted = true
        self.version = version
        self.capabilities = capabilities
        lastCall = now
        return changed
    }

    /// Ask the guest to shut down. False when the tool is not running or a request is already in flight.
    mutating func requestShutdown(now: Double) -> Bool {
        guard isRunning(now: now) else { return false }
        switch shutdown {
        case .requested:
            return false
        case .sent(let since) where now - since < Self.retryAfter:
            return false
        default:
            shutdown = .requested(since: now)
            return true
        }
    }

    /// True while a request is waiting to be delivered: the poll replies carry the flag until it is acknowledged.
    var shutdownFlagged: Bool {
        if case .requested = shutdown { return true }
        return false
    }

    /// The tool's answer: 0 means it asked the Finder, anything else is the error it got.
    mutating func shutdownResult(_ code: Int32, now: Double) {
        guard case .requested = shutdown else { return }
        shutdown = code == 0 ? .sent(since: now) : .failed(code: code)
    }
}
