/*
 *  PrefsBridge.swift - Prefs C API on the main actor.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

@_silgen_name("SheepPrefsGetString")
private func SheepPrefsGetString(_ key: UnsafePointer<CChar>) -> UnsafePointer<CChar>
@_silgen_name("SheepPrefsSetString")
private func SheepPrefsSetString(_ key: UnsafePointer<CChar>, _ value: UnsafePointer<CChar>)
@_silgen_name("SheepPrefsGetBool")
private func SheepPrefsGetBool(_ key: UnsafePointer<CChar>) -> Int32
@_silgen_name("SheepPrefsSetBool")
private func SheepPrefsSetBool(_ key: UnsafePointer<CChar>, _ value: Int32)
@_silgen_name("SheepPrefsGetInt")
private func SheepPrefsGetInt(_ key: UnsafePointer<CChar>) -> Int32
@_silgen_name("SheepPrefsSetInt")
private func SheepPrefsSetInt(_ key: UnsafePointer<CChar>, _ value: Int32)
@_silgen_name("SheepPrefsSave")
private func SheepPrefsSave()
@_silgen_name("SheepPrefsPath")
private func SheepPrefsPath() -> UnsafePointer<CChar>

@MainActor
enum PrefsBridge {
    static func string(_ key: String) -> String {
        key.withCString { String(cString: SheepPrefsGetString($0)) }
    }

    static func setString(_ key: String, _ value: String) {
        key.withCString { keyPtr in
            value.withCString { SheepPrefsSetString(keyPtr, $0) }
        }
    }

    static func bool(_ key: String) -> Bool {
        key.withCString { SheepPrefsGetBool($0) != 0 }
    }

    static func setBool(_ key: String, _ value: Bool) {
        key.withCString { SheepPrefsSetBool($0, value ? 1 : 0) }
    }

    static func int(_ key: String) -> Int {
        key.withCString { Int(SheepPrefsGetInt($0)) }
    }

    static func setInt(_ key: String, _ value: Int) {
        key.withCString { SheepPrefsSetInt($0, Int32(value)) }
    }

    static func save() {
        SheepPrefsSave()
    }

    static func path() -> String {
        String(cString: SheepPrefsPath())
    }
}

enum PrefsFile {
    static func load(_ path: String) -> [String: String] {
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return [:] }
        var out: [String: String] = [:]
        for line in text.split(whereSeparator: \.isNewline) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty || trimmed.hasPrefix("#") { continue }
            let parts = trimmed.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
            guard let key = parts.first else { continue }
            out[String(key)] = parts.count > 1 ? String(parts[1]) : "true"
        }
        return out
    }

    static func save(_ path: String, _ values: [String: String]) {
        var existing = load(path)
        for (key, value) in values {
            existing[key] = value
        }
        let body = existing.keys.sorted().map { "\($0) \(existing[$0] ?? "")" }.joined(separator: "\n") + "\n"
        try? body.write(toFile: path, atomically: true, encoding: .utf8)
    }

    static func string(_ values: [String: String], _ key: String, _ fallback: String = "") -> String {
        values[key] ?? fallback
    }

    static func bool(_ values: [String: String], _ key: String, _ fallback: Bool = false) -> Bool {
        guard let raw = values[key]?.lowercased() else { return fallback }
        return raw == "true" || raw == "yes" || raw == "1"
    }

    static func int(_ values: [String: String], _ key: String, _ fallback: Int = 0) -> Int {
        Int(values[key] ?? "") ?? fallback
    }
}
