/*
 *  LibraryImport.swift - Adding an existing prefs file to the VM library (pure: no UI, no disk access).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum LibraryImport {
    /// Prefs keys whose value is a file or folder path.
    static let pathKeys: Set<String> = ["disk", "cdrom", "floppy", "rom", "extfs"]

    /// The prefs text with every relative path made absolute against `dir`. The copy in the library lives in
    /// another folder, so a path that meant "next to the original prefs file" would otherwise stop working.
    static func absolutized(_ text: String, relativeTo dir: URL) -> String {
        let lines = text.components(separatedBy: "\n").map { line -> String in
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard !trimmed.isEmpty, !trimmed.hasPrefix("#") else { return line }
            let parts = trimmed.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
            guard parts.count == 2, pathKeys.contains(String(parts[0])) else { return line }
            let value = parts[1].trimmingCharacters(in: .whitespaces)
            if value.isEmpty || value.hasPrefix("/") || value.hasPrefix("~") || value.hasPrefix("*") {
                return line
            }
            return "\(parts[0]) \(dir.appendingPathComponent(value).standardized.path)"
        }
        return lines.joined(separator: "\n")
    }

    /// A library name for a prefs file: its folder's name when it is called "prefs", else its own name.
    static func suggestedName(for file: URL) -> String {
        let base = file.lastPathComponent
        if base == "prefs" {
            let folder = file.deletingLastPathComponent().lastPathComponent
            return folder.isEmpty || folder == "/" ? "Mac OS" : folder
        }
        let stem = file.deletingPathExtension().lastPathComponent
        return stem.isEmpty ? "Mac OS" : stem
    }

    /// True when the text looks like a SheepShaver prefs file (it names at least one of the keys every VM has).
    static func looksLikePrefs(_ text: String) -> Bool {
        for line in text.components(separatedBy: "\n") {
            let key = line.trimmingCharacters(in: .whitespaces).split(separator: " ", maxSplits: 1).first.map(String.init) ?? ""
            if ["disk", "rom", "ramsize", "screen", "jit", "extfs", "cdrom"].contains(key) { return true }
        }
        return false
    }
}
