// Unit tests for ShearsProtocol.swift and ShearsPointer.swift. Built by tools/shears/test.sh unit.
import Foundation

var failures = 0
var checks = 0
func check(_ ok: Bool, _ what: String, line: Int = #line) {
    checks += 1
    if !ok { failures += 1; print("FAIL (line \(line)): \(what)") }
}

func be(_ v: UInt32) -> [UInt8] { var o: [UInt8] = []; ShearsCodec.put(&o, v); return o }
func message(magic: UInt32 = Shears.magic, version: UInt32 = 1, seq: UInt32 = 7, command: UInt32 = 4,
             length: UInt32? = nil, payload: [UInt8] = []) -> [UInt8] {
    be(magic) + be(version) + be(seq) + be(command) + be(0) + be(length ?? UInt32(payload.count)) + be(0) + be(0) + payload
}
func ptrPayload(_ x: Int32, _ y: Int32, _ w: Int32, _ h: Int32, _ b: UInt32 = 0) -> [UInt8] {
    be(UInt32(bitPattern: x)) + be(UInt32(bitPattern: y)) + be(UInt32(bitPattern: w)) + be(UInt32(bitPattern: h)) + be(b)
}

// MARK: protocol

do {
    let ok = try ShearsCodec.decode(message(payload: [1, 2, 3, 4]))
    check(ok.seq == 7 && ok.command == 4 && ok.payload == [1, 2, 3, 4], "valid message decodes")
} catch { check(false, "valid message threw \(error)") }

func decodeError(_ m: [UInt8]) -> ShearsDecodeError? {
    do { _ = try ShearsCodec.decode(m); return nil } catch { return error as? ShearsDecodeError }
}
check(decodeError([]) == .tooShort, "empty is too short")
check(decodeError(Array(message().prefix(31))) == .tooShort, "31 bytes is too short")
check(decodeError(message(magic: 0x1234_5678)) == .badMagic, "bad magic")
check(decodeError(message(version: 0)) == .badVersion, "version 0 refused")
check((try? ShearsCodec.decode(message(version: 9))) != nil, "a newer guest version is accepted (capabilities decide)")
check(decodeError(message(length: 8, payload: [1, 2, 3, 4])) == .lengthMismatch, "length longer than the data")
check(decodeError(message(length: 2, payload: [1, 2, 3, 4])) == .lengthMismatch, "length shorter than the data")
check(decodeError(message(length: UInt32(Shears.maxPayload) + 4, payload: [UInt8](repeating: 0, count: Shears.maxPayload + 4))) == .lengthMismatch, "payload over the maximum")
check(decodeError(message(length: 0xFFFF_FFFF)) == .lengthMismatch, "huge length")
check((try? ShearsCodec.decode(message(payload: [UInt8](repeating: 0, count: Shears.maxPayload)))) != nil, "maximum payload is accepted")

let reply = ShearsCodec.encodeReply(seq: 9, command: 4, status: .ok, payload: [0xAA, 0xBB, 0xCC, 0xDD])
check(reply.count == Shears.headerSize + 4, "reply size")
check(Array(reply[0..<4]) == be(Shears.magic) && Array(reply[8..<12]) == be(9) && Array(reply[12..<16]) == be(4), "reply header echoes seq and command")
check(Array(reply[16..<20]) == be(0) && Array(reply[20..<24]) == be(4), "reply status and length")
let big = ShearsCodec.encodeReply(seq: 1, command: 1, status: .ok, payload: [UInt8](repeating: 1, count: 99999))
check(big.count == Shears.mailboxSize, "reply payload is capped to the mailbox")

var rd = ShearsReader([0, 0, 0, 5, 0xFF, 0xFF, 0xFF, 0xFE, 1, 2])
check(rd.u32() == 5, "reader u32")
check(rd.i32() == -2, "reader i32 negative")
check(rd.u32() == nil, "reader short read is nil, not a trap")
check(rd.rest() == [1, 2], "reader rest")

check(ShearsPointerReport(payload: ptrPayload(10, 20, 1024, 768, 1)) == ShearsPointerReport(x: 10, y: 20, width: 1024, height: 768, buttons: 1), "PTR_POS decodes")
check(ShearsPointerReport(payload: ptrPayload(-5, 900, 1024, 768))?.x == -5, "negative coordinates are kept")
check(ShearsPointerReport(payload: Array(ptrPayload(1, 2, 3, 4).prefix(19))) == nil, "short PTR_POS refused")
check(ShearsPointerReport(payload: ptrPayload(1, 2, 0, 768)) == nil, "zero width refused")
check(ShearsPointerReport(payload: ptrPayload(1, 2, 1024, -1)) == nil, "negative height refused")
check(ShearsPointerReport(payload: ptrPayload(1, 2, 20000, 768)) == nil, "absurd width refused")
check(ShearsPointerReport(payload: ptrPayload(1, 2, 16384, 16384)) != nil, "largest plausible screen accepted")

