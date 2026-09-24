/*
 *  VMLibraryView.swift - VM list and display, laid out like UTM.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import SwiftUI

struct VMLibraryView: View {
    @State private var selection: String? = "Mac OS 9.2.1"
    @State private var showSettings = false

    var body: some View {
        NavigationSplitView {
            List(selection: $selection) {
                Label("Mac OS 9.2.1", systemImage: "desktopcomputer")
                    .tag("Mac OS 9.2.1")
            }
            .navigationSplitViewColumnWidth(min: 220, ideal: 260)
            .toolbar {
                ToolbarItem {
                    Button(action: {}) { Image(systemName: "plus") }
                }
            }
        } detail: {
            VStack(spacing: 0) {
                HStack {
                    Text(selection ?? "SheepShaver")
                        .font(.headline)
                    Spacer()
                    Button(action: { showSettings = true }) {
                        Image(systemName: "slider.horizontal.3")
                    }
                    .help("Settings")
                }
                .padding(12)
                DisplayView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(Color.black)
            }
        }
        .sheet(isPresented: $showSettings) {
            VMSettingsView()
                .frame(minWidth: 720, minHeight: 480)
        }
    }
}
