/*
 *  GuestDisplayView.swift - Metal screen. The right pane of the window.
 *
 *  Pointer events are converted into this view's bounds. The sidebar is a
 *  sibling, so its width is not part of the guest position. Hidden, this
 *  view is the full content width. The picture is this view's current
 *  bounds, and it moves when the window resizes or the sidebar toggles.
 *  A click in the picture grabs. The host pointer stays put until ctrl-g.
 *  Movement is the physical delta, delivered to the guest. The guest
 *  clamps its arrow to the screen. This window does not keep a second
 *  position, and the picture edge does not release. mouse relative grabs
 *  on a click and releases the same way.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import QuartzCore

/// Diagnostic output: on in Debug builds, or in any build with NW_VERBOSE=1 in the environment (NW_VERBOSE=0 turns
/// it off). A normal run prints status lines, warnings and errors only. The C++ side is nw_log.h.
let nwDiagnosticsOn: Bool = {
    if let e = ProcessInfo.processInfo.environment["NW_VERBOSE"], !e.isEmpty { return e != "0" }
    #if DEBUG
    return true
    #else
    return false
    #endif
}()

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

private final class GuestArrowView: NSImageView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

@MainActor
final class GuestDisplayView: NSView {
    var inputEnabled = false
    private var attached = false
    private var gaming = false
    private var modifiers: NSEvent.ModifierFlags = []
    private var tracking: NSTrackingArea?
    private var guestW: CGFloat = 1024
    private var guestH: CGFloat = 768
    private var macCursor: NSCursor?
    private var hidHost = false
    private var cursorHidden = false
    private var disassociated = false
    private var fracX: CGFloat = 0
    private var fracY: CGFloat = 0
    private var appliedGames: Bool?
    private var trackingClick = false
    private let hint = NSTextField(labelWithString: "ctrl-g to release")
    private let guestArrow = GuestArrowView()
    private var arrowHotX: CGFloat = 1
    private var arrowHotY: CGFloat = 1
    private var arrowX = 0
    private var arrowY = 0
    private var arrowVisible = false
    private var arrowHasPosition = false
    private var grabHold: Timer?
    private var moveMonitor: Any?
    /// Ctrl-G is eaten on the way down. The matching G up must not reach the guest.
    private var suppressGuestKeyUp: UInt16?
    /// Cursor position in the space `CGWarpMouseCursorPosition` uses.
    /// Sampled at the grab, and again when the window is key in front.
    /// A later read that disagrees means macOS reconnected the pointer.
    /// The warp puts it back. That call posts no mouse event, so the
    /// move that exposed the drift is delivered. The difference itself
    /// is not sent: a steady error flooded the guest.
    private var lockPoint: CGPoint?
    private var stuckDx: CGFloat = 0
    private var stuckDy: CGFloat = 0
    /// Host pointer is usable again while this window is not the foreground
    /// key window. The grab stays attached; ctrl-g is still the release.
    private var pausingCapture = false
    private var loggedWarpError: Int32?
    private var loggedAssociateError: Int32?

    override var isOpaque: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override var safeAreaInsets: NSEdgeInsets { NSEdgeInsets() }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func makeBackingLayer() -> CALayer { CAMetalLayer() }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        clipsToBounds = true
        layer?.isOpaque = true
        hint.textColor = .white
        hint.backgroundColor = NSColor.black.withAlphaComponent(0.55)
        hint.drawsBackground = true
        hint.isBezeled = false
        hint.font = .systemFont(ofSize: 13, weight: .medium)
        hint.isHidden = true
        addSubview(hint)
        guestArrow.imageScaling = .scaleAxesIndependently
        guestArrow.imageFrameStyle = .none
        guestArrow.isHidden = true
        addSubview(guestArrow)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    var guestPixelSize: NSSize { NSSize(width: guestW, height: guestH) }

    func setGuestSize(width: Int, height: Int) {
        guestW = CGFloat(max(width, 1))
        guestH = CGFloat(max(height, 1))
        layoutGuestArrow()
    }

    /// A click in the picture grabs. ctrl-g releases it.
    /// `mouse relative` grabs on a click and releases the same way.
    func applyMousePrefs() {
        let games = PrefsBridge.string("mouse") == "relative"
        if appliedGames != games {
            appliedGames = games
            setGaming(games)
        }
        setHostCursor()
    }

    func setGaming(_ on: Bool) {
        attached = false
        VideoHostMouseButton(0, 0)
        VideoHostMouseButton(1, 0)
        associateCursor()
        showHostCursor()
        VideoHostSetRelMouse(0)
        hint.isHidden = true
        stopGrabHold()
        gaming = on
        layoutGuestArrow()
        logMouse("NW-BOOT mouse mode=\(on ? "relative" : "absolute")", status: true)
    }

    override func layout() {
        super.layout()
        let size = hint.intrinsicContentSize
        hint.frame = NSRect(
            x: bounds.midX - size.width / 2 - 8,
            y: 12,
            width: size.width + 16,
            height: size.height + 8
        )
        layoutGuestArrow()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.acceptsMouseMovedEvents = true
        let center = NotificationCenter.default
        center.removeObserver(self, name: NSApplication.willResignActiveNotification, object: nil)
        center.removeObserver(self, name: NSApplication.didBecomeActiveNotification, object: nil)
        center.removeObserver(self, name: NSWindow.didResignKeyNotification, object: nil)
        center.removeObserver(self, name: NSWindow.didBecomeKeyNotification, object: nil)
        guard let window else { return }
        center.addObserver(self, selector: #selector(focusChanged(_:)), name: NSApplication.willResignActiveNotification, object: NSApp)
        center.addObserver(self, selector: #selector(focusChanged(_:)), name: NSApplication.didBecomeActiveNotification, object: NSApp)
        center.addObserver(self, selector: #selector(focusChanged(_:)), name: NSWindow.didResignKeyNotification, object: window)
        center.addObserver(self, selector: #selector(focusChanged(_:)), name: NSWindow.didBecomeKeyNotification, object: window)
    }

    deinit {
        let center = NotificationCenter.default
        center.removeObserver(self, name: NSApplication.willResignActiveNotification, object: nil)
        center.removeObserver(self, name: NSApplication.didBecomeActiveNotification, object: nil)
        center.removeObserver(self, name: NSWindow.didResignKeyNotification, object: nil)
        center.removeObserver(self, name: NSWindow.didBecomeKeyNotification, object: nil)
    }

    /// Losing key or the app does not release the grab. It does stop
    /// warping, which would pull the pointer out of the other window.
    /// Reconnect runs from will-resign-active, while this app is still
    /// allowed to call `CGAssociateMouseAndMouseCursorPosition`. A sheet
    /// only resigns key; the app is still in front, so that reconnect
    /// works from did-resign-key.
    @objc private func focusChanged(_ notification: Notification) {
        guard attached else { return }
        switch notification.name {
        case NSApplication.willResignActiveNotification, NSWindow.didResignKeyNotification:
            pauseCapture()
        case NSApplication.didBecomeActiveNotification, NSWindow.didBecomeKeyNotification:
            guard captureIsForeground else { return }
            resumeCapture()
        default:
            break
        }
    }

    override func resetCursorRects() {
        // The guest arrow stays put until the click that grabs. The host
        // arrow has to remain visible so that click can land, including
        // on the menu bar. Once grabbed, the guest draws the only arrow.
        if attached && !PrefsBridge.bool("hardcursor") {
            addCursorRect(bounds, cursor: Self.blankCursor)
            return
        }
        if PrefsBridge.bool("hardcursor") {
            addCursorRect(bounds, cursor: macCursor ?? .arrow)
            return
        }
        addCursorRect(bounds, cursor: .arrow)
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking {
            removeTrackingArea(tracking)
        }
        // The rect is the picture as placed. inVisibleRect keeps it on that
        // picture when the window size changes.
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .enabledDuringMouseDrag, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        tracking = area
    }

    /// The picture moved: the window resized, or the sidebar was shown or hidden.
    /// A grab stays grabbed. Absolute mode, while released, remaps the pointer
    /// onto the new bounds. Relative mode recenters when the sidebar toggle
    /// changed the picture.
    func syncPointerToPicture(recenterRelative: Bool = false) {
        updateTrackingAreas()
        guard inputEnabled, bounds.width > 1, bounds.height > 1 else { return }
        if gaming {
            guard attached else { return }
            if recenterRelative {
                warpToCenter()
            }
            return
        }
        place(nil)
    }

    /// Clicks in the picture, including the menu bar along the top edge.
    /// The window would otherwise treat that strip as title-bar drag.
    func claimMouse(_ event: NSEvent) -> Bool {
        switch event.type {
        case .leftMouseDown, .leftMouseUp, .leftMouseDragged,
             .rightMouseDown, .rightMouseUp, .rightMouseDragged,
             .otherMouseDown, .otherMouseUp, .otherMouseDragged:
            break
        default:
            return false
        }
        guard inputEnabled, window?.attachedSheet == nil else { return false }
        let p = convert(event.locationInWindow, from: nil)
        let down = event.type == .leftMouseDown || event.type == .rightMouseDown || event.type == .otherMouseDown
        let up = event.type == .leftMouseUp || event.type == .rightMouseUp || event.type == .otherMouseUp
        if down && !containsPicture(p) {
            trackingClick = false
            return false
        }
        guard containsPicture(p) || (trackingClick && !down) else { return false }
        if down { trackingClick = true }
        if up { trackingClick = false }
        switch event.type {
        case .leftMouseDown:
            mouseDown(with: event)
        case .leftMouseUp:
            mouseUp(with: event)
        case .leftMouseDragged:
            mouseDragged(with: event)
        case .rightMouseDown:
            rightMouseDown(with: event)
        case .rightMouseUp:
            rightMouseUp(with: event)
        case .rightMouseDragged:
            rightMouseDragged(with: event)
        case .otherMouseDown:
            otherMouseDown(with: event)
        case .otherMouseUp:
            otherMouseUp(with: event)
        case .otherMouseDragged:
            otherMouseDragged(with: event)
        default:
            return false
        }
        return true
    }

    override func mouseEntered(with event: NSEvent) {
        window?.makeFirstResponder(self)
        guard inputEnabled else { return }
        if gaming {
            if !attached { return }
            place(event)
            setHostCursor()
            return
        }
        place(event)
        setHostCursor()
    }

    override func mouseMoved(with event: NSEvent) {
        if moveMonitor != nil { return }
        move(event)
    }

    override func mouseDragged(with event: NSEvent) {
        if moveMonitor != nil { return }
        move(event)
    }

    override func rightMouseDragged(with event: NSEvent) {
        if moveMonitor != nil { return }
        move(event)
    }

    override func otherMouseDragged(with event: NSEvent) {
        if moveMonitor != nil { return }
        move(event)
    }

    override func mouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        if gaming {
            captureForClick()
            press(event)
            return
        }
        grabAbsolute(event)
        press(event)
    }

    override func mouseUp(with event: NSEvent) {
        release(event)
    }

    override func rightMouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        if gaming {
            captureForClick()
            press(event)
            return
        }
        grabAbsolute(event)
        press(event)
    }

    override func rightMouseUp(with event: NSEvent) {
        release(event)
    }

    override func otherMouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        if gaming {
            captureForClick()
            press(event)
            return
        }
        grabAbsolute(event)
        press(event)
    }

    override func otherMouseUp(with event: NSEvent) {
        release(event)
    }

    override func keyDown(with event: NSEvent) {
        if releaseByHotkey(event) { return }
        guard inputEnabled else { return }
        VideoHostKey(adbCode(event.keyCode), 1)
    }

    override func keyUp(with event: NSEvent) {
        if consumeHotkeyUp(event) { return }
        guard inputEnabled else { return }
        VideoHostKey(adbCode(event.keyCode), 0)
    }

    override func flagsChanged(with event: NSEvent) {
        guard inputEnabled else { return }
        let now = event.modifierFlags
        change(.shift, 0x38, now)
        change(.control, 0x36, now)
        change(.option, 0x3a, now)
        change(.command, 0x37, now)
        change(.capsLock, 0x39, now)
        modifiers = now
    }

    /// Carbon virtual key codes are not ADB codes. Letters and digits already
    /// match the SDL table. These do not: host Control is 0x3B, which is the
    /// guest's Left Arrow.
    private func adbCode(_ host: UInt16) -> Int32 {
        switch host {
        case 0x38, 0x3c: return 0x38 // Shift
        case 0x3a, 0x3d: return 0x3a // Option
        case 0x37, 0x36: return 0x37 // Command
        case 0x3b, 0x3e: return 0x36 // Control
        case 0x39: return 0x39 // Caps Lock
        case 0x7e: return 0x3e // Up
        case 0x7d: return 0x3d // Down
        case 0x7b: return 0x3b // Left
        case 0x7c: return 0x3c // Right
        case 0x35: return 0x35 // Escape
        case 0x24: return 0x24 // Return
        case 0x30: return 0x30 // Tab
        case 0x33: return 0x33 // Delete
        case 0x75: return 0x75 // Forward Delete
        default: return Int32(host)
        }
    }

    func applyMacCursor() {
        macCursor = Self.makeMacCursor()
        if let macCursor {
            guestArrow.image = macCursor.image
            arrowHotX = macCursor.hotSpot.x
            arrowHotY = macCursor.hotSpot.y
        }
        layoutGuestArrow()
        setHostCursor()
    }

    /// Mac OS reports where it drew the arrow. While the pointer is grabbed
    /// the arrow is this view, above the picture, so a movie cannot cover it.
    func setGuestArrow(x: Int, y: Int, visible: Bool) {
        arrowX = x
        arrowY = y
        arrowVisible = visible
        arrowHasPosition = true
        layoutGuestArrow()
    }

    private func layoutGuestArrow() {
        let show = attached && arrowHasPosition && (!gaming || arrowVisible)
        guestArrow.isHidden = !show
        guard show, guestW > 1, guestH > 1, bounds.width > 1, bounds.height > 1 else { return }
        let scaleX = bounds.width / guestW
        let scaleY = bounds.height / guestH
        let px = CGFloat(arrowX) * scaleX
        let py = bounds.height - CGFloat(arrowY) * scaleY
        let w = 16 * scaleX
        let h = 16 * scaleY
        guestArrow.frame = NSRect(
            x: px - arrowHotX * scaleX,
            y: py - (16 - arrowHotY) * scaleY,
            width: w,
            height: h
        )
    }

    func releaseByHotkey(_ event: NSEvent) -> Bool {
        let hotkey = event.keyCode == 5 && event.modifierFlags.contains(.control)
        guard hotkey else { return false }
        if !attached { return gaming && event.isARepeat }
        if modifiers.contains(.control) {
            VideoHostKey(0x36, 0)
        }
        suppressGuestKeyUp = event.keyCode
        ungrab("hotkey")
        return true
    }

    /// The G up that follows an eaten Ctrl-G. One shot.
    func consumeHotkeyUp(_ event: NSEvent) -> Bool {
        guard event.keyCode == suppressGuestKeyUp else { return false }
        suppressGuestKeyUp = nil
        return true
    }

    private func change(_ flag: NSEvent.ModifierFlags, _ code: Int32, _ now: NSEvent.ModifierFlags) {
        let was = modifiers.contains(flag)
        let isOn = now.contains(flag)
        if was != isOn {
            VideoHostKey(code, isOn ? 1 : 0)
        }
    }

    private func containsPicture(_ p: NSPoint) -> Bool {
        p.x >= bounds.minX && p.y >= bounds.minY && p.x < bounds.maxX && p.y < bounds.maxY
    }

    /// Guest pixel for an absolute pointer that is inside the picture.
    /// Above, below, or off either side is not a guest position.
    private func absolutePoint(_ p: NSPoint) -> (x: Int, y: Int)? {
        guard bounds.width > 1, bounds.height > 1 else { return nil }
        guard p.x >= bounds.minX, p.x < bounds.maxX else { return nil }
        guard p.y >= bounds.minY, p.y < bounds.maxY else { return nil }
        let x = Int(p.x / bounds.width * guestW)
        let y = Int((bounds.height - p.y) / bounds.height * guestH)
        return (x, y)
    }

    private func captureForClick() {
        window?.makeFirstResponder(self)
        if gaming && !attached {
            grabGaming()
        }
    }

    private func grabGaming() {
        attached = true
        logMouse("NW-BOOT mouse grab")
        VideoHostSetRelMouse(1)
        disassociateCursor()
        warpToCenter()
        hideHostCursor()
        hint.isHidden = false
        fracX = 0
        fracY = 0
        window?.makeFirstResponder(self)
        setHostCursor()
        startGrabHold()
    }

    private func ungrab(_ reason: String) {
        guard attached else { return }
        attached = false
        stopGrabHold()
        VideoHostMouseButton(0, 0)
        VideoHostMouseButton(1, 0)
        VideoHostSetRelMouse(0)
        associateCursor()
        showHostCursor()
        hint.isHidden = true
        layoutGuestArrow()
        setHostCursor()
        logMouse("NW-BOOT mouse ungrab reason=\(reason)")
    }

    private func hideHostCursor() {
        if cursorHidden { return }
        NSCursor.hide()
        cursorHidden = true
    }

    private func showHostCursor() {
        if !cursorHidden { return }
        NSCursor.unhide()
        cursorHidden = false
    }

    private func disassociateCursor() {
        captureCursor()
    }

    /// macOS reconnects a disconnected cursor after the pointer sits still.
    /// The next physical move then leaves the picture and the view stops
    /// hearing deltas. Keep the disconnect, put the pointer back on the
    /// grab point, and take moves from a monitor so a tracking rect is
    /// not required.
    private func startGrabHold() {
        stopGrabHold()
        lockPoint = cursorInWarpSpace()
        stuckDx = 0
        stuckDy = 0
        holdGrab()
        let timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.holdGrab()
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        grabHold = timer
        moveMonitor = NSEvent.addLocalMonitorForEvents(
            matching: [.mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged]
        ) { [weak self] event in
            MainActor.assumeIsolated {
                guard let self, self.attached, !self.pausingCapture else { return }
                self.move(event)
                self.holdGrab()
            }
            return event
        }
    }

    private func stopGrabHold() {
        grabHold?.invalidate()
        grabHold = nil
        lockPoint = nil
        stuckDx = 0
        stuckDy = 0
        pausingCapture = false
        if let moveMonitor {
            NSEvent.removeMonitor(moveMonitor)
            self.moveMonitor = nil
        }
    }

    /// The screen whose frame origin is Cocoa's global origin. Its height
    /// is the flip into `CGWarpMouseCursorPosition`, including when this
    /// window is on another display.
    private func primaryScreen() -> NSScreen? {
        NSScreen.screens.first { $0.frame.origin == .zero } ?? NSScreen.screens.first
    }

    /// Cocoa mouse location, flipped into the top-left space the warp uses.
    private func cursorInWarpSpace() -> CGPoint? {
        guard let primary = primaryScreen() else { return nil }
        let cocoa = NSEvent.mouseLocation
        return CGPoint(x: cocoa.x, y: primary.frame.height - cocoa.y)
    }

    private var captureIsForeground: Bool {
        NSApp.isActive && window?.isKeyWindow == true
    }

    private func holdGrab() {
        guard attached else { return }
        if !captureIsForeground {
            pauseCapture()
            return
        }
        if pausingCapture {
            resumeCapture()
        }
        if let lock = lockPoint, let now = cursorInWarpSpace() {
            let dx = now.x - lock.x
            let dy = now.y - lock.y
            if abs(dx) > 0.5 || abs(dy) > 0.5 {
                let repeated = abs(dx - stuckDx) < 1 && abs(dy - stuckDy) < 1
                if repeated && (abs(dx) > 200 || abs(dy) > 200) {
                    lockPoint = nil
                    logMouse("NW-BOOT mouse lock dropped dx=\(Int(dx)) dy=\(Int(dy))")
                } else {
                    stuckDx = dx
                    stuckDy = dy
                    // The warp posts no mouse event. Do not drop the next
                    // move: this method runs again on that move, and a
                    // sticky skip then discards every one of them.
                    let err = CGWarpMouseCursorPosition(lock)
                    if err != .success {
                        noteCaptureError("warp", err)
                    }
                }
            } else {
                stuckDx = 0
                stuckDy = 0
            }
        }
        captureCursor()
    }

    private func pauseCapture() {
        guard attached, !pausingCapture else { return }
        pausingCapture = true
        associateCursor()
        showHostCursor()
        logMouse("NW-BOOT mouse capture paused active=\(NSApp.isActive) key=\(window?.isKeyWindow == true)")
    }

    private func resumeCapture() {
        guard attached, pausingCapture else { return }
        pausingCapture = false
        lockPoint = cursorInWarpSpace()
        stuckDx = 0
        stuckDy = 0
        hideHostCursor()
        captureCursor()
        logMouse("NW-BOOT mouse capture resumed captured=\(disassociated)")
    }

    private func captureCursor() {
        guard captureIsForeground else { return }
        // A successful earlier call does not prove the system is still
        // disconnected. Reassert capture from holdGrab after focus changes
        // or pointer drift; otherwise only Ctrl-G and a new click repair it.
        let err = CGAssociateMouseAndMouseCursorPosition(boolean_t(0))
        if err == .success {
            disassociated = true
            loggedAssociateError = nil
        } else {
            noteCaptureError("disassociate", err)
        }
    }

    private func associateCursor() {
        if !disassociated { return }
        let err = CGAssociateMouseAndMouseCursorPosition(boolean_t(1))
        if err == .success {
            disassociated = false
            loggedAssociateError = nil
        } else {
            noteCaptureError("associate", err)
        }
    }

    private func noteCaptureError(_ what: String, _ err: CGError) {
        let code = err.rawValue
        if what == "warp" {
            if loggedWarpError == code { return }
            loggedWarpError = code
        } else if loggedAssociateError == code {
            return
        } else {
            loggedAssociateError = code
        }
        logMouse("NW-BOOT mouse \(what) failed \(code)", status: true)
    }

    private func warpToCenter() {
        guard let window, let primary = primaryScreen() else { return }
        let inWindow = convert(NSPoint(x: bounds.midX, y: bounds.midY), to: nil)
        let cocoa = window.convertToScreen(NSRect(origin: inWindow, size: .zero))
        let cg = CGPoint(x: cocoa.minX, y: primary.frame.height - cocoa.minY)
        let warpErr = CGWarpMouseCursorPosition(cg)
        if warpErr != .success {
            noteCaptureError("warp", warpErr)
        }
        let onErr = CGAssociateMouseAndMouseCursorPosition(boolean_t(1))
        let offErr = CGAssociateMouseAndMouseCursorPosition(boolean_t(0))
        if offErr == .success {
            disassociated = true
            loggedAssociateError = nil
        } else if onErr == .success {
            disassociated = false
            noteCaptureError("disassociate", offErr)
        } else {
            noteCaptureError("disassociate", offErr)
        }
    }

    private func setHostCursor() {
        window?.invalidateCursorRects(for: self)
    }

    private func press(_ event: NSEvent) {
        guard inputEnabled else { return }
        place(event)
        let which: Int32 = event.buttonNumber == 1 ? 1 : 0
        VideoHostMouseButton(which, 1)
    }

    private func release(_ event: NSEvent) {
        guard inputEnabled else { return }
        place(event)
        let which: Int32 = event.buttonNumber == 1 ? 1 : 0
        VideoHostMouseButton(which, 0)
    }

    /// Coordinates in this view. `convert(_:from: nil)` is the window point
    /// mapped through the view frame, so a sidebar to the left does not shift x.
    private func place(_ event: NSEvent?) {
        guard inputEnabled else { return }
        if gaming {
            if attached, let event {
                sendRel(event)
            }
            return
        }
        if attached, let event {
            sendAbsDelta(event)
        }
    }

    /// Host pointer stays where the click happened. Later motion is that
    /// physical delta, not a new absolute position minus a stored one.
    /// A disconnected pointer reports up as down, so delta Y is not negated.
    /// The guest clamps the arrow; stopping at this window's edge dropped
    /// movement whenever the stored position and the arrow had split.
    private func sendAbsDelta(_ event: NSEvent) {
        fracX += event.deltaX
        fracY += event.deltaY
        let dx = Int32(fracX.rounded(.towardZero))
        let dy = Int32(fracY.rounded(.towardZero))
        fracX -= CGFloat(dx)
        fracY -= CGFloat(dy)
        if dx != 0 || dy != 0 {
            VideoHostMouseMove(dx, dy)
        }
    }

    private func grabAbsolute(_ event: NSEvent) {
        guard !attached, !gaming else { return }
        let p = convert(event.locationInWindow, from: nil)
        if let mapped = absolutePoint(p) {
            let gx = Int32(min(max(mapped.x, 0), Int(guestW) - 1))
            let gy = Int32(min(max(mapped.y, 0), Int(guestH) - 1))
            VideoHostSetRelMouse(0)
            VideoHostMouseAbs(gx, gy)
        }
        attached = true
        VideoHostSetRelMouse(0)
        disassociateCursor()
        hideHostCursor()
        hint.isHidden = false
        layoutGuestArrow()
        fracX = 0
        fracY = 0
        window?.makeFirstResponder(self)
        setHostCursor()
        startGrabHold()
        logMouse("NW-BOOT mouse grab captured=\(disassociated) active=\(NSApp.isActive) key=\(window?.isKeyWindow == true)")
    }

    private func sendRel(_ event: NSEvent) {
        fracX += event.deltaX
        fracY += -event.deltaY
        let dx = Int32(fracX.rounded(.towardZero))
        let dy = Int32(fracY.rounded(.towardZero))
        fracX -= CGFloat(dx)
        fracY -= CGFloat(dy)
        if dx != 0 || dy != 0 {
            VideoHostMouseMove(dx, dy)
        }
    }

    private func move(_ event: NSEvent) {
        guard inputEnabled else { return }
        place(event)
        if gaming { return }
        let hide = VideoGuestCursorHidesHost() != 0
        if hide != hidHost {
            hidHost = hide
            setHostCursor()
        }
    }

    /// A status or error line always prints; everything else only with diagnostics on.
    private func logMouse(_ line: String, status: Bool = false) {
        guard status || nwDiagnosticsOn else { return }
        fputs(line + "\n", stdout)
        fflush(stdout)
    }

    private static let blankCursor: NSCursor = {
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: 16,
            pixelsHigh: 16,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 64,
            bitsPerPixel: 32
        ) else {
            return .arrow
        }
        rep.bitmapData?.update(repeating: 0, count: 16 * 64)
        let image = NSImage(size: NSSize(width: 16, height: 16))
        image.addRepresentation(rep)
        return NSCursor(image: image, hotSpot: .zero)
    }()

    private static func makeMacCursor() -> NSCursor? {
        guard let raw = VideoHostCursorBytes() else { return nil }
        let hotX = Int(raw[2])
        let hotY = Int(raw[3])
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: 16,
            pixelsHigh: 16,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 64,
            bitsPerPixel: 32
        ), let pixels = rep.bitmapData else {
            return nil
        }
        for y in 0..<16 {
            let rowBits = (UInt16(raw[4 + y * 2]) << 8) | UInt16(raw[5 + y * 2])
            let maskBits = (UInt16(raw[36 + y * 2]) << 8) | UInt16(raw[37 + y * 2])
            for x in 0..<16 {
                let bit: UInt16 = 0x8000 >> x
                let i = (y * 16 + x) * 4
                if maskBits & bit == 0 {
                    pixels[i] = 0
                    pixels[i + 1] = 0
                    pixels[i + 2] = 0
                    pixels[i + 3] = 0
                    continue
                }
                let black = rowBits & bit != 0
                pixels[i] = black ? 0 : 255
                pixels[i + 1] = black ? 0 : 255
                pixels[i + 2] = black ? 0 : 255
                pixels[i + 3] = 255
            }
        }
        let image = NSImage(size: NSSize(width: 16, height: 16))
        image.addRepresentation(rep)
        return NSCursor(image: image, hotSpot: NSPoint(x: hotX, y: hotY))
    }
}