// MARK: pointer rule

let W = 1024, H = 768
func at(_ x: Int, _ y: Int, buttons: UInt32 = 0) -> ShearsPointerReport { ShearsPointerReport(x: x, y: y, width: W, height: H, buttons: buttons) }

check(ShearsEdgeDetector.edges(at: at(0, 300)) == [.left], "x = 0 is the left edge")
check(ShearsEdgeDetector.edges(at: at(W - 1, 300)) == [.right], "x = w-1 is the right edge")
check(ShearsEdgeDetector.edges(at: at(500, 0)) == [.top], "y = 0 is the top edge")
check(ShearsEdgeDetector.edges(at: at(500, H - 1)) == [.bottom], "y = h-1 is the bottom edge")
check(ShearsEdgeDetector.edges(at: at(1, 1)).isEmpty, "one pixel in is not on an edge")
check(ShearsEdgeDetector.edges(at: at(W - 2, H - 2)).isEmpty, "one pixel in from the far corner is not on an edge")
check(ShearsEdgeDetector.edges(at: at(0, 0)) == [.left, .top], "the top-left corner is two edges")
check(ShearsEdgeDetector.edges(at: at(W - 1, H - 1)) == [.right, .bottom], "the bottom-right corner is two edges")
check(ShearsEdgeDetector.edges(at: at(-3, 900)) == [.left, .bottom], "positions beyond the screen count as on that edge")

// A steady firm push: `step` points every 10 ms starting after the dwell.
func push(_ d: inout ShearsEdgeDetector, dx: Double, dy: Double, from t0: Double, steps: Int, step: Double = 8) -> (ShearsEdge?, Double) {
    var t = t0
    for _ in 0..<steps {
        t += 0.01
        if let e = d.host(dx: dx * step, dy: dy * step, now: t) { return (e, t) }
    }
    return (nil, t)
}

// every edge releases, and only that edge, from the middle of the edge
let cases: [(ShearsEdge, ShearsPointerReport, Double, Double, String)] = [
    (.left, at(0, 400), -1, 0, "left"), (.right, at(W - 1, 400), 1, 0, "right"),
    (.top, at(500, 0), 0, -1, "top"), (.bottom, at(500, H - 1), 0, 1, "bottom"),
]
for (edge, rep, dx, dy, name) in cases {
    var d = ShearsEdgeDetector()
    d.guest(rep, now: 10)
    let (hit, _) = push(&d, dx: dx, dy: dy, from: 10.2, steps: 30)
    check(hit == edge, "\(name): a firm push through the edge releases it")

    var inward = ShearsEdgeDetector()
    inward.guest(rep, now: 10)
    let (none, _) = push(&inward, dx: -dx, dy: -dy, from: 10.2, steps: 60)
    check(none == nil, "\(name): pushing inward never releases")

    var along = ShearsEdgeDetector()
    along.guest(rep, now: 10)
    let (slide, _) = push(&along, dx: dy != 0 ? 1 : 0, dy: dx != 0 ? 1 : 0, from: 10.2, steps: 200)
    check(slide == nil, "\(name): sliding along the edge never releases")

    var wrongWay = ShearsEdgeDetector()
    wrongWay.guest(rep, now: 10)
    let (opp, _) = push(&wrongWay, dx: dx, dy: dy, from: 10.2, steps: 1, step: 8)
    check(opp == nil, "\(name): a single small push is below the threshold")
}

// arrival: the tail of a flick that lands on the edge does not release
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.0, steps: 10, step: 8)   // 100 ms of 8-point moves, inside the dwell
    check(hit == nil, "pushing during the dwell does not release")
    let (later, _) = push(&d, dx: -1, dy: 0, from: 10.2, steps: 20)
    check(later == .left, "the same push after the dwell releases")
}

// slow drift: 1 point per 20 ms is 50 points/s, under the leak, so it never releases
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.2, steps: 1000, step: 0.6)
    check(hit == nil, "a slow drift against the edge never releases")
}

// pushing in then back out cancels
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    _ = push(&d, dx: -1, dy: 0, from: 10.2, steps: 3)            // 24 points out
    _ = push(&d, dx: 1, dy: 0, from: 10.23, steps: 5)            // 40 points back
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.28, steps: 3)
    check(hit == nil, "pushing back inward cancels the earlier push")
}

// the arrow leaving the edge resets the dwell and the push
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    _ = push(&d, dx: -1, dy: 0, from: 10.2, steps: 4)
    d.guest(at(30, 400), now: 10.3)
    d.guest(at(0, 400), now: 10.31)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.31, steps: 10)   // 80 points inside the new dwell
    check(hit == nil, "coming back to the edge starts the dwell again")
}

