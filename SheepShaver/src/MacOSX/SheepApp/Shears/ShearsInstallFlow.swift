/*
 *  ShearsInstallFlow.swift - "Install Sheep Shears in the guest": the sheet that explains it and adds the installer disk
 *  to a virtual machine's prefs. Used by the VM's own Guest menu and by the library window's Guest menu.
 *
 *  The installer is a small disk image bundled in the app. It is copied next to the other SheepShaver files and added
 *  to the VM's disks (read-only); after the guest next starts, the user opens the installer on it. A running guest
 *  cannot gain a disk, so the sheet says when it takes effect.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
enum ShearsInstallFlow {
    static func run(on window: NSWindow, prefsPath: String?, vmName: String) {
        let alert = NSAlert()
        alert.messageText = "Install Sheep Shears in the guest"
        alert.informativeText = "Sheep Shears releases the mouse at the screen edge and shares the clipboard with this Mac. Its installer is a small disk. It is added to this virtual machine; after the guest next starts, open “Install Sheep Shears” on the “Sheep Shears” disk and click Install. No restart is needed after that, and the disk can stay or be removed."
        alert.addButton(withTitle: "Add Installer Disk")
        alert.addButton(withTitle: "Show Disk in Finder")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            MainActor.assumeIsolated {
                switch response {
                case .alertFirstButtonReturn: addInstallerDisk(on: window, prefsPath: prefsPath, vmName: vmName)
                case .alertSecondButtonReturn:
                    if let url = try? SheepShearsInstaller.stagedCopy() { NSWorkspace.shared.activateFileViewerSelecting([url]) }
                default: break
                }
            }
        }
    }

    private static func addInstallerDisk(on window: NSWindow, prefsPath: String?, vmName: String) {
        func report(_ title: String, _ text: String) {
            let note = NSAlert()
            note.messageText = title
            note.informativeText = text
            note.beginSheetModal(for: window)
        }
        guard let prefsPath, !prefsPath.isEmpty else {
            return report("This virtual machine has no prefs file", "The installer disk is added to the prefs file the virtual machine was started with.")
        }
        do {
            let url = try SheepShearsInstaller.stagedCopy()
            let added = try SheepShearsInstaller.addDisk(url, toPrefsAt: prefsPath)
            report(added ? "The installer disk was added" : "The installer disk is already added",
                   added ? "Restart “\(vmName)” (shut it down and start it again). Then open “Install Sheep Shears” on the “Sheep Shears” disk."
                         : "Open “Install Sheep Shears” on the “Sheep Shears” disk in the guest.")
        } catch {
            report("The installer disk could not be added", error.localizedDescription)
        }
    }
}
