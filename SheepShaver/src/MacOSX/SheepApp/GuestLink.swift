/*
 *  GuestLink.swift - What the guest display view needs from the emulator: the keyboard and mouse going in, and a little
 *  state coming out (the Mac cursor, whether the guest draws it, the edge-release distance, the VM's prefs).
 *
 *  A VM in its own window talks to its emulator directly (`LocalGuestLink`: the same VideoHost* calls the view always
 *  made). A VM shown in the library window talks to another process (`RemoteGuestLink`, over the display socket).
 *  The view does not know which one it has.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

@MainActor
protocol GuestLink: AnyObject {
    func key(_ code: Int32, down: Bool)
    func mouseAbs(_ x: Int32, _ y: Int32)
    func mouseMove(_ dx: Int32, _ dy: Int32)
    func button(_ number: Int32, down: Bool)
    func setRelativeMouse(_ on: Bool)
    /// The guest tool should report where the guest pointer is (only while grabbed in absolute mode).
    func setPointerWanted(_ on: Bool)
    /// The Mac cursor as `MacCursor` in video.cpp holds it (68 bytes), or nil when there is none yet.
    var cursorBytes: [UInt8]? { get }
    /// True once the guest draws its own cursor (the host arrow is hidden over the picture then).
    var cursorHidesHost: Bool { get }
    /// Pointer distance (guest pixels) that releases the grab at a screen edge; 0 when edge release is off.
    var edgeReleaseThreshold: Double { get }
    /// The VM's prefs ("mouse", "hardcursor").
    func prefString(_ key: String) -> String
    func prefBool(_ key: String) -> Bool
}

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

/// The emulator is in this process: direct calls, exactly as the window made them before the link existed.
@MainActor
final class LocalGuestLink: GuestLink {
    func key(_ code: Int32, down: Bool) { VideoHostKey(code, down ? 1 : 0) }
    func mouseAbs(_ x: Int32, _ y: Int32) { VideoHostMouseAbs(x, y) }
    func mouseMove(_ dx: Int32, _ dy: Int32) { VideoHostMouseMove(dx, dy) }
    func button(_ number: Int32, down: Bool) { VideoHostMouseButton(number, down ? 1 : 0) }
    func setRelativeMouse(_ on: Bool) { VideoHostSetRelMouse(on ? 1 : 0) }
    func setPointerWanted(_ on: Bool) { ShearsHost.shared.setPointerWanted(on) }
    var cursorBytes: [UInt8]? {
        VideoHostCursorBytes().map { Array(UnsafeBufferPointer(start: $0, count: DisplayWire.cursorBytes)) }
    }
    var cursorHidesHost: Bool { VideoGuestCursorHidesHost() != 0 }
    var edgeReleaseThreshold: Double { ShearsHost.shared.edgeReleaseThreshold }
    func prefString(_ key: String) -> String { PrefsBridge.string(key) }
    func prefBool(_ key: String) -> Bool { PrefsBridge.bool(key) }
}
