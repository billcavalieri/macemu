/*
 *  GuestDisplayView.swift - Metal screen. The right pane of the window.
 *
 *  Pointer events are converted into this view's bounds. The sidebar is a
 *  sibling, so its width is not part of the guest position. Hidden, this
 *  view is the full content width. The picture edge is this view's current
 *  bounds, and it moves when the window resizes or the sidebar toggles.
 *  edgegrab grabs on enter. Absolute releases at the left, right, or bottom.
 *  The menu bar is the top of the picture. Once the pointer leaves through
 *  that edge, the guest cursor stays put until the pointer comes back.
 *  mouse relative grabs on a click and releases at any edge or with ctrl-g.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import QuartzCore

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
    private var lastGX: Int32 = -1
    private var lastGY: Int32 = -1
    private var hidHost = false
    private var cursorHidden = false
    private var disassociated = false
    private var fracX: CGFloat = 0
    private var fracY: CGFloat = 0
    private var skipWarpDelta = false
    private var trackX: CGFloat = 0
    private var trackY: CGFloat = 0
    private var appliedGames: Bool?
    private var trackingClick = false
    private let hint = NSTextField(labelWithString: "ctrl-g to release")

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
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    var guestPixelSize: NSSize { NSSize(width: guestW, height: guestH) }

    func setGuestSize(width: Int, height: Int) {
        guestW = CGFloat(max(width, 1))
        guestH = CGFloat(max(height, 1))
        lastGX = -1
        lastGY = -1
    }

    /// Absolute tracks this picture. `edgegrab` releases at its edge.
    /// `mouse relative` grabs on a click until that edge or ctrl-g.
    func applyMousePrefs() {
        let games = PrefsBridge.string("mouse") == "relative"
        if appliedGames != games {
            appliedGames = games
            setGaming(games)
        }
        if !games {
            noteEdgeGrab()
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
        gaming = on
        logMouse("NW-BOOT mouse mode=\(on ? "relative" : "absolute")")
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
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.acceptsMouseMovedEvents = true
    }

    override func resetCursorRects() {
        if PrefsBridge.bool("hardcursor") && !(gaming && attached) {
            addCursorRect(bounds, cursor: macCursor ?? .arrow)
            return
        }
        // The guest draws the arrow into the picture, including during
        // boot before the video interrupt is live. A second host arrow
        // on top of it is the other cursor in that window.
        addCursorRect(bounds, cursor: Self.blankCursor)
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
    /// Absolute mode remaps the pointer onto the new bounds. A pointer that
    /// landed in the sidebar releases. Relative mode recenters only when the
    /// sidebar toggle changed which edges exist.
    func syncPointerToPicture(recenterRelative: Bool = false) {
        updateTrackingAreas()
        guard inputEnabled, bounds.width > 1, bounds.height > 1 else { return }
        if gaming {
            guard attached else { return }
            if recenterRelative {
                trackX = bounds.midX
                trackY = bounds.midY
                warpToCenter()
                return
            }
            if trackX < bounds.minX || trackY < bounds.minY || trackX >= bounds.maxX || trackY >= bounds.maxY {
                ungrab("windowEdge")
            }
            return
        }
        place(nil)
        if wantsEdgeRelease() && !attached {
            noteEdgeGrab()
        }
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
        if wantsEdgeRelease() && !attached {
            grabAtPicture()
        }
        place(event)
        setHostCursor()
    }

    override func mouseExited(with event: NSEvent) {
        guard inputEnabled, !gaming else { return }
        let p = convert(event.locationInWindow, from: nil)
        if abovePicture(p) {
            showHostCursor()
            return
        }
        // A stale tracking rect exits while the pointer is still over the picture.
        if containsPicture(p) { return }
        if edgeReleases() {
            ungrab("windowEdge")
        } else {
            showHostCursor()
        }
    }

    override func mouseMoved(with event: NSEvent) {
        move(event)
    }

    override func mouseDragged(with event: NSEvent) {
        move(event)
    }

    override func rightMouseDragged(with event: NSEvent) {
        move(event)
    }

    override func otherMouseDragged(with event: NSEvent) {
        move(event)
    }

    override func mouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        captureForClick()
        press(event)
    }

    override func mouseUp(with event: NSEvent) {
        release(event)
    }

    override func rightMouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        captureForClick()
        press(event)
    }

    override func rightMouseUp(with event: NSEvent) {
        release(event)
    }

    override func otherMouseDown(with event: NSEvent) {
        guard inputEnabled else { return }
        captureForClick()
        press(event)
    }

    override func otherMouseUp(with event: NSEvent) {
        release(event)
    }

    override func keyDown(with event: NSEvent) {
        if releaseByHotkey(event) { return }
        guard inputEnabled else { return }
        VideoHostKey(Int32(event.keyCode), 1)
    }

    override func keyUp(with event: NSEvent) {
        guard inputEnabled else { return }
        VideoHostKey(Int32(event.keyCode), 0)
    }

    override func flagsChanged(with event: NSEvent) {
        guard inputEnabled else { return }
        let now = event.modifierFlags
        change(.shift, 0x38, now)
        change(.control, 0x3b, now)
        change(.option, 0x3a, now)
        change(.command, 0x37, now)
        change(.capsLock, 0x39, now)
        modifiers = now
    }

    func applyMacCursor() {
        macCursor = Self.makeMacCursor()
        setHostCursor()
    }

    func releaseByHotkey(_ event: NSEvent) -> Bool {
        let hotkey = event.keyCode == 5 && event.modifierFlags.contains(.control)
        guard hotkey else { return false }
        guard gaming else { return false }
        if !attached { return event.isARepeat }
        if modifiers.contains(.control) {
            VideoHostKey(0x3b, 0)
        }
        ungrab("hotkey")
        return true
    }

    private func change(_ flag: NSEvent.ModifierFlags, _ code: Int32, _ now: NSEvent.ModifierFlags) {
        let was = modifiers.contains(flag)
        let isOn = now.contains(flag)
        if was != isOn {
            VideoHostKey(code, isOn ? 1 : 0)
        }
    }

    private func wantsEdgeRelease() -> Bool {
        PrefsBridge.bool("edgegrab")
    }

    private func edgeReleases() -> Bool {
        wantsEdgeRelease()
    }

    /// Absolute grab. The host cursor stays associated; the picture edge releases it.
    private func grabAtPicture() {
        attached = true
        VideoHostSetRelMouse(0)
        logMouse("NW-BOOT mouse grab")
    }

    private func noteEdgeGrab() {
        guard inputEnabled, !gaming, wantsEdgeRelease(), !attached, let window else { return }
        guard bounds.width > 1, bounds.height > 1 else { return }
        let p = convert(window.mouseLocationOutsideOfEventStream, from: nil)
        guard containsPicture(p) else { return }
        grabAtPicture()
    }

    private func containsPicture(_ p: NSPoint) -> Bool {
        p.x >= bounds.minX && p.y >= bounds.minY && p.x < bounds.maxX && p.y < bounds.maxY
    }

    /// Over the picture's width, but above it: the toolbar and title bar.
    /// The guest cursor stays where it was. Clicks there belong to the window.
    private func abovePicture(_ p: NSPoint) -> Bool {
        bounds.width > 1 && p.x >= bounds.minX && p.x < bounds.maxX && p.y >= bounds.maxY
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
        trackX = bounds.midX
        trackY = bounds.midY
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
    }

    private func ungrab(_ reason: String) {
        guard attached else { return }
        attached = false
        VideoHostMouseButton(0, 0)
        VideoHostMouseButton(1, 0)
        VideoHostSetRelMouse(0)
        associateCursor()
        showHostCursor()
        hint.isHidden = true
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
        if disassociated { return }
        CGAssociateMouseAndMouseCursorPosition(boolean_t(0))
        disassociated = true
    }

    private func associateCursor() {
        if !disassociated { return }
        CGAssociateMouseAndMouseCursorPosition(boolean_t(1))
        disassociated = false
    }

    private func warpToCenter() {
        guard let window, let primary = NSScreen.screens.first else { return }
        let inWindow = convert(NSPoint(x: bounds.midX, y: bounds.midY), to: nil)
        let cocoa = window.convertToScreen(NSRect(origin: inWindow, size: .zero))
        let cg = CGPoint(x: cocoa.minX, y: primary.frame.height - cocoa.minY)
        CGWarpMouseCursorPosition(cg)
        CGAssociateMouseAndMouseCursorPosition(boolean_t(1))
        CGAssociateMouseAndMouseCursorPosition(boolean_t(0))
        disassociated = true
        skipWarpDelta = true
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
        guard bounds.width > 1, bounds.height > 1 else { return }
        let winP: NSPoint
        if let event {
            winP = event.locationInWindow
        } else if let window {
            winP = window.mouseLocationOutsideOfEventStream
        } else {
            return
        }
        let p = convert(winP, from: nil)
        if abovePicture(p) {
            showHostCursor()
            return
        }
        guard let mapped = absolutePoint(p) else {
            if edgeReleases() {
                ungrab("windowEdge")
            }
            showHostCursor()
            return
        }
        hideHostCursor()
        let gx = Int32(min(max(mapped.x, 0), Int(guestW) - 1))
        let gy = Int32(min(max(mapped.y, 0), Int(guestH) - 1))
        if gx == lastGX && gy == lastGY { return }
        lastGX = gx
        lastGY = gy
        VideoHostMouseAbs(gx, gy)
    }

    private func sendRel(_ event: NSEvent) {
        if skipWarpDelta {
            skipWarpDelta = false
            return
        }
        fracX += event.deltaX
        fracY += -event.deltaY
        let dx = Int32(fracX.rounded(.towardZero))
        let dy = Int32(fracY.rounded(.towardZero))
        fracX -= CGFloat(dx)
        fracY -= CGFloat(dy)
        if dx != 0 || dy != 0 {
            VideoHostMouseMove(dx, dy)
        }
        guard wantsEdgeRelease(), bounds.width > 1, bounds.height > 1 else { return }
        trackX += event.deltaX
        trackY += event.deltaY
        guard trackX < bounds.minX || trackY < bounds.minY || trackX >= bounds.maxX || trackY >= bounds.maxY else { return }
        var exit = NSPoint(
            x: min(max(trackX, bounds.minX), bounds.maxX - 1),
            y: min(max(trackY, bounds.minY), bounds.maxY - 1)
        )
        if trackX < bounds.minX {
            exit.x = bounds.minX - 2
        } else if trackX >= bounds.maxX {
            exit.x = bounds.maxX + 2
        }
        if trackY < bounds.minY {
            exit.y = bounds.minY - 2
        } else if trackY >= bounds.maxY {
            exit.y = bounds.maxY + 2
        }
        ungrab("windowEdge")
        placeHostCursor(exit)
    }

    /// Warp after the cursor is associated again. A warp while disconnected swaps axes.
    private func placeHostCursor(_ local: NSPoint) {
        guard let window, let primary = NSScreen.screens.first else { return }
        let inWindow = convert(local, to: nil)
        let cocoa = window.convertToScreen(NSRect(origin: inWindow, size: .zero))
        let cg = CGPoint(x: cocoa.minX, y: primary.frame.height - cocoa.minY)
        CGWarpMouseCursorPosition(cg)
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

    private func logMouse(_ line: String) {
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
