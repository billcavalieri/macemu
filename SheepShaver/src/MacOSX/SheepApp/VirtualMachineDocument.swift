/*
 *  VirtualMachineDocument.swift - VM folders in Application Support.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import Foundation

struct VirtualMachineDocument: Identifiable, Hashable, Sendable {
    let id: String
    var name: String
    var prefsPath: String
}

@MainActor
final class VirtualMachineStore: ObservableObject {
    @Published var machines: [VirtualMachineDocument] = []
    @Published var selection: String?
    @Published var runningID: String?

    private static var root: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("SheepShaver/VMs", isDirectory: true)
    }

    init() {
        reload()
    }

    func reload() {
        let fm = FileManager.default
        try? fm.createDirectory(at: Self.root, withIntermediateDirectories: true)
        var found: [VirtualMachineDocument] = []
        if let dirs = try? fm.contentsOfDirectory(at: Self.root, includingPropertiesForKeys: nil) {
            for dir in dirs where dir.hasDirectoryPath {
                let prefs = dir.appendingPathComponent("prefs")
                guard fm.fileExists(atPath: prefs.path) else { continue }
                let meta = dir.appendingPathComponent("vm.json")
                var name = dir.lastPathComponent
                if let data = try? Data(contentsOf: meta),
                   let json = try? JSONSerialization.jsonObject(with: data) as? [String: String],
                   let n = json["name"], !n.isEmpty {
                    name = n
                }
                found.append(VirtualMachineDocument(id: dir.lastPathComponent, name: name, prefsPath: prefs.path))
            }
        }
        found.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        machines = found
        if selection == nil {
            selection = runningID ?? found.first?.id
        }
    }

    func document(id: String?) -> VirtualMachineDocument? {
        machines.first { $0.id == id }
    }

    @discardableResult
    func create(name: String, disk: String, rom: String) -> VirtualMachineDocument {
        let id = UUID().uuidString
        let dir = Self.root.appendingPathComponent(id, isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = dir.appendingPathComponent("prefs")
        let body = """
        disk \(disk)
        rom \(rom)
        screen win/1024/768
        ramsize 536870912
        jit true
        sheepforce true
        qtcodec true
        edgegrab true
        gfxaccel true
        """
        try? body.write(to: prefs, atomically: true, encoding: .utf8)
        let meta = ["name": name]
        if let data = try? JSONSerialization.data(withJSONObject: meta) {
            try? data.write(to: dir.appendingPathComponent("vm.json"))
        }
        reload()
        selection = id
        return VirtualMachineDocument(id: id, name: name, prefsPath: prefs.path)
    }

    func markRunning(prefsPath: String) {
        if let existing = machines.first(where: { $0.prefsPath == prefsPath }) {
            runningID = existing.id
            selection = existing.id
        }
    }

    func launch(_ doc: VirtualMachineDocument) {
        if doc.id == runningID {
            return
        }
        let config = NSWorkspace.OpenConfiguration()
        config.arguments = ["--config", doc.prefsPath]
        config.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL, configuration: config) { _, _ in }
    }
}
