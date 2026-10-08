/*
 *  GuestInput.swift - What the control channel needs to type and to move the guest pointer: US-layout key codes, key
 *  names, and the step planner that walks the pointer to a target.
 *
 *  Pure Foundation: compiled on its own by tools/vms/tests/unit.sh. Key codes are the ADB codes the emulator's
 *  VideoHostKey takes (the classic Mac virtual key codes, with the arrow and Control keys at their ADB values).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum GuestKeys {
    static let command = 0x37, shift = 0x38, option = 0x3a, control = 0x36

    /// One key press: the code, and whether Shift is held for it.
    struct Stroke: Equatable {
        var code: Int
        var shift: Bool
    }

    // index = key code 0x00...0x32
    private static let plain = Array("asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.\0 `".utf8)
    private static let shifted = Array("ASDFHGZXCV\0BQWERYT!@#$^%+(&_*)}OU{IP\0LJ\"K:|<?NM>\0\0~".utf8)

    /// The key for a printable US-layout character (and Return for a line break, Tab for a tab). Nil if it cannot be typed.
    static func stroke(for character: Character) -> Stroke? {
        if character == "\n" || character == "\r" { return Stroke(code: 0x24, shift: false) }
        if character == "\t" { return Stroke(code: 0x30, shift: false) }
        guard character.isASCII, let byte = character.asciiValue, byte >= 0x20 else { return nil }
        for i in 0..<0x33 where plain[i] != 0 && plain[i] == byte { return Stroke(code: i, shift: false) }
        for i in 0..<0x33 where shifted[i] != 0 && shifted[i] == byte { return Stroke(code: i, shift: true) }
        return nil
    }

    private static let names: [String: Int] = [
        "return": 0x24, "enter": 0x4c, "tab": 0x30, "space": 0x31, "delete": 0x33, "backspace": 0x33, "escape": 0x35, "esc": 0x35,
        "forwarddelete": 0x75, "home": 0x73, "end": 0x77, "pageup": 0x74, "pagedown": 0x79, "help": 0x72,
        "left": 0x3b, "right": 0x3c, "down": 0x3d, "up": 0x3e,
        "f1": 0x7a, "f2": 0x78, "f3": 0x63, "f4": 0x76, "f5": 0x60, "f6": 0x61, "f7": 0x62, "f8": 0x64,
        "f9": 0x65, "f10": 0x6d, "f11": 0x67, "f12": 0x6f,
    ]

    /// A named key ("return", "left", "f5") or a single character; nil when unknown.
    static func code(forKey name: String) -> Int? {
        let lower = name.lowercased()
        if let code = names[lower] { return code }
        if name.count == 1, let s = stroke(for: name.first!), !s.shift { return s.code }
        if name.count == 1, let s = stroke(for: Character(name.lowercased())) { return s.code }
        return nil
    }

    private static let modifierCodes: [String: Int] = [
        "command": command, "cmd": command, "shift": shift, "option": option, "opt": option, "alt": option,
        "control": control, "ctrl": control,
    ]

    static func modifierCode(_ name: String) -> Int? { modifierCodes[name.lowercased()] }
}

/// Walks the guest pointer to a target in a loop: send a relative move, read where the pointer ended up, repeat. The
/// guest applies its own acceleration to every report, so the planner sends proportionally smaller steps near the target
/// and single pixels at the end.
enum MouseStepPlanner {
    static let tolerance = 1
    static let farStep = 48         // largest report while far from the target
    static let nearDistance = 10    // inside this, steps shrink to single pixels

    /// The next relative move, or nil when the pointer is at the target (within `tolerance`).
    static func next(from: (x: Int, y: Int), to: (x: Int, y: Int)) -> (dx: Int, dy: Int)? {
        let ex = to.x - from.x, ey = to.y - from.y
        if abs(ex) <= tolerance && abs(ey) <= tolerance { return nil }
        func step(_ e: Int) -> Int {
            let magnitude = abs(e)
            if magnitude <= tolerance { return 0 }
            let limit = magnitude <= nearDistance ? 1 : min(farStep, max(2, magnitude / 2))
            return e > 0 ? limit : -limit
        }
        return (step(ex), step(ey))
    }
}
