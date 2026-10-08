/*
 *  ShearsPointer.swift - Decides when a grabbed pointer should leave the guest picture.
 *
 *  The input is the guest's own pointer position (PTR_POS from the guest tool) and the host pointer's physical
 *  deltas, in guest orientation (positive x right, positive y down), the same numbers that are sent to the guest.
 *  A pure function of those and of time, so it runs in unit tests without a window or a mouse.
 *
 *  The pointer is released at an edge when all of these hold:
 *    - the guest says the arrow is on that edge, and the report is fresh;
 *    - it has been there for `dwell` seconds, so the tail of a flick that merely arrives at the edge (the menu
 *      bar, a window at the screen edge) does not release;
 *    - the host pointer then pushes outward, through that edge, by `threshold` points. Motion along the edge does
 *      not count, and the accumulated push leaks away at `leak` points per second, so a slow drift never releases;
 *    - no button is down (a drag stays in the guest).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum ShearsEdge: Int, CaseIterable {
    case left, right, top, bottom
}

struct ShearsEdgeDetector {
    /// Outward push, in host points, that releases. 0 turns edge release off.
    var threshold: Double = 40
    /// How long the arrow must sit on the edge before pushing counts.
    var dwell: Double = 0.12
    /// Points per second the accumulated push decays by.
    var leak: Double = 60
    /// A report older than this says nothing about where the arrow is now.
    var staleAfter: Double = 1.5

    private(set) var report: ShearsPointerReport?
    private var reportTime: Double = 0
    private var arrivedAt: [ShearsEdge: Double] = [:]
    private var push: [ShearsEdge: Double] = [:]
    private var lastTime: Double = 0

    static func edges(at r: ShearsPointerReport) -> Set<ShearsEdge> {
        var e = Set<ShearsEdge>()
        if r.x <= 0 { e.insert(.left) }
        if r.x >= r.width - 1 { e.insert(.right) }
        if r.y <= 0 { e.insert(.top) }
        if r.y >= r.height - 1 { e.insert(.bottom) }
        return e
    }

    /// Forget everything: a new grab, a release, or the tool going away.
    mutating func reset() {
        report = nil
        arrivedAt.removeAll()
        push.removeAll()
    }

    /// A new position from the guest tool.
    mutating func guest(_ r: ShearsPointerReport, now: Double) {
        let at = Self.edges(at: r)
        for e in ShearsEdge.allCases {
            if at.contains(e) {
                if arrivedAt[e] == nil { arrivedAt[e] = now }
            } else {
                arrivedAt[e] = nil
                push[e] = 0
            }
        }
        report = r
        reportTime = now
    }

    /// The host pointer moved by (dx, dy) in guest orientation. Returns the edge to release at, if any.
    mutating func host(dx: Double, dy: Double, now: Double) -> ShearsEdge? {
        let dt = max(0, now - lastTime)
        lastTime = now
        guard threshold > 0, let r = report, now - reportTime <= staleAfter, !r.buttonDown else { return nil }
        let at = Self.edges(at: r)
        for e in ShearsEdge.allCases {
            let leaked = max(0, (push[e] ?? 0) - leak * dt)
            guard at.contains(e), let since = arrivedAt[e], now - since >= dwell else {
                push[e] = leaked
                continue
            }
            let outward: Double
            switch e {
            case .left: outward = -dx
            case .right: outward = dx
            case .top: outward = -dy
            case .bottom: outward = dy
            }
            // Moving back inward cancels what was pushed; it does not go below zero.
            let total = max(0, leaked + outward)
            push[e] = total
            if total >= threshold {
                push.removeAll()
                return e
            }
        }
        return nil
    }
}