// the push that was built up before the arrow left the edge is gone when it comes back
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    _ = push(&d, dx: -1, dy: 0, from: 10.2, steps: 5, step: 7)     // 35 points of push, just under the threshold
    d.guest(at(25, 400), now: 10.26)
    d.guest(at(0, 400), now: 10.27)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.40, steps: 2, step: 10)  // 20 more points, after the new dwell
    check(hit == nil, "leaving the edge clears the accumulated push")
}

// buttons, stale reports, switched off, no report at all
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400, buttons: 1), now: 10)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.2, steps: 30)
    check(hit == nil, "a drag (button down) does not release")

    var stale = ShearsEdgeDetector()
    stale.guest(at(0, 400), now: 10)
    let (s, _) = push(&stale, dx: -1, dy: 0, from: 12, steps: 30)
    check(s == nil, "a report older than staleAfter says nothing")

    var off = ShearsEdgeDetector()
    off.threshold = 0
    off.guest(at(0, 400), now: 10)
    let (o, _) = push(&off, dx: -1, dy: 0, from: 10.2, steps: 30)
    check(o == nil, "threshold 0 turns edge release off")

    var none = ShearsEdgeDetector()
    let (n, _) = push(&none, dx: -1, dy: 0, from: 10.2, steps: 30)
    check(n == nil, "no guest report, no release")

    var r = ShearsEdgeDetector()
    r.guest(at(0, 400), now: 10)
    r.reset()
    let (after, _) = push(&r, dx: -1, dy: 0, from: 10.2, steps: 30)
    check(after == nil && r.report == nil, "reset forgets the position")
}

// corners: each edge is judged on its own push
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 0), now: 10)
    let (a, _) = push(&d, dx: 0, dy: -1, from: 10.2, steps: 30)
    check(a == .top, "top-left corner: pushing up releases the top")
    var e = ShearsEdgeDetector()
    e.guest(at(0, 0), now: 10)
    let (b, _) = push(&e, dx: -1, dy: 0, from: 10.2, steps: 30)
    check(b == .left, "top-left corner: pushing left releases the left")
    var f = ShearsEdgeDetector()
    f.guest(at(W - 1, H - 1), now: 10)
    let (c, _) = push(&f, dx: 1, dy: 0, from: 10.2, steps: 30)
    check(c == .right, "bottom-right corner: pushing right releases the right")
    var g = ShearsEdgeDetector()
    g.guest(at(W - 1, H - 1), now: 10)
    let (dd, _) = push(&g, dx: 0, dy: 1, from: 10.2, steps: 30)
    check(dd == .bottom, "bottom-right corner: pushing down releases the bottom")
}

// a heartbeat report at the same spot does not restart the dwell
do {
    var d = ShearsEdgeDetector()
    d.guest(at(0, 400), now: 10)
    d.guest(at(0, 400), now: 10.5)
    let (hit, _) = push(&d, dx: -1, dy: 0, from: 10.5, steps: 10)    // 80 points, less than one dwell after the repeat
    check(hit == .left, "a repeated report at the edge keeps the original arrival time")
}

// MARK: session: is the tool running, and shutdown requests

do {
    var s = ShearsSession()
    check(!s.isRunning(now: 100), "no tool before HELLO")
    check(s.status(now: 100) == ShearsToolStatus(toolRunning: false, toolVersion: 0, capabilities: 0, shutdown: .idle), "initial status")
    check(!s.requestShutdown(now: 100), "no shutdown request without a tool")
    s.called(now: 100)
    check(!s.isRunning(now: 100), "calls without a HELLO do not make it a running tool")
    check(!s.requestShutdown(now: 100), "and cannot be asked to shut down")
    check(!s.shutdownFlagged, "nothing flagged without a request")

    check(s.hello(version: 1, capabilities: 3, now: 100), "first HELLO is a change")
    check(!s.hello(version: 1, capabilities: 3, now: 101), "the same HELLO again is not")
    check(s.hello(version: 2, capabilities: 3, now: 102), "a new version is a change")
    check(s.isRunning(now: 102), "running right after HELLO")
    check(s.isRunning(now: 102 + ShearsSession.liveWindow), "still running at the edge of the window")
    check(!s.isRunning(now: 102 + ShearsSession.liveWindow + 0.1), "gone once it has been silent longer than the window")
    s.called(now: 120)
    check(s.isRunning(now: 124), "any call refreshes it")
    check(s.status(now: 124).toolVersion == 2 && s.status(now: 124).capabilities == 3, "status carries version and capabilities")
    check(!s.isRunning(now: 126.5), "and it lapses again")
}

