import Foundation

/// ROM trap dispatch table of a SuperMario (68k Toolbox) ROM image.
///
/// The ROM header long at `0x22` (`DispTableOff`) points at the table
/// `InitDispatcher` uses to fill the OS (256) and Toolbox (1024) trap tables.
/// Two encodings exist:
///
/// - **Long form** (Mac OS 8.5+ New World ROMs): 1024 Toolbox longs for
///   `A800…ABFF` followed by 256 OS longs for `A000…A0FF`; each long is a ROM
///   offset, 0 = not in ROM. The table ends exactly at `RomRsrc` (header `0x1A`).
/// - **Packed form** (older ROMs, decoded as in BasiliskII `find_rom_trap`):
///   one byte per trap, `0x80` unimplemented, `0xFF` + 4-byte absolute offset,
///   high-bit set = 7-bit delta ×2, otherwise 15-bit signed delta ×2.
///
/// Result: 68k ROM offset of each trap's implementation, i.e. the address the
/// emulator jumps to when `r24` lands in the Toolbox after an A-line dispatch.
public enum SuperMarioTrapTable {
    public struct Entry: Sendable, Equatable {
        public var trap: UInt16
        public var romOffset: UInt32?

        public var name: String { ATrapTable.bareName(for: trap) }
    }

    public struct Table: Sendable, Equatable {
        public var encoding: String
        public var tableOffset: Int
        public var entries: [Entry]

        public var implemented: [Entry] { entries.filter { $0.romOffset != nil } }

        public func entry(for trap: UInt16) -> Entry? {
            let canonical = ATrapTable.canonical(trap)
            return entries.first { $0.trap == canonical }
        }

        /// Trap word whose implementation starts at `romOffset`, if any.
        public func trap(at romOffset: UInt32) -> Entry? {
            entries.first { $0.romOffset == romOffset }
        }

        /// Trap whose implementation contains `romOffset` (closest start at or below).
        public func trap(containing romOffset: UInt32) -> Entry? {
            implemented
                .filter { $0.romOffset! <= romOffset }
                .max { $0.romOffset! < $1.romOffset! }
        }
    }

    public static func decode(_ rom: Data) -> Table? {
        guard rom.count >= 0x44 else { return nil }
        guard let dispOff = try? Int(BinaryCursor.u32be(rom, 0x22)),
              let romRsrc = try? Int(BinaryCursor.u32be(rom, 0x1A)),
              dispOff > 0, dispOff < rom.count
        else { return nil }

        if dispOff + 0x1400 <= rom.count, dispOff + 0x1400 == romRsrc || looksLikeLongTable(rom, at: dispOff) {
            return decodeLong(rom, at: dispOff)
        }
        return decodePacked(rom, at: dispOff)
    }

    private static func looksLikeLongTable(_ rom: Data, at offset: Int) -> Bool {
        var plausible = 0
        for i in 0..<64 {
            guard let value = try? BinaryCursor.u32be(rom, offset + i * 4) else { return false }
            if value == 0 || (value & 1 == 0 && Int(value) < rom.count) {
                plausible += 1
            }
        }
        return plausible == 64
    }

    private static func decodeLong(_ rom: Data, at offset: Int) -> Table? {
        var entries: [Entry] = []
        entries.reserveCapacity(1280)
        for i in 0..<1024 {
            guard let value = try? BinaryCursor.u32be(rom, offset + i * 4) else { return nil }
            entries.append(Entry(trap: 0xA800 + UInt16(i), romOffset: value == 0 ? nil : value))
        }
        for i in 0..<256 {
            guard let value = try? BinaryCursor.u32be(rom, offset + 4096 + i * 4) else { return nil }
            entries.append(Entry(trap: 0xA000 + UInt16(i), romOffset: value == 0 ? nil : value))
        }
        return Table(encoding: "long", tableOffset: offset, entries: entries)
    }

    private static func decodePacked(_ rom: Data, at offset: Int) -> Table? {
        var entries: [Entry] = []
        var bp = offset
        var ofs: UInt32 = 0
        for (first, count) in [(UInt16(0xA800), 1024), (UInt16(0xA000), 256)] {
            for i in 0..<count {
                guard bp < rom.count else { return nil }
                let b = rom[rom.startIndex + bp]
                bp += 1
                var unimplemented = false
                if b == 0x80 {
                    unimplemented = true
                } else if b == 0xFF {
                    guard let value = try? BinaryCursor.u32be(rom, bp) else { return nil }
                    ofs = value
                    bp += 4
                } else if b & 0x80 != 0 {
                    let add = UInt32(b & 0x7F) << 1
                    if add == 0 { return finish(entries, offset) }
                    ofs &+= add
                } else {
                    guard bp < rom.count else { return nil }
                    let raw = (UInt16(b) << 8 | UInt16(rom[rom.startIndex + bp])) << 1
                    bp += 1
                    let add = Int16(bitPattern: raw)
                    if add == 0 { return finish(entries, offset) }
                    ofs = UInt32(bitPattern: Int32(bitPattern: ofs) &+ Int32(add))
                }
                entries.append(Entry(trap: first + UInt16(i), romOffset: unimplemented ? nil : ofs))
            }
        }
        return finish(entries, offset)
    }

    private static func finish(_ entries: [Entry], _ offset: Int) -> Table? {
        entries.isEmpty ? nil : Table(encoding: "packed", tableOffset: offset, entries: entries)
    }

    /// `Romfile`-style listing: one line per implemented trap, ROM offset order.
    public static func listing(_ table: Table) -> String {
        var lines = [
            "# SuperMario ROM trap dispatch table (\(table.encoding), at \(BinaryCursor.hex(table.tableOffset)))",
            "# trap  name  68k-rom-offset",
            ""
        ]
        let sorted = table.implemented.sorted { $0.romOffset! < $1.romOffset! }
        for entry in sorted {
            lines.append(String(format: "%04X  %@  0x%06X", entry.trap, entry.name, entry.romOffset!))
        }
        let missing = table.entries.count - table.implemented.count
        lines.append("")
        lines.append("# \(table.implemented.count) implemented in ROM, \(missing) not in ROM (System file / unimplemented)")
        return lines.joined(separator: "\n")
    }
}
