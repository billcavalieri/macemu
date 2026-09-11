import Foundation

/// Classic Mac OS A-line / toolbox traps. Names come from cxmon `mon_atraps.h`
/// (`ATrapNames.table`, 1177 words, Traps.h order). `A97C` is GetNewDialog and
/// `A97D` is NewDialog; the old hand-written table that swapped GetCCursor onto
/// `A97C` is gone.
public enum ATrapTable {
    public static let names: [UInt16: String] = ATrapNames.table

    /// OS traps (A000-A7FF): bits 8-10 are flags (don't-save-A0, sys heap,
    /// immediate); the trap number is the low 8 bits. Toolbox traps
    /// (A800-AFFF): bit 10 is auto-pop; the trap number is the low 10 bits.
    public static func isOSTrap(_ trap: UInt16) -> Bool {
        trap & 0x0800 == 0
    }

    /// Canonical word without flag bits: `A1AD` -> `A0AD`, `AC68` -> `A868`.
    public static func canonical(_ trap: UInt16) -> UInt16 {
        if isOSTrap(trap) {
            return 0xA000 | (trap & 0x00FF)
        }
        return 0xA800 | (trap & 0x03FF)
    }

    /// Flag-insensitive name lookup: exact word first, then the canonical
    /// word, then (for OS traps) the flagged spellings cxmon lists
    /// (e.g. `Gestalt` is filed under `A1AD`).
    public static func lookupName(_ trap: UInt16) -> String? {
        if let exact = names[trap] { return exact }
        let base = canonical(trap)
        if let named = names[base] { return named }
        if isOSTrap(trap) {
            for flags: UInt16 in [0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700] {
                if let named = names[base | flags] { return named }
            }
        } else if let named = names[base | 0x400] {
            return named
        }
        return nil
    }

    /// Trap names as they appear in SheepShaver `G3: 68k …` log lines.
    public static let logTrapNames: [(String, UInt16)] = [
        ("GetNewDialog", 0xA97C),
        ("NewDialog", 0xA97D),
        ("Launch", 0xA9F2),
        ("LoadSeg", 0xA9F0),
        ("GetCCursor", 0xAA1B),
        ("DialogDispatch", 0xAA68),
        ("DisposeDialog", 0xA983),
        ("OpenResFile", 0xA06E),
        ("GetResource", 0xA9A0),
        ("SysError", 0xA9C9),
        ("InitCursor", 0xA88F),
        ("SetPort", 0xA873),
        ("CloseRgn", 0xA8D9),
        ("GetEOF", 0xA011),
        ("GetFPos", 0xA018),
        ("SetFPos", 0xA044),
        ("Read", 0xA002),
        ("HOpen", 0xA200),
        ("CodeFragmentDispatch", 0xAA5A),
        ("FixMul", 0xA868),
        ("DisposePtr", 0xA01F),
        ("ModalDialog", 0xA991)
    ]

    public static func name(for trap: UInt16) -> String {
        if let named = lookupName(trap) {
            return "_\(named)"
        }
        return String(format: "_A%03X", trap & 0x0FFF)
    }

    /// Bare name (no `_` prefix) or `A%03X`.
    public static func bareName(for trap: UInt16) -> String {
        lookupName(trap) ?? String(format: "A%03X", trap & 0x0FFF)
    }

    /// Resolve trap opcode for mill logs. Prefers `op=` / trailing `A97C` over mislabeled names
    /// (macemu still prints `GetCCursor A97C`; `$A97C` is GetNewDialog per Traps.h / mill_apply).
    public static func millApplyTrap(in line: String) -> UInt16? {
        if let groups = firstMatchGroups(in: line, pattern: #"\bop=([0-9a-fA-F]+)"#),
           groups.count >= 1,
           let op = parseHex(groups[0]) {
            return UInt16(truncatingIfNeeded: op)
        }
        if let opcode = logTrapOpcode(in: line) {
            if opcode == 0xA9F2, line.localizedCaseInsensitiveContains("pef ") {
                return trapNameFromLogLabel(in: line)
            }
            return opcode
        }
        return trapNameFromLogLabel(in: line)
    }

    public static func trapName(in line: String) -> UInt16? {
        millApplyTrap(in: line)
    }

    /// macemu-correct trap name for a log opcode (without Ghidra `_` prefix).
    public static func millApplyTrapName(for trap: UInt16) -> String {
        bareName(for: trap)
    }

    private static func logTrapOpcode(in line: String) -> UInt16? {
        guard let regex = try? NSRegularExpression(
            pattern: #" (A[0-9A-Fa-f]{3})(?:\s|$)"#,
            options: []
        ) else {
            return nil
        }
        let range = NSRange(line.startIndex..., in: line)
        let matches = regex.matches(in: line, options: [], range: range)
        guard let last = matches.last, last.numberOfRanges > 1,
              let swiftRange = Range(last.range(at: 1), in: line),
              let value = UInt16(String(line[swiftRange]), radix: 16),
              value >= 0xA000 else {
            return nil
        }
        return value
    }

    private static func trapNameFromLogLabel(in line: String) -> UInt16? {
        for (name, trap) in logTrapNames where line.contains(name) {
            if trap == 0xA9F2, line.localizedCaseInsensitiveContains("pef ") {
                continue
            }
            return trap
        }
        return nil
    }

    private static func firstMatchGroups(in text: String, pattern: String) -> [String]? {
        guard let regex = try? NSRegularExpression(pattern: pattern, options: [.caseInsensitive]) else { return nil }
        let range = NSRange(text.startIndex..., in: text)
        guard let match = regex.firstMatch(in: text, options: [], range: range) else { return nil }
        return (1..<match.numberOfRanges).compactMap { index in
            guard let swiftRange = Range(match.range(at: index), in: text) else { return nil }
            return String(text[swiftRange])
        }
    }

    private static func parseHex(_ text: String) -> UInt64? {
        UInt64(text, radix: 16)
    }

    public static var noSkipUITraps: Set<UInt16> {
        [
            0xA97C, 0xA97D, 0xAA1B, 0xAA68, 0xA873, 0xA983, 0xA8D9,
            0xA06E, 0xA9C9, 0xA9A0, 0xA88F, 0xA991,
            0xA01F, 0xA023, 0xA044, 0xA002, 0xA450
        ]
    }
}

public enum AddressSpaces {
    /// SheepShaver / `rom_disasm.py` mapping for the 4 MB NewWorld MacROM.
    public static let ppcMacROMBase: UInt64 = 0x5000_0000
    public static let ppcMacROMSize: UInt64 = 0x40_0000
    /// GetNewDialog ROM overlay stub (trap A97C; mill `UI_SKIP_68K_LO`).
    public static let getNewDialogStub: UInt64 = 0x5C86C
    public static let getNewDialogStubEnd: UInt64 = 0x5C8C0
}
