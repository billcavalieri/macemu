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

    /// Guest > Add SheepBench Disk: the benchmark app, on its own small disk, added like the installer disk.
    static func addSheepBench(on window: NSWindow, prefsPath: String?, vmName: String) {
        let alert = NSAlert()
        alert.messageText = "Add the SheepBench disk"
        alert.informativeText = "SheepBench measures Mac OS 9 in this emulator: the processor with the JIT on and off, memory, QuickDraw with SheepForce on and off, 3D and the disk, and compares the processor tests with this Mac running the same code. Its disk is added to this virtual machine; after the guest next starts, open SheepBench on the “SheepBench” disk. For the JIT and GPU comparisons, turn on “Allow SheepBench to change JIT and SheepForce while running” in the virtual machine's settings (Advanced)."
        alert.addButton(withTitle: "Add Disk")
        alert.addButton(withTitle: "Show Disk in Finder")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            MainActor.assumeIsolated {
                switch response {
                case .alertFirstButtonReturn:
                    addDisk(on: window, prefsPath: prefsPath, vmName: vmName, resource: SheepShearsInstaller.benchResource,
                            diskName: SheepShearsInstaller.benchDiskName, open: "SheepBench", disk: "SheepBench")
                case .alertSecondButtonReturn:
                    if let url = try? SheepShearsInstaller.stagedCopy(resource: SheepShearsInstaller.benchResource,
                                                                      diskName: SheepShearsInstaller.benchDiskName) {
                        NSWorkspace.shared.activateFileViewerSelecting([url])
                    }
                default: break
                }
            }
        }
    }

    private static func addInstallerDisk(on window: NSWindow, prefsPath: String?, vmName: String) {
        addDisk(on: window, prefsPath: prefsPath, vmName: vmName, resource: "SheepShearsInstaller",
                diskName: SheepShearsInstaller.diskName, open: "Install Sheep Shears", disk: "Sheep Shears")
    }

    private static func addDisk(on window: NSWindow, prefsPath: String?, vmName: String, resource: String,
                                diskName: String, open: String, disk: String) {
        func report(_ title: String, _ text: String) {
            let note = NSAlert()
            note.messageText = title
            note.informativeText = text
            note.beginSheetModal(for: window)
        }
        guard let prefsPath, !prefsPath.isEmpty else {
            return report("This virtual machine has no prefs file", "The disk is added to the prefs file the virtual machine was started with.")
        }
        do {
            let url = try SheepShearsInstaller.stagedCopy(resource: resource, diskName: diskName)
            let added = try SheepShearsInstaller.addDisk(url, toPrefsAt: prefsPath)
            report(added ? "The disk was added" : "The disk is already added",
                   added ? "Restart “\(vmName)” (shut it down and start it again). Then open “\(open)” on the “\(disk)” disk."
                         : "Open “\(open)” on the “\(disk)” disk in the guest.")
        } catch {
            report("The disk could not be added", error.localizedDescription)
        }
    }
}