do {
    var s = ShearsSession()
    s.hello(version: 1, capabilities: 3, now: 10)
    check(s.requestShutdown(now: 11), "a running tool accepts a shutdown request")
    check(s.shutdownFlagged, "the request is flagged for the poll replies")
    check(!s.requestShutdown(now: 11.5), "a second request while one is waiting is refused")
    check(s.shutdownFlagged, "still flagged until acknowledged")
    s.called(now: 12)
    s.shutdownResult(0, now: 12)
    check(!s.shutdownFlagged, "acknowledged: no longer flagged")
    check(s.shutdown == .sent(since: 12), "phase is sent")
    check(!s.requestShutdown(now: 15), "too soon after a sent request")
    s.called(now: 21)
    check(s.requestShutdown(now: 22.1), "a retry is allowed after retryAfter")
    s.called(now: 22.2)
    s.shutdownResult(-1708, now: 22.3)
    check(s.shutdown == .failed(code: -1708) && !s.shutdownFlagged, "a failure is recorded and not re-flagged")
    check(s.requestShutdown(now: 23), "a failed request can be tried again")
    s.shutdownResult(0, now: 23.5)
    check(s.shutdown == .sent(since: 23.5), "and it can then succeed")
}

do {
    var s = ShearsSession()
    s.hello(version: 1, capabilities: 3, now: 10)
    s.shutdownResult(0, now: 11)
    check(s.shutdown == .idle, "a result nobody asked for is ignored")
    check(s.requestShutdown(now: 12), "request accepted")
    check(!s.requestShutdown(now: 12 + ShearsSession.liveWindow + 1), "a silent tool cannot be asked, even with a request pending")
    var gone = ShearsSession()
    gone.hello(version: 1, capabilities: 3, now: 10)
    check(!gone.requestShutdown(now: 10 + ShearsSession.liveWindow + 1), "no request once the tool has gone quiet")
    check(s.status(now: 12 + ShearsSession.liveWindow + 1).shutdown == .requested(since: 12), "the pending request is still visible in the status")
}

// SYS_RESULT and the new reply format
check(ShearsCommand(rawValue: 5) == .sysResult, "command 5 is SYS_RESULT")
check(ShearsCommand(rawValue: 6) == .settings, "command 6 is SETTINGS")
check(ShearsCommand(rawValue: 7) == nil, "command 7 is unknown")
check(Shears.flagShutdownRequested == 2 && Shears.capShutdown == 2, "flag and capability bit values are part of the guest ABI")

// features: part of the guest ABI (the control panel writes these bits)
check(ShearsFeatures.edgeRelease.rawValue == 1 && ShearsFeatures.clipboard.rawValue == 2, "feature bit values are part of the guest ABI")
check(ShearsFeatures.all.rawValue == 3, "all features")
check(ShearsFeatures(guestWord: 0xFFFF_FFFF) == .all, "unknown feature bits from the guest are dropped")
check(ShearsFeatures(guestWord: 0).isEmpty, "a guest can switch everything off")
check(ShearsFeatures(guestWord: 2) == .clipboard, "clipboard only")
check(ShearsFeatures(guestWord: 1).intersection(.all) == .edgeRelease, "edge release only")
check(ShearsFeatures.all.intersection([.clipboard]) == .clipboard && ShearsFeatures([.edgeRelease]).intersection(.clipboard).isEmpty, "a feature needs both sides")

// installer: adding the disk to a VM's prefs
do {
    let path = "/Users/x/Library/Application Support/SheepShaver/Sheep Shears Installer.hfv"
    let added = SheepShearsInstaller.prefsText("disk /a.hfv\nscreen win/1024/768\n", adding: path)
    check(added == "disk /a.hfv\nscreen win/1024/768\ndisk *\(path)\n", "the installer disk is appended as a read-only disk")
    check(SheepShearsInstaller.prefsText("disk /a.hfv", adding: path) == "disk /a.hfv\ndisk *\(path)\n", "a missing final newline is supplied")
    check(SheepShearsInstaller.prefsText("", adding: path) == "disk *\(path)\n", "an empty prefs file works")
    check(SheepShearsInstaller.prefsText("disk *\(path)\n", adding: path) == nil, "already added (read-only): nothing changes")
    check(SheepShearsInstaller.prefsText("disk \(path)\n", adding: path) == nil, "already added (writable): nothing changes")
    check(SheepShearsInstaller.prefsText("cdrom \(path)\n", adding: path) != nil, "only a disk line counts")
    check(SheepShearsInstaller.prefsText("disk \(path)2\n", adding: path) != nil, "a longer path is another disk")
    let again = SheepShearsInstaller.prefsText(SheepShearsInstaller.prefsText("disk /a\n", adding: path)!, adding: path)
    check(again == nil, "adding twice adds once")
}

print("shears tests: \(checks) checks, \(failures) failed")
exit(failures == 0 ? 0 : 1)
