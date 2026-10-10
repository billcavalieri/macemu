/*
 *  PrefsDocument.swift - A SheepShaver prefs file as a list of lines (pure: no UI, no disk access).
 *
 *  A prefs file is "key value" lines. Some keys repeat (several `disk` lines are several disks, in order) and a file
 *  may hold comments, blank lines and keys this app has no control for. Editing goes through this type so that
 *  everything it was not asked to change comes back exactly as it was: same order, same comments, every repeated line.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

struct PrefsDocument: Equatable {
    private enum Line: Equatable {
        case other(String)                  // a comment, a blank line, anything that is not "key value"
        case entry(key: String, value: String)
    }

    private var lines: [Line] = []

    init(text: String = "") {
        var parts = text.components(separatedBy: "\n")
        if parts.last == "" { parts.removeLast() }          // the newline that ends the file is not a blank line
        lines = parts.map { raw in
            let line = raw.hasSuffix("\r") ? String(raw.dropLast()) : raw
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty || trimmed.hasPrefix("#") { return .other(line) }
            let pieces = trimmed.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
            guard let key = pieces.first else { return .other(line) }
            let value = pieces.count > 1 ? pieces[1].trimmingCharacters(in: .whitespaces) : ""
            return .entry(key: String(key), value: value)
        }
    }

    /// Every value of a key, in file order (the disks of a VM, for example).
    func values(_ key: String) -> [String] {
        lines.compactMap { if case .entry(let k, let v) = $0, k == key { return v } else { return nil } }
    }

    /// Whether the file has the key at all.
    func has(_ key: String) -> Bool { !values(key).isEmpty }

    /// The value of a key that appears once. SheepShaver lets a later line replace an earlier one for such keys,
    /// so the last one counts. Nil when the key is not in the file.
    func value(_ key: String) -> String? { values(key).last }

    func string(_ key: String, _ fallback: String = "") -> String { value(key) ?? fallback }

    func bool(_ key: String, _ fallback: Bool = false) -> Bool {
        guard let raw = value(key)?.lowercased() else { return fallback }
        if raw.isEmpty { return true }          // "key" alone
        return raw == "true" || raw == "yes" || raw == "1"
    }

    func int(_ key: String, _ fallback: Int = 0) -> Int {
        Int(value(key) ?? "") ?? fallback
    }

    /// Makes the key have exactly these values, in this order. The first existing line of the key is where the new
    /// first value goes (so the key stays where the user put it); other old lines of the key are dropped; with
    /// no existing line the values are added at the end. An empty list removes the key.
    mutating func setValues(_ key: String, _ newValues: [String]) {
        var queue = newValues
        var out: [Line] = []
        var placed = false
        for line in lines {
            guard case .entry(let k, _) = line, k == key else { out.append(line); continue }
            if !placed {
                placed = true
                out.append(contentsOf: queue.map { .entry(key: key, value: $0) })
                queue.removeAll()
            }
        }
        if !placed { out.append(contentsOf: queue.map { .entry(key: key, value: $0) }) }
        lines = out
    }

    mutating func set(_ key: String, _ value: String) { setValues(key, [value]) }
    mutating func set(_ key: String, _ value: Bool) { setValues(key, [value ? "true" : "false"]) }
    mutating func set(_ key: String, _ value: Int) { setValues(key, ["\(value)"]) }
    mutating func remove(_ key: String) { setValues(key, []) }

    var text: String {
        lines.map { line -> String in
            switch line {
            case .other(let raw): return raw
            case .entry(let key, let value): return value.isEmpty ? "\(key) " : "\(key) \(value)"
            }
        }.joined(separator: "\n") + (lines.isEmpty ? "" : "\n")
    }
}
