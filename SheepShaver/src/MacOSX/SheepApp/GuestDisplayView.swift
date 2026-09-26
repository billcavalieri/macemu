/*
 *  GuestDisplayView.swift - Visible SheepForce Metal surface and input.
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

@_silgen_name("VideoHostMouseButton")
private func VideoHostMouseButton(_ button: Int32, _ down: Int32)

@_silgen_name("VideoHostCursorBytes")
private func VideoHostCursorBytes() -> UnsafePointer<UInt8>?

@_silgen_name("VideoGuestCursorHidesHost")
private func VideoGuestCursorHidesHost() -> Int32

@MainActor
final class GuestPaneView: NSView {
    let display = GuestDisplayView(frame: .zero)
    private var guestW: CGFloat = 1024
    private var guestH: CGFloat = 768

    override var safeAreaInsets: NSEdgeInsets { NSEdgeInsets() }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    var guestSize: NSSize { NSSize(width: guestW, height: guestH) }

    override func hitTest(_ point: NSPoint) -> NSView? {
        display.frame.contains(point) ? display : super.hitTest(point)
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        clipsToBounds = true
        layer?.backgroundColor = NSColor.black.cgColor
        display.frame = NSRect(x: 0, y: 0, width: guestW, height: guestH)
        addSubview(display)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    func setGuestSize(width: Int, height: Int) {
        guestW = CGFloat(max(width, 1))
        guestH = CGFloat(max(height, 1))
        display.setGuestSize(width: width, height: height)
        needsLayout = true
    }

    override func layout() {
        super.layout()
        guard guestW > 0, guestH > 0, bounds.width > 0, bounds.height > 0 else {
            display.frame = bounds
            return
        }
        let scale = min(bounds.width / guestW, bounds.height / guestH)
        let width = guestW * scale
        let height = guestH * scale
        display.frame = NSRect(
            x: bounds.midX - width / 2,
            y: bounds.midY - height / 2,
            width: width,
            height: height
        )
    }
}

@MainActor
final class GuestDisplayView: NSView {
    private var attached = false
    private var pointerInside = false
    private var modifiers: NSEvent.ModifierFlags = []
    private var tracking: NSTrackingArea?
    private var guestW: CGFloat = 1024
    private var guestH: CGFloat = 768
    private var edgeGrab = true
    private var macCursor: NSCursor?
    private var lastGX: Int32 = -1
    private var lastGY: Int32 = -1
    private var hidHost = false
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

    func setGuestSize(width: Int, height: Int) {
        guestW = CGFloat(max(width, 1))
        guestH = CGFloat(max(height, 1))
        lastGX = -1
        lastGY = -1
    }

    func setEdgeGrab(_ on: Bool) {
        edgeGrab = on
        hint.isHidden = on || !attached
        needsLayout = true
    }

    func applyMacCursor() {
        macCursor = Self.makeMacCursor()
        setHostCursor()
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
        updateTrackingAreas()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.acceptsMouseMovedEvents = true
    }

    override func resetCursorRects() {
        if PrefsBridge.bool("hardcursor") {
            addCursorRect(bounds, cursor: macCursor ?? .arrow)
            return
        }
        if VideoGuestCursorHidesHost() != 0 {
            addCursorRect(bounds, cursor: Self.blankCursor)
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking {
            removeTrackingArea(tracking)
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .inVisibleRect, .enabledDuringMouseDrag],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        tracking = area
    }

    override func mouseEntered(with event: NSEvent) {
        window?.makeFirstResponder(self)
        track(event)
        setHostCursor()
    }

    override func mouseExited(with event: NSEvent) {
        track(event)
        setHostCursor()
    }

    override func mouseMoved(with event: NSEvent) {
        window?.makeFirstResponder(self)
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
        window?.makeFirstResponder(self)
        press(event)
    }

    override func mouseUp(with event: NSEvent) {
        release(event)
    }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        press(event)
    }

    override func rightMouseUp(with event: NSEvent) {
        release(event)
    }

    override func otherMouseDown(with event: NSEvent) {
        press(event)
    }

    override func otherMouseUp(with event: NSEvent) {
        release(event)
    }

    override func keyDown(with event: NSEvent) {
        if !edgeGrab && event.keyCode == 5 && event.modifierFlags.contains(.control) {
            if modifiers.contains(.control) {
                VideoHostKey(0x3b, 0)
            }
            detach()
            return
        }
        VideoHostKey(Int32(event.keyCode), 1)
    }

    override func keyUp(with event: NSEvent) {
        VideoHostKey(Int32(event.keyCode), 0)
    }

    override func flagsChanged(with event: NSEvent) {
        let now = event.modifierFlags
        change(.shift, 0x38, now)
        change(.control, 0x3b, now)
        change(.option, 0x3a, now)
        change(.command, 0x37, now)
        change(.capsLock, 0x39, now)
        modifiers = now
    }

    private func change(_ flag: NSEvent.ModifierFlags, _ code: Int32, _ now: NSEvent.ModifierFlags) {
        let was = modifiers.contains(flag)
        let isOn = now.contains(flag)
        if was != isOn {
            VideoHostKey(code, isOn ? 1 : 0)
        }
    }

    private func attach() {
        if attached { return }
        attached = true
        hint.isHidden = edgeGrab
        setHostCursor()
    }

    private func detach() {
        if !attached { return }
        attached = false
        VideoHostMouseButton(0, 0)
        VideoHostMouseButton(1, 0)
        setHostCursor()
        hint.isHidden = true
    }

    private func setHostCursor() {
        window?.invalidateCursorRects(for: self)
    }

    private func press(_ event: NSEvent) {
        move(event)
        let which: Int32 = event.buttonNumber == 1 ? 1 : 0
        VideoHostMouseButton(which, 1)
    }

    private func release(_ event: NSEvent) {
        move(event)
        let which: Int32 = event.buttonNumber == 1 ? 1 : 0
        VideoHostMouseButton(which, 0)
    }

    /* Do not convert() on this CAMetalLayer view. UTM's display is the
     * content view (origin 0); ours sits to the right of a 220pt sidebar.
     * Metal convert(from: nil) drops that origin, so locationInWindow.x
     * 1024 looks like the right edge while the picture still has ~220pt
     * to go (1024-220)/1024 ≈ 78%. Use the pane, which is a plain NSView. */
    private func windowMouse(from event: NSEvent?) -> NSPoint? {
        guard let window else { return nil }
        return event?.locationInWindow ?? window.mouseLocationOutsideOfEventStream
    }

    private func pictureInWindow() -> NSRect? {
        guard window != nil, let pane = superview, frame.width > 1, frame.height > 1 else {
            return nil
        }
        return pane.convert(frame, to: nil)
    }

    private func inPicture(_ p: NSPoint, _ r: NSRect) -> Bool {
        p.x >= r.minX && p.y >= r.minY && p.x <= r.maxX && p.y <= r.maxY
    }

    private func mapped(_ p: NSPoint, _ r: NSRect) -> (Int32, Int32)? {
        guard r.width > 0, r.height > 0 else { return nil }
        var x = Int((p.x - r.minX) / r.width * guestW)
        var y = Int((r.maxY - p.y) / r.height * guestH)
        if x < 0 { x = 0 }
        if y < 0 { y = 0 }
        if x >= Int(guestW) { x = Int(guestW) - 1 }
        if y >= Int(guestH) { y = Int(guestH) - 1 }
        return (Int32(x), Int32(y))
    }

    private func applyHost(_ p: NSPoint, _ r: NSRect) {
        guard let (x, y) = mapped(p, r) else { return }
        if x == lastGX && y == lastGY { return }
        lastGX = x
        lastGY = y
        VideoHostMouseAbs(x, y)
    }

    private var grabLog = 0

    private func logGrab(_ tag: String, _ p: NSPoint, _ r: NSRect, _ inside: Bool) {
        if tag != "detach" {
            grabLog += 1
            if grabLog > 12 { return }
        }
        let metal = convert(p, from: nil)
        let paneX = superview?.frame.minX ?? 0
        print("NW-BOOT mouse \(tag) loc=\(Int(p.x)),\(Int(p.y)) pic=\(Int(r.minX)),\(Int(r.minY)) \(Int(r.width))x\(Int(r.height)) inside=\(inside ? 1 : 0) metal=\(Int(metal.x)),\(Int(metal.y)) bounds=\(Int(bounds.width))x\(Int(bounds.height)) paneX=\(Int(paneX)) guest=\(Int(guestW))x\(Int(guestH))")
    }

    private func track(_ event: NSEvent?) {
        guard let p = windowMouse(from: event), let r = pictureInWindow() else { return }
        let inside = inPicture(p, r)
        logGrab(attached && !inside ? "detach" : "move", p, r, inside)
        pointerInside = inside
        if edgeGrab {
            if attached && !inside {
                detach()
                return
            }
            if !attached && inside {
                attach()
            }
        }
        if attached || inside {
            applyHost(p, r)
        }
    }

    private func move(_ event: NSEvent) {
        track(event)
        let hide = VideoGuestCursorHidesHost() != 0
        if hide != hidHost {
            hidHost = hide
            setHostCursor()
        }
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
