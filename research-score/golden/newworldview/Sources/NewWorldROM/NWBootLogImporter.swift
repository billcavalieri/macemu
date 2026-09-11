import Foundation

/// Extract `BootEvent`s from a SheepShaver `NW-BOOT` log.
///
/// Accepted spellings:
///
/// - Canonical (the S4 contract; same grammar as the nwgolden stream):
///   `NW-BOOT X E <srr0> <vector> [<dar>|<srr1>]`, `NW-BOOT A <op> <68k-pc> <h>`
/// - Legacy G3 lines (first 8 per kind / first 24 A-lines in the g3 tree):
///   `G3: DSI n=1 SRR0=… DAR=… to=…`, `G3: ISI n=1 SRR0=… to=…`,
///   `G3: sc n=1 r0=… to=…`, `G3: DEC 0x900 to=… srr0=…`,
///   `G3: 68k A-line A9F2 pc=…`
///
/// `68k map` / `68k spin` heartbeats are not dispatch events and are skipped.
public enum NWBootLogImporter {
    public struct Result: Sendable {
        public var events: [BootEvent]
        public var lines: [String]
        /// Number of lines that were `NW-BOOT` but carried no event.
        public var otherBootLines: Int
    }

    public static func load(_ url: URL) throws -> Result {
        let text = try MillLogReader.decompressedString(from: url)
        return parse(text)
    }

    public static func parse(_ text: String) -> Result {
        var events: [BootEvent] = []
        var lines: [String] = []
        var other = 0
        var lineNumber = 0
        text.enumerateLines { line, _ in
            lineNumber += 1
            lines.append(line)
            guard let range = line.range(of: "NW-BOOT ") else { return }
            let body = line[range.upperBound...]
            if let event = parse(body: body, lineNumber: lineNumber) {
                events.append(event)
            } else {
                other += 1
            }
        }
        return Result(events: events, lines: lines, otherBootLines: other)
    }

    static func parse(body: Substring, lineNumber: Int) -> BootEvent? {
        if body.hasPrefix("X ") || body.hasPrefix("A ") {
            return GoldenTraceImporter.parseEventLine(body, lineNumber: lineNumber)
        }
        guard body.hasPrefix("G3: ") || body.hasPrefix("G2: ") else { return nil }
        let text = String(body.dropFirst(4))

        if text.hasPrefix("DSI n=") {
            return BootEvent(
                kind: .exception(vector: 0x300),
                pc: hexField("SRR0", in: text),
                extra: hexField("DAR", in: text),
                target: hexField("to", in: text),
                line: lineNumber
            )
        }
        if text.hasPrefix("ISI n=") {
            return BootEvent(
                kind: .exception(vector: 0x400),
                pc: hexField("SRR0", in: text),
                target: hexField("to", in: text),
                line: lineNumber
            )
        }
        if text.hasPrefix("sc n=") {
            return BootEvent(
                kind: .exception(vector: 0xC00),
                pc: hexField("pc", in: text) ?? hexField("srr0", in: text),
                extra: hexField("r0", in: text),
                target: hexField("to", in: text),
                line: lineNumber
            )
        }
        if text.hasPrefix("DEC 0x900 to=") {
            return BootEvent(
                kind: .exception(vector: 0x900),
                pc: hexField("srr0", in: text),
                target: hexField("to", in: text),
                line: lineNumber
            )
        }
        if text.hasPrefix("68k A-line ") {
            let rest = text.dropFirst("68k A-line ".count)
            let opText = rest.prefix { $0.isHexDigit }
            guard let op = UInt16(opText, radix: 16) else { return nil }
            return BootEvent(kind: .aline(op: op), pc: hexField("pc", in: text), line: lineNumber)
        }
        return nil
    }

    private static func hexField(_ name: String, in text: String) -> UInt64? {
        guard let regex = try? NSRegularExpression(pattern: "\\b\(name)=([0-9a-fA-F]+)", options: []) else {
            return nil
        }
        let range = NSRange(text.startIndex..., in: text)
        guard let match = regex.firstMatch(in: text, range: range),
              let valueRange = Range(match.range(at: 1), in: text)
        else { return nil }
        return UInt64(text[valueRange], radix: 16)
    }
}
