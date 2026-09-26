/*
 *  VMLibraryView.swift - Sidebar list of VM documents.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import SwiftUI

struct VMLibraryView: View {
    @ObservedObject var store: VirtualMachineStore
    var onPlay: (VirtualMachineDocument) -> Void
    var onSelect: (VirtualMachineDocument) -> Void

    var body: some View {
        List(store.machines, selection: $store.selection) { machine in
            HStack {
                ClassicMacIcon()
                VStack(alignment: .leading, spacing: 2) {
                    Text(machine.name)
                    if machine.id == store.runningID {
                        Text("Running")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                Spacer()
                Button("Play") {
                    onPlay(machine)
                }
                .buttonStyle(.borderless)
            }
            .tag(machine.id)
        }
        .onChange(of: store.selection) { _, id in
            if let doc = store.document(id: id) {
                onSelect(doc)
            }
        }
    }
}

private struct ClassicMacIcon: View {
    var body: some View {
        if let url = Bundle.main.url(forResource: "ClassicMacOS", withExtension: "svg"),
           let image = NSImage(contentsOf: url) {
            Image(nsImage: image)
                .resizable()
                .scaledToFit()
                .frame(width: 20, height: 24)
        } else {
            Image(systemName: "desktopcomputer")
        }
    }
}
