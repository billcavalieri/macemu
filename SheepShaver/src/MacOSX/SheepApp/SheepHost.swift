/*
 *  SheepHost.swift - AppKit host: one window, Metal scanout, VM library.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import SwiftUI

@MainActor
enum BootGate {
    static var path: String?
    static var quit = false
}

@MainActor
@objc(SheepHost)
final class SheepHost: NSObject {
    private static var controller: SheepWindowController?

    @objc class func waitForConfig() -> UnsafePointer<CChar>? {
        NSApplication.shared.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        if NSApp.mainMenu == nil {
            NSApp.finishLaunching()
        }
        let wc = ensureWindow()
        wc.showWindow(nil)
        wc.sizeWindowToGuest()
        var chosen: String?
        while chosen == nil {
            if BootGate.quit {
                NSApp.terminate(nil)
                return nil
            }
            if let event = NSApp.nextEvent(
                matching: .any,
                until: Date.distantFuture,
                inMode: .default,
                dequeue: true
            ) {
                NSApp.sendEvent(event)
            }
            chosen = BootGate.path
        }
        return chosen.flatMap { strdup($0) }.map { UnsafePointer($0) }
    }

    @objc class func show(withWidth width: Int32, height: Int32) -> NSView {
        if !Thread.isMainThread {
            var view: NSView?
            DispatchQueue.main.sync {
                view = show(withWidth: width, height: height)
            }
            return view!
        }
        NSApplication.shared.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        if NSApp.mainMenu == nil {
            NSApp.finishLaunching()
        }
        let wc = ensureWindow()
        wc.booted = true
        wc.pane.setGuestSize(width: Int(width), height: Int(height))
        wc.display.setEdgeGrab(PrefsBridge.bool("edgegrab"))
        wc.showWindow(nil)
        wc.sizeWindowToGuest()
        wc.window?.acceptsMouseMovedEvents = true
        wc.window?.makeFirstResponder(wc.display)
        wc.window?.layoutIfNeeded()
        DispatchQueue.main.async {
            wc.sizeWindowToGuest()
            wc.window?.makeFirstResponder(wc.display)
        }
        let path = PrefsBridge.path()
        if !path.isEmpty {
            BootGate.path = path
            wc.store.markRunning(prefsPath: path)
        }
        if let name = wc.store.document(id: wc.store.runningID ?? wc.store.selection)?.name {
            wc.setSubtitle(name)
        } else if !path.isEmpty {
            wc.setSubtitle(URL(fileURLWithPath: path).deletingLastPathComponent().lastPathComponent)
        }
        return wc.display
    }

    @objc class func setGuestWidth(_ width: Int32, height: Int32) {
        hop {
            controller?.pane.setGuestSize(width: Int(width), height: Int(height))
        }
    }

    @objc class func applyMacCursor() {
        hop {
            controller?.display.applyMacCursor()
        }
    }

    @objc class func reloadEdgeGrab() {
        hop {
            controller?.display.setEdgeGrab(PrefsBridge.bool("edgegrab"))
        }
    }

    private static func ensureWindow() -> SheepWindowController {
        if let controller {
            return controller
        }
        let wc = SheepWindowController()
        controller = wc
        return wc
    }

    private static func hop(_ body: @escaping @MainActor () -> Void) {
        if Thread.isMainThread {
            MainActor.assumeIsolated(body)
        } else {
            DispatchQueue.main.async {
                MainActor.assumeIsolated(body)
            }
        }
    }
}
