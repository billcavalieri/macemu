/*
 *  ShearsInstaller.swift - Host side of "Guest > Install Sheep Shears": the installer disk bundled in the app is
 *  copied next to the other SheepShaver files and added to a virtual machine's prefs as a read-only disk.
 *
 *  Foundation only: compiled on its own by tools/shears/test.sh unit.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum SheepShearsInstaller {
    struct Failure: LocalizedError {
        var errorDescription: String?
    }

    static let diskName = "Sheep Shears Installer.hfv"

    /// The prefs text with `disk *<path>` added, or nil when that disk is already listed (read-only or not).
    static func prefsText(_ text: String, adding path: String) -> String? {
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let words = line.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
            guard words.count == 2, words[0] == "disk" else { continue }
            var listed = words[1].trimmingCharacters(in: .whitespaces)
            if listed.hasPrefix("*") { listed.removeFirst() }
            if listed == path { return nil }
        }
        var out = text
        if !out.isEmpty && !out.hasSuffix("\n") { out += "\n" }
        out += "disk *\(path)\n"
        return out
    }

    /// Adds the disk to the prefs file. False when it was already there.
    static func addDisk(_ disk: URL, toPrefsAt prefsPath: String) throws -> Bool {
        let text = (try? String(contentsOfFile: prefsPath, encoding: .utf8)) ?? ""
        guard let updated = prefsText(text, adding: disk.path) else { return false }
        try updated.write(toFile: prefsPath, atomically: true, encoding: .utf8)
        return true
    }

    /// The bundled disk copied to ~/Library/Application Support/SheepShaver, replaced when the app has a newer one.
    static func stagedCopy(bundle: Bundle = .main) throws -> URL {
        guard let source = bundle.url(forResource: "SheepShearsInstaller", withExtension: "hfv") else {
            throw Failure(errorDescription: "This copy of SheepShaver does not contain the Sheep Shears installer disk.")
        }
        let fm = FileManager.default
        let folder = try fm.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
            .appendingPathComponent("SheepShaver", isDirectory: true)
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        let destination = folder.appendingPathComponent(diskName)
        if fm.contentsEqual(atPath: source.path, andPath: destination.path) { return destination }
        let temporary = folder.appendingPathComponent(diskName + ".new")
        try? fm.removeItem(at: temporary)
        try fm.copyItem(at: source, to: temporary)
        _ = try fm.replaceItemAt(destination, withItemAt: temporary)
        return destination
    }
}
