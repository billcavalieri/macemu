/*
 *  DisplayView.swift - Guest picture inside the SwiftUI window.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import SwiftUI

struct DisplayView: NSViewRepresentable {
    func makeNSView(context: Context) -> GuestHostView {
        let view = GuestHostView()
        SheepScreen.host = view
        return view
    }

    func updateNSView(_ nsView: GuestHostView, context: Context) {
        SheepScreen.host = nsView
        nsView.showFramebuffer()
    }
}
