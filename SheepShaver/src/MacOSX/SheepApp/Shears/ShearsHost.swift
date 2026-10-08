/*
 *  ShearsHost.swift - Host side of Sheep Shears: answers the guest tool's mailbox calls.
 *
 *  The handler runs on the emulation thread, synchronously, while the guest waits for the reply, so it cannot be
 *  an actor method. State is behind a lock; anything that touches the window is handed to the main actor and the
 *  reply does not wait for it. Decoding and the pointer rule live in ShearsProtocol.swift and ShearsPointer.swift.
 *
 *  Prefs: `noshears true` turns the whole feature off (HELLO is answered `disabled`); `edgerelease <points>` sets
 *  the push needed to release at an edge (default 40, 0 turns edge release off).
 *
 *  `status` says whether the tool is running (it has called within ShearsSession.liveWindow seconds). The host can
 *  ask the guest to shut down with `requestShutdown()`: the request rides on the poll replies until the tool
 *  acknowledges it, and the tool then sends the Finder a shut-down Apple Event, so applications are asked to quit
 *  and save as they would for Special > Shut Down. When Mac OS finishes, the guest powers off through the PMU and
 *  the emulator quits (nw_pmu_host_power in main_unix.cpp).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

@_silgen_name("ClipboardSetBridgeEnabled")
private func ClipboardSetBridgeEnabled(_ on: Int32)

@_silgen_name("ShearsHostSetHandler")
private func ShearsHostSetHandler(
    _ handler: (@convention(c) (UnsafePointer<UInt8>?, UInt32, UnsafeMutablePointer<UInt8>?, UInt32) -> Int32)?
)

/// Called by the core on the emulation thread. A closure written inside the main-actor `install()` would inherit
/// main-actor isolation and trap when called from another thread, so this is a plain file-scope function.
private func shearsEntry(_ request: UnsafePointer<UInt8>?, _ requestLen: UInt32,
                         _ reply: UnsafeMutablePointer<UInt8>?, _ replyCap: UInt32) -> Int32 {
    guard let request, let reply, requestLen >= UInt32(Shears.headerSize) else { return -1 }
    let bytes = Array(UnsafeBufferPointer(start: request, count: Int(requestLen)))
    let out = ShearsHost.shared.handle(bytes)
    guard !out.isEmpty, out.count <= Int(replyCap) else { return -1 }
    out.withUnsafeBufferPointer { reply.update(from: $0.baseAddress!, count: out.count) }
    return Int32(out.count)
}

final class ShearsHost: @unchecked Sendable {
    static let shared = ShearsHost()

    private let lock = NSLock()
    private var session = ShearsSession()
    private var loggedBad = 0
    private var reports = 0
    private var pointerWanted = false
    /// What the guest asked for (control panel) and what the host menu allows; a feature runs when both agree.
    private var guestFeatures: ShearsFeatures = .all
    private var hostFeatures: ShearsFeatures = .all

    /// The pref values are read on the main actor when the guest starts and kept here.
    private var enabled = true
    private var edgeThreshold: Double = 40

    // Main actor only: transition logging and the test hook.
    private var statusTimer: Timer?
    private var lastReported: ShearsToolStatus?
    private var runningSince: Double?
    private var testShutdownAfter: Double?

    /// Register with the core. Call once the guest is starting; safe to call again.
    @MainActor
    func install() {
        let off = PrefsBridge.bool("noshears")
        let raw = PrefsBridge.string("edgerelease").trimmingCharacters(in: .whitespaces)
        let stored = UserDefaults.standard.object(forKey: ShearsHost.featuresKey) as? Int
        lock.lock()
        enabled = !off
        edgeThreshold = Double(raw) ?? 40
        hostFeatures = stored.map { ShearsFeatures(guestWord: UInt32(truncatingIfNeeded: $0)) } ?? .all
        lock.unlock()
        applyFeatures()
        // Measuring aid: pretend the pointer is grabbed so the tool polls fast (no mouse is needed to time it).
        if nwDiagnosticsOn, ProcessInfo.processInfo.environment["NW_SHEARS_FAST"] == "1" { setPointerWanted(true) }
        // Test hook (tools/shears/test.sh shutdown): request a shutdown this many seconds after the tool appears.
        if nwDiagnosticsOn, let raw = ProcessInfo.processInfo.environment["NW_SHEARS_SHUTDOWN_AFTER"] {
            testShutdownAfter = Double(raw)
        }
        ShearsHostSetHandler(shearsEntry)
        if statusTimer == nil {
            let timer = Timer(timeInterval: 1, repeats: true) { _ in
                MainActor.assumeIsolated { ShearsHost.shared.watchStatus() }
            }
            RunLoop.main.add(timer, forMode: .common)
            statusTimer = timer
        }
    }

    /// Once a second: log changes, tell the UI, and run the test hook.
    @MainActor
    private func watchStatus() {
        let now = ProcessInfo.processInfo.systemUptime
        let current = status
        if current.toolRunning {
            if runningSince == nil { runningSince = now }
        } else {
            runningSince = nil
        }
        if let after = testShutdownAfter, let since = runningSince, now - since >= after {
            testShutdownAfter = nil
            _ = requestShutdown()
        }
        guard current != lastReported else { return }
        if let previous = lastReported {
            if previous.toolRunning && !current.toolRunning {
                ShearsHost.say("Sheep Shears: the guest tool stopped responding")
            }
            if previous.shutdown != current.shutdown {
                switch current.shutdown {
                case .sent: ShearsHost.say("Sheep Shears: the guest was asked to shut down")
                case .failed(let code): ShearsHost.say("Sheep Shears: the guest could not start a shutdown (error \(code))")
                default: break
                }
            }
        }
        lastReported = current
        NotificationCenter.default.post(name: .shearsStatusChanged, object: nil)
    }

    private static func say(_ text: String) {
        fputs(text + "\n", stdout)
        fflush(stdout)
    }

    /// Whether the tool is running in the guest, its version, and where a shutdown request stands.
    var status: ShearsToolStatus {
        lock.lock(); defer { lock.unlock() }
        return session.status(now: ProcessInfo.processInfo.systemUptime)
    }

    /// Ask the guest to shut down properly. False when the tool is not running or a request is already in flight.
    @discardableResult
    func requestShutdown() -> Bool {
        lock.lock()
        let ok = enabled && session.requestShutdown(now: ProcessInfo.processInfo.systemUptime)
        lock.unlock()
        if ok { ShearsHost.say("Sheep Shears: shutdown requested") }
        return ok
    }

    var edgeReleaseThreshold: Double {
        lock.lock(); defer { lock.unlock() }
        return enabled && effectiveFeatures.contains(.edgeRelease) ? edgeThreshold : 0
    }

    private static let featuresKey = "SheepShearsHostFeatures"
    private var effectiveFeatures: ShearsFeatures { guestFeatures.intersection(hostFeatures) }

    /// What is in effect now: the guest's choice and the host's menu both have to allow it.
    var features: ShearsFeatures {
        lock.lock(); defer { lock.unlock() }
        return effectiveFeatures
    }

    /// What the guest's control panel allows (everything until the tool says otherwise).
    var guestSwitches: ShearsFeatures {
        lock.lock(); defer { lock.unlock() }
        return guestFeatures
    }

    /// The host-side switches (Guest menu), kept across launches.
    var hostSwitches: ShearsFeatures {
        get { lock.lock(); defer { lock.unlock() }; return hostFeatures }
        set {
            lock.lock(); hostFeatures = newValue; lock.unlock()
            UserDefaults.standard.set(Int(newValue.rawValue), forKey: ShearsHost.featuresKey)
            applyFeatures()
        }
    }

    /// Push the effective features to the parts that act on them, and say so when they changed.
    private var lastApplied: ShearsFeatures?
    private func applyFeatures() {
        lock.lock()
        let now = effectiveFeatures
        let changed = lastApplied != now
        lastApplied = now
        lock.unlock()
        ClipboardSetBridgeEnabled(now.contains(.clipboard) ? 1 : 0)
        if changed {
            ShearsHost.say("Sheep Shears: edge release \(now.contains(.edgeRelease) ? "on" : "off"), clipboard \(now.contains(.clipboard) ? "on" : "off")")
        }
    }

    /// The view says whether it is grabbed in absolute mode, i.e. whether the guest pointer position is wanted now.
    /// Only then does the tool wake up often; otherwise it polls slowly, because every guest wake-up costs host CPU.
    func setPointerWanted(_ wanted: Bool) {
        lock.lock(); pointerWanted = wanted; lock.unlock()
    }

    /// Ticks (1/60 s) the tool should sleep before its next call.
    private var nextPollTicks: UInt32 {
        lock.lock(); defer { lock.unlock() }
        return pointerWanted && enabled && edgeThreshold > 0 && effectiveFeatures.contains(.edgeRelease)
            ? ShearsHost.fastTicks : ShearsHost.idleTicks
    }

    private func pollPayload() -> [UInt8] {
        let ticks = nextPollTicks
        lock.lock()
        let flags = session.shutdownFlagged ? Shears.flagShutdownRequested : 0
        lock.unlock()
        var payload: [UInt8] = []
        ShearsCodec.put(&payload, ticks)
        ShearsCodec.put(&payload, flags)
        return payload
    }

    static let fastTicks: UInt32 = 2
    static let idleTicks: UInt32 = 15

    func handle(_ message: [UInt8]) -> [UInt8] {
        let request: ShearsRequest
        do {
            request = try ShearsCodec.decode(message)
        } catch {
            noteBad("rejected a malformed mailbox: \(error)")
            return []
        }
        lock.lock()
        let on = enabled
        if on { session.called(now: ProcessInfo.processInfo.systemUptime) }
        lock.unlock()
        func reply(_ status: ShearsStatus, _ payload: [UInt8] = []) -> [UInt8] {
            ShearsCodec.encodeReply(seq: request.seq, command: request.command, status: status, payload: payload)
        }
        guard on else { return reply(.disabled) }
        guard let command = ShearsCommand(rawValue: request.command) else { return reply(.unsupported) }

        switch command {
        case .hello:
            var r = ShearsReader(request.payload)
            guard let version = r.u32(), let caps = r.u32() else { return reply(.badRequest) }
            lock.lock()
            let first = session.hello(version: version, capabilities: caps, now: ProcessInfo.processInfo.systemUptime)
            lock.unlock()
            if first {
                ShearsHost.say("Sheep Shears: guest tool connected, version \(version), capabilities \(String(caps, radix: 16))")
            }
            var payload: [UInt8] = []
            ShearsCodec.put(&payload, Shears.version)
            ShearsCodec.put(&payload, Shears.capPointer | Shears.capShutdown)
            ShearsCodec.put(&payload, (edgeReleaseThreshold > 0 ? Shears.capPointer : 0) | Shears.capShutdown)
            lock.lock(); guestFeatures = .all; lock.unlock()    // a tool that starts says its settings next (SETTINGS)
            applyFeatures()
            ShearsCodec.put(&payload, nextPollTicks)
            return reply(.ok, payload)

        case .poll:
            return reply(.ok, pollPayload())

        case .log:
            if nwDiagnosticsOn {
                let text = String(decoding: request.payload.prefix(200).filter { $0 >= 0x20 && $0 < 0x7f }, as: UTF8.self)
                fputs("SHEARS-GUEST: \(text)\n", stdout)
                fflush(stdout)
            }
            return reply(.ok)

        case .sysResult:
            var r = ShearsReader(request.payload)
            guard let action = r.u32(), let code = r.i32() else { return reply(.badRequest) }
            if action == Shears.actionShutdown {
                lock.lock()
                session.shutdownResult(code, now: ProcessInfo.processInfo.systemUptime)
                lock.unlock()
            }
            return reply(.ok)

        case .settings:
            var r = ShearsReader(request.payload)
            guard let word = r.u32() else { return reply(.badRequest) }
            lock.lock(); guestFeatures = ShearsFeatures(guestWord: word); lock.unlock()
            applyFeatures()
            var payload: [UInt8] = []
            ShearsCodec.put(&payload, features.rawValue)
            return reply(.ok, payload)

        case .ptrPos:
            guard let report = ShearsPointerReport(payload: request.payload) else { return reply(.badRequest) }
            if nwDiagnosticsOn {
                lock.lock(); reports += 1; let n = reports; lock.unlock()
                if n <= 3 || n % 500 == 0 {
                    fputs("SHEARS: PTR_POS #\(n) x=\(report.x) y=\(report.y) screen=\(report.width)x\(report.height) buttons=\(report.buttons)\n", stdout)
                    fflush(stdout)
                }
            }
            ShearsHost.deliver(report)
            return reply(.ok, pollPayload())
        }
    }

    private func noteBad(_ text: String) {
        lock.lock()
        loggedBad += 1
        let n = loggedBad
        lock.unlock()
        if n <= 5 {
            fputs("Sheep Shears: \(text)\n", stdout)
            fflush(stdout)
        }
    }

    private static func deliver(_ report: ShearsPointerReport) {
        let now = ProcessInfo.processInfo.systemUptime
        DispatchQueue.main.async {
            MainActor.assumeIsolated {
                SheepHost.guestPointerReported(report, at: now)
            }
        }
    }
}

extension Notification.Name {
    /// Posted on the main thread when ShearsHost.status changes (checked once a second).
    static let shearsStatusChanged = Notification.Name("ShearsStatusChanged")
}
