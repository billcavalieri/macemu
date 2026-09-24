/*
 *  SheepHost.swift - The Mac window. SwiftUI replaces the old SheepShaver window.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import QuartzCore
import SwiftUI

@_silgen_name("VideoHostFramebuffer")
private func VideoHostFramebuffer(_ w: UnsafeMutablePointer<Int32>, _ h: UnsafeMutablePointer<Int32>, _ row: UnsafeMutablePointer<Int32>) -> UnsafeRawPointer?

final class SheepScreen {
    nonisolated(unsafe) static var guest: NSView?
    nonisolated(unsafe) static var window: NSWindow?
    nonisolated(unsafe) static weak var host: GuestHostView?
}

final class GuestHostView: NSImageView {
    override var isOpaque: Bool { true }

    func showFramebuffer() {
        var w: Int32 = 0
        var h: Int32 = 0
        var row: Int32 = 0
        guard let base = VideoHostFramebuffer(&w, &h, &row), w > 0, h > 0, row > 0 else { return }
        let byteCount = Int(row * h)
        var opaque = Data(count: byteCount)
        opaque.withUnsafeMutableBytes { dest in
            guard let out = dest.baseAddress else { return }
            memcpy(out, base, byteCount)
            var p = out.assumingMemoryBound(to: UInt8.self)
            var i = 0
            while i < byteCount {
                p[i] = 255
                i += 4
            }
        }
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)
        guard let provider = CGDataProvider(data: opaque as CFData),
              let cg = CGImage(width: Int(w), height: Int(h), bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(row), space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: info, provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return }
        imageScaling = .scaleAxesIndependently
        image = NSImage(cgImage: cg, size: NSSize(width: Int(w), height: Int(h)))
        needsDisplay = true
        displayIfNeeded()
        CATransaction.flush()
    }
}

private extension NSView {
    func firstGuestHost() -> GuestHostView? {
        if let guest = self as? GuestHostView { return guest }
        for child in subviews {
            if let guest = child.firstGuestHost() { return guest }
        }
        return nil
    }
}

@objc(SheepHost)
final class SheepHost: NSObject {
    @objc(showWithDisplay:)
    static func show(display: NSView) {
        SheepScreen.guest = display
        let host = NSHostingController(rootView: VMLibraryView())
        let window = NSWindow(contentViewController: host)
        window.title = "SheepShaver"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.styleMask.insert(.fullSizeContentView)
        window.setContentSize(NSSize(width: 1100, height: 740))
        window.isReleasedWhenClosed = false
        window.delegate = QuitOnClose.shared
        SheepScreen.window = window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc static func redraw() {
        if SheepScreen.host == nil {
            SheepScreen.host = SheepScreen.window?.contentView?.firstGuestHost()
        }
        guard let host = SheepScreen.host else { return }
        if Thread.isMainThread {
            host.showFramebuffer()
        } else {
            DispatchQueue.main.async { host.showFramebuffer() }
        }
    }
}

private final class QuitOnClose: NSObject, NSWindowDelegate {
    nonisolated(unsafe) static let shared = QuitOnClose()

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        VideoHostRequestQuit()
        return true
    }
}

@_silgen_name("VideoHostRequestQuit")
private func VideoHostRequestQuit()
