/*
 *  VirtualMachineDocument.swift - VM folders in Application Support.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit
import Foundation

struct VirtualMachineDocument: Hashable, Sendable {
    let id: String
    var name: String
    var prefsPath: String
}

@MainActor
final class VirtualMachineStore {
    var machines: [VirtualMachineDocument] = []
    var selection: String?
    /// The VM this process is running (a process that booted a VM), else nil.
    var runningID: String?
    /// VMs this process (the manager) started, by id. They are separate processes with their own windows.
    private(set) var launched: [String: NSRunningApplication] = [:]
    /// Started a moment ago, process not reported yet: counts as running so a second start is refused at once.
    private var launching = Set<String>()
    private var terminationObserver: NSObjectProtocol?

    /// VMs found running by asking their control sockets (started by this manager, an earlier one, or by hand).
    private(set) var answering = Set<String>()

    /// Whether a VM the library starts shows in the library window (the default) or opens its own window.
    /// Settings key `SheepOpenInLibraryWindow`; NW_NO_EMBED=1 forces windows (tests, diagnostics only).
    static var opensInLibraryWindow: Bool {
        if nwDiagnosticsOn, ProcessInfo.processInfo.environment["NW_NO_EMBED"] == "1" { return false }
        return UserDefaults.standard.object(forKey: "SheepOpenInLibraryWindow") as? Bool ?? true
    }

    /// Whether a VM's display socket is there (it runs embedded and is up).
    static func hasDisplaySocket(_ id: String) -> Bool {
        FileManager.default.fileExists(atPath: DisplayPaths.socketPath(vmID: id))
    }

    /// Whether a VM is running, here or in a process the manager started.
    func isRunning(_ id: String) -> Bool {
        if id == runningID || launching.contains(id) || answering.contains(id) { return true }
        if let app = launched[id] { return !app.isTerminated }
        return false
    }

    /// Asks every library VM's control socket whether it answers (off the main thread), then tells the library
    /// when something changed. The manager calls this every couple of seconds while its window is open.
    func pollRunning() {
        let ids = machines.map(\.id)
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let found = Set(ids.filter { VMControlClient.isRunning(vmID: $0) })
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self, found != self.answering else { return }
                    self.answering = found
                    NotificationCenter.default.post(name: .vmRunStateChanged, object: nil)
                }
            }
        }
    }

    /// Asks a running VM to shut down: a clean Mac OS shutdown when the Sheep Shears tool is running in it. The
    /// completion gets nil on success, else the VM's own explanation (for example that the tool is not running).
    func shutDown(_ doc: VirtualMachineDocument, force: Bool, completion: @escaping @MainActor (String?) -> Void) {
        let id = doc.id
        DispatchQueue.global(qos: .userInitiated).async {
            var failure: String?
            do { _ = try VMControlClient.call(vmID: id, op: "shutdown", args: ["force": force], timeout: 10) }
            catch { failure = (error as? ControlError)?.message ?? error.localizedDescription }
            DispatchQueue.main.async { MainActor.assumeIsolated { completion(failure) } }
        }
    }

    /// Renames a VM in the library (its folder and disks are untouched).
    func rename(_ id: String, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, let dir = machines.first(where: { $0.id == id }).map({ URL(fileURLWithPath: $0.prefsPath).deletingLastPathComponent() }) else { return }
        let meta = dir.appendingPathComponent("vm.json")
        var json = (try? Data(contentsOf: meta)).flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: String] } ?? [:]
        json["name"] = trimmed
        if let data = try? JSONSerialization.data(withJSONObject: json) { try? data.write(to: meta) }
        reload()
    }

    /// Takes a VM out of the library: its folder (prefs and NVRAM) goes to the Trash. Disk images are never touched.
    func remove(_ id: String) throws {
        guard let doc = machines.first(where: { $0.id == id }) else { return }
        try FileManager.default.trashItem(at: URL(fileURLWithPath: doc.prefsPath).deletingLastPathComponent(), resultingItemURL: nil)
        if selection == id { selection = nil }
        reload()
    }

    private func watchTermination() {
        guard terminationObserver == nil else { return }
        terminationObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didTerminateApplicationNotification, object: nil, queue: .main) { [weak self] note in
            guard let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication else { return }
            MainActor.assumeIsolated {
                guard let self, let id = self.launched.first(where: { $0.value.processIdentifier == app.processIdentifier })?.key else { return }
                self.launched[id] = nil
                NotificationCenter.default.post(name: .vmRunStateChanged, object: nil, userInfo: ["id": id])
            }
        }
    }

    private static var root: URL {
        // Test runs (tools/vms/test.sh) keep their VMs elsewhere than the user's library; honoured only with diagnostics on
        if nwDiagnosticsOn, let dir = ProcessInfo.processInfo.environment["NW_VM_LIBRARY"], !dir.isEmpty {
            return URL(fileURLWithPath: dir, isDirectory: true)
        }
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
        var doc = PrefsDocument()
        doc.set("disk", disk)
        if !rom.isEmpty { doc.set("rom", rom) }         // empty: SheepShaver finds the "Mac OS ROM" file in the startup disk
        doc.set("screen", "win/1024/768")
        doc.set("ramsize", 536870912)
        doc.set("jit", true)
        doc.set("sheepforce", true)
        doc.set("qtcodec", true)
        doc.set("mouse", "absolute")
        doc.set("gfxaccel", true)
        try? doc.text.write(to: prefs, atomically: true, encoding: .utf8)
        let meta = ["name": name]
        if let data = try? JSONSerialization.data(withJSONObject: meta) {
            try? data.write(to: dir.appendingPathComponent("vm.json"))
        }
        reload()
        selection = id
        return VirtualMachineDocument(id: id, name: name, prefsPath: prefs.path)
    }

    enum ImportError: Error, LocalizedError {
        case notFound(String), notPrefs(String)
        var errorDescription: String? {
            switch self {
            case .notFound(let path): return "There is no prefs file at \(path)."
            case .notPrefs(let path): return "\(path) does not look like a SheepShaver prefs file (it has no disk, rom or screen line)."
            }
        }
    }

    /// Adds a VM that already has a prefs file (a file, or a folder holding one called "prefs") to the library. The
    /// prefs are copied into a new library folder, with relative paths made absolute, so the disks stay where they are.
    /// Adding the same file twice selects the entry made the first time.
    @discardableResult
    func addExisting(_ chosen: URL) throws -> VirtualMachineDocument {
        let fm = FileManager.default
        var file = chosen
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: file.path, isDirectory: &isDir) else { throw ImportError.notFound(chosen.path) }
        if isDir.boolValue { file = file.appendingPathComponent("prefs") }
        guard fm.fileExists(atPath: file.path), let text = try? String(contentsOf: file, encoding: .utf8) else {
            throw ImportError.notFound(file.path)
        }
        guard LibraryImport.looksLikePrefs(text) else { throw ImportError.notPrefs(file.path) }
        file = file.resolvingSymlinksInPath()
        // Already in the library, or imported before
        if let inLibrary = machines.first(where: { URL(fileURLWithPath: $0.prefsPath).resolvingSymlinksInPath() == file }) {
            selection = inLibrary.id
            return inLibrary
        }
        for dir in (try? fm.contentsOfDirectory(at: Self.root, includingPropertiesForKeys: nil)) ?? [] {
            if let data = try? Data(contentsOf: dir.appendingPathComponent("vm.json")),
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: String],
               json["source"] == file.path, let existing = machines.first(where: { $0.id == dir.lastPathComponent }) {
                selection = existing.id
                return existing
            }
        }
        let id = UUID().uuidString
        let dir = Self.root.appendingPathComponent(id, isDirectory: true)
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        let name = LibraryImport.suggestedName(for: file)
        try LibraryImport.absolutized(text, relativeTo: file.deletingLastPathComponent())
            .write(to: dir.appendingPathComponent("prefs"), atomically: true, encoding: .utf8)
        let meta = try JSONSerialization.data(withJSONObject: ["name": name, "source": file.path])
        try meta.write(to: dir.appendingPathComponent("vm.json"))
        reload()
        selection = id
        return document(id: id) ?? VirtualMachineDocument(id: id, name: name, prefsPath: dir.appendingPathComponent("prefs").path)
    }

    func markRunning(prefsPath: String) {
        if let existing = machines.first(where: { $0.prefsPath == prefsPath }) {
            runningID = existing.id
            selection = existing.id
        }
    }

    /// Starts the VM in its own process and window. Returns false when it is already running (that window is
    /// brought forward instead). `background` shows its window without taking the focus (used by remote clients).
    @discardableResult
    func launch(_ doc: VirtualMachineDocument, background: Bool = false, embedded: Bool = false,
                completion: (@MainActor (Bool) -> Void)? = nil) -> Bool {
        if doc.id == runningID {
            return false
        }
        if let app = launched[doc.id], !app.isTerminated {
            app.activate()
            return false
        }
        if launching.contains(doc.id) {
            return false
        }
        launching.insert(doc.id)
        watchTermination()
        let config = NSWorkspace.OpenConfiguration()
        config.activates = !background
        // --vm-dir: this VM's own folder holds its NVRAM, so VMs running at the same time do not share one file
        config.arguments = ["--config", doc.prefsPath, "--vm-id", doc.id,
                            "--vm-dir", URL(fileURLWithPath: doc.prefsPath).deletingLastPathComponent().path]
        if background { config.arguments.append("--background") }
        // Embedded: no window of its own; the library window shows the picture and takes the keyboard and mouse
        if embedded { config.arguments.append("--embedded") }
        if nwDiagnosticsOn {
            // Test runs: the VM inherits the diagnostic environment and writes its output to a file
            config.environment = ProcessInfo.processInfo.environment
            if let dir = ProcessInfo.processInfo.environment["NW_MANAGER_LOGDIR"] {
                config.arguments.append(contentsOf: ["--log", "\(dir)/\(doc.id).log"])
            }
        }
        config.createsNewApplicationInstance = true
        let id = doc.id
        nonisolated(unsafe) let store = self
        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL, configuration: config) { app, error in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    store.launching.remove(id)
                    if let app { store.launched[id] = app }
                    NotificationCenter.default.post(name: .vmRunStateChanged, object: nil, userInfo: ["id": id])
                    completion?(app != nil && error == nil)
                }
            }
        }
        return true
    }
}

extension Notification.Name {
    /// A VM started or stopped (userInfo["id"]): the library refreshes its rows.
    static let vmRunStateChanged = Notification.Name("SheepVMRunStateChanged")
}
