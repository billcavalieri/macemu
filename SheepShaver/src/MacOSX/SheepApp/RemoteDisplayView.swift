/*
 *  RemoteDisplayView.swift - A VM that runs in another process, shown in the library window. It is the same view the
 *  VM's own window uses (grab, Ctrl-G, edge release, the guest cursor) with a `RemoteGuestLink` instead of direct calls,
 *  and with its picture drawn from the VM's shared memory (sheepforce_remote.mm) on a display link.
 *
 *  The display link runs only while the view is on screen. Every tick tells the VM somebody is looking (a heartbeat in
 *  the shared memory) and draws the newest frame; paused, the heartbeat stops and the VM stops drawing, so a VM nobody
 *  is looking at costs nothing extra.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import QuartzCore

@_silgen_name("RemoteDisplayOpen")
private func RemoteDisplayOpen(_ name: UnsafePointer<CChar>) -> UnsafeMutableRawPointer?
@_silgen_name("RemoteDisplayClose")
private func RemoteDisplayClose(_ handle: UnsafeMutableRawPointer)
@_silgen_name("RemoteDisplayConfigureLayer")
private func RemoteDisplayConfigureLayer(_ handle: UnsafeMutableRawPointer, _ layer: UnsafeMutableRawPointer)
@_silgen_name("RemoteDisplayDraw")
private func RemoteDisplayDraw(_ handle: UnsafeMutableRawPointer, _ layer: UnsafeMutableRawPointer, _ interval: UInt64) -> Int32
@_silgen_name("RemoteDisplayGeometry")
private func RemoteDisplayGeometry(_ handle: UnsafeMutableRawPointer, _ width: UnsafeMutablePointer<Int32>, _ height: UnsafeMutablePointer<Int32>)
@_silgen_name("RemoteDisplayFramesDrawn")
private func RemoteDisplayFramesDrawn(_ handle: UnsafeMutableRawPointer) -> UInt64
@_silgen_name("RemoteDisplayNewestAgeNs")
private func RemoteDisplayNewestAgeNs(_ handle: UnsafeMutableRawPointer) -> UInt64

@MainActor
final class RemoteDisplayView: GuestDisplayView {
    let remoteLink: RemoteGuestLink
    /// Every event from the VM after this view has reacted to it (the library window shows the tool state, problems …).
    var onGuestEvent: ((DisplayEvent) -> Void)?
    private var remote: UnsafeMutableRawPointer?
    private var displayLink: CADisplayLink?
    private var openTimer: Timer?
    private var observers: [NSObjectProtocol] = []
    private let sharedMemoryName: String
    /// The VM has opened its own window (Detach): its picture is not here, and the heartbeat stops.
    private(set) var inOwnWindow = false

    init(link: RemoteGuestLink, vmID: String) {
        remoteLink = link
        sharedMemoryName = DisplayPaths.sharedMemoryName(vmID: vmID)
        super.init(frame: NSRect(x: 0, y: 0, width: 1024, height: 768))
        self.link = link
        inputEnabled = true
        let size = link.guestSize
        setGuestSize(width: Int(size.width), height: Int(size.height))
        if link.cursor != nil { applyMacCursor() }
        link.onEvent = { [weak self] event in self?.handle(event) }
        openSharedMemory()
        if remote == nil {
            // The VM creates the region a moment after its socket: look again until it is there
            openTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.openSharedMemory() }
            }
        }
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:)")
    }

    /// Frames drawn so far (diagnostics and tests).
    var framesDrawn: UInt64 { remote.map { RemoteDisplayFramesDrawn($0) } ?? 0 }
    var isShowingPicture: Bool { framesDrawn > 0 }

    private func openSharedMemory() {
        guard remote == nil, !remoteLink.closed else { return }
        guard let handle = sharedMemoryName.withCString({ RemoteDisplayOpen($0) }) else { return }
        remote = handle
        openTimer?.invalidate()
        openTimer = nil
        if let layer {
            RemoteDisplayConfigureLayer(handle, Unmanaged.passUnretained(layer).toOpaque())
        }
        startDisplayLink()
    }

    // MARK: display link

    private func startDisplayLink() {
        guard remote != nil, displayLink == nil else { return }
        let link = displayLink(target: self, selector: #selector(tick(_:)))
        link.add(to: .main, forMode: .common)
        displayLink = link
        updateRunning()
    }

    @objc private func tick(_ link: CADisplayLink) {
        guard let remote, let layer else { return }
        if layer.contentsScale != (window?.backingScaleFactor ?? 1) { layer.contentsScale = window?.backingScaleFactor ?? 1 }
        let interval = UInt64(max(link.targetTimestamp - link.timestamp, 1.0 / 240) * 1_000_000_000)
        let drew = RemoteDisplayDraw(remote, Unmanaged.passUnretained(layer).toOpaque(), interval)
        if nwDiagnosticsOn { noteTick(drew != 0, remote) }
        var w: Int32 = 0, h: Int32 = 0
        RemoteDisplayGeometry(remote, &w, &h)
        if w > 0, h > 0, CGFloat(w) != guestPixelSize.width || CGFloat(h) != guestPixelSize.height {
            setGuestSize(width: Int(w), height: Int(h))
            superview?.needsLayout = true
        }
    }

    // Diagnostics (NW_VERBOSE=1): once a second, ticks, frames drawn, and how old the frame was when it was drawn
    private var statStart = ProcessInfo.processInfo.systemUptime
    private var statTicks = 0, statDrawn = 0
    private var statAgeSum: UInt64 = 0, statAgeMax: UInt64 = 0
    private func noteTick(_ drew: Bool, _ remote: UnsafeMutableRawPointer) {
        statTicks += 1
        if drew {
            statDrawn += 1
            let age = RemoteDisplayNewestAgeNs(remote)
            statAgeSum += age
            statAgeMax = max(statAgeMax, age)
        }
        let now = ProcessInfo.processInfo.systemUptime
        if now - statStart >= 1 {
            fputs("DISPLAY ticks=\(statTicks) drawn=\(statDrawn) age_avg_us=\(statDrawn > 0 ? statAgeSum / UInt64(statDrawn) / 1000 : 0) age_max_us=\(statAgeMax / 1000)\n", stdout)
            fflush(stdout)
            statStart = now; statTicks = 0; statDrawn = 0; statAgeSum = 0; statAgeMax = 0
        }
    }

    /// On screen: the window is visible and not minimized and this view is not hidden. Otherwise the heartbeat stops.
    private func updateRunning() {
        let visible = window.map { $0.occlusionState.contains(.visible) && !$0.isMiniaturized } ?? false
        displayLink?.isPaused = !(visible && !isHiddenOrHasHiddenAncestor && !inOwnWindow)
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        observers.forEach { NotificationCenter.default.removeObserver($0) }
        observers = []
        guard let window else {
            updateRunning()
            return
        }
        for name in [NSWindow.didChangeOcclusionStateNotification, NSWindow.didMiniaturizeNotification, NSWindow.didDeminiaturizeNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: window, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.updateRunning() }
            })
        }
        applyMousePrefs()
        updateRunning()
    }

    override func viewDidHide() {
        super.viewDidHide()
        updateRunning()
    }

    override func viewDidUnhide() {
        super.viewDidUnhide()
        updateRunning()
    }

    // MARK: events from the VM

    private func handle(_ event: DisplayEvent) {
        switch event {
        case .guestMode(let w, let h, _, _):
            setGuestSize(width: Int(w), height: Int(h))
            superview?.needsLayout = true
        case .cursor:
            applyMacCursor()
        case .cursorHidesHost:
            window?.invalidateCursorRects(for: self)
        case .displayMode(let own):
            inOwnWindow = own
            inputEnabled = !own
            if own { releaseCapture("detached") }
            updateRunning()
        case .arrow(let x, let y, let visible):
            setGuestArrow(x: Int(x), y: Int(y), visible: visible)
        case .shearsPointer(let x, let y, let w, let h, let buttons):
            guestPointerReported(ShearsPointerReport(x: Int(x), y: Int(y), width: Int(w), height: Int(h), buttons: buttons),
                                 at: ProcessInfo.processInfo.systemUptime)
        default:
            break
        }
        onGuestEvent?(event)
    }

    /// Stops drawing and leaves the VM (it keeps running). The view is not used afterwards.
    func disconnect() {
        releaseCapture("disconnect")
        openTimer?.invalidate()
        openTimer = nil
        displayLink?.invalidate()
        displayLink = nil
        observers.forEach { NotificationCenter.default.removeObserver($0) }
        observers = []
        if let handle = remote { RemoteDisplayClose(handle) }
        remote = nil
        remoteLink.onEvent = nil
        remoteLink.close()
    }
}

/// Holds a VM's picture at its own proportions in the middle of whatever space there is, on black (what a monitor does).
@MainActor
final class DisplayAspectContainer: NSView {
    private(set) var display: RemoteDisplayView?
    /// Space the picture keeps clear of (the window's toolbar); the black still fills it.
    var insets = NSEdgeInsets() { didSet { needsLayout = true } }

    override var isOpaque: Bool { true }

    func show(_ view: RemoteDisplayView?) {
        guard view !== display else { return }
        display?.removeFromSuperview()
        display = view
        if let view {
            view.autoresizingMask = []
            addSubview(view)
        }
        needsLayout = true
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.black.setFill()
        dirtyRect.fill()
    }

    override func layout() {
        super.layout()
        guard let display else { return }
        let guest = display.guestPixelSize
        let area = NSRect(x: insets.left, y: insets.bottom, width: bounds.width - insets.left - insets.right,
                          height: bounds.height - insets.top - insets.bottom)
        guard guest.width >= 1, guest.height >= 1, area.width >= 1, area.height >= 1 else { return }
        let scale = min(area.width / guest.width, area.height / guest.height)
        let size = NSSize(width: (guest.width * scale).rounded(), height: (guest.height * scale).rounded())
        display.frame = NSRect(x: (area.minX + (area.width - size.width) / 2).rounded(),
                               y: (area.minY + (area.height - size.height) / 2).rounded(),
                               width: size.width, height: size.height)
    }
}
