import Foundation

/// Importer for the QEMU `nwgolden` plugin stream (macemu
/// `research-score/golden/nwgolden.c`). Grammar, one event per line:
///
///     # nwgolden 1 target=ppc
///     X <E|I|H> <from-pc> <to-vector> [<dar>|<srr1>]
///     A <op> <68k-pc> <handler>
///     T <epoch-ms> <nX> <nA>
///
/// The same grammar, prefixed with `NW-BOOT `, is accepted from SheepShaver
/// logs by `NWBootLogImporter`, so a SheepShaver build that emits
/// `NW-BOOT X …` / `NW-BOOT A …` lines diffs 1:1 against the golden stream.
public enum GoldenTraceImporter {
    public struct Tick: Sendable, Equatable {
        public var epochMS: Int64
        public var exceptions: Int
        public var alines: Int
        public var line: Int
    }

    public struct Stream {
        public var events: [BootEvent] = []
        public var ticks: [Tick] = []
        public var header: String?
        public var lineCount = 0
        /// Byte offset at which each event's line starts (for phase markers).
        public var eventByteOffsets: [Int] = []
    }

    public struct Options: Sendable {
        /// Drop everything before the first event whose PC is in the Mac ROM
        /// (68k or PPC part) — i.e. skip OpenBIOS. This is the "NK handoff".
        public var startAtROM = true
        public var layout: BootLayout = .qemuMac99
        /// Keep only these vectors (nil = all). A-line events are always kept.
        public var vectors: Set<UInt16>? = nil
        public var maxEvents: Int? = nil
        /// Stop reading at this byte offset (phase marker).
        public var endByteOffset: Int? = nil
        public var startByteOffset: Int = 0

        public init() {}
    }

    /// Parse one event line (without trailing newline). Returns nil for
    /// comments, ticks and unknown lines.
    public static func parseEventLine(_ line: Substring, lineNumber: Int) -> BootEvent? {
        var fields = line.split(separator: " ", omittingEmptySubsequences: true)
        guard let head = fields.first else { return nil }
        fields.removeFirst()
        switch head {
        case "X":
            guard fields.count >= 3,
                  let from = UInt64(fields[1], radix: 16),
                  let to = UInt64(fields[2], radix: 16)
            else { return nil }
            let extra = fields.count >= 4 ? UInt64(fields[3], radix: 16) : nil
            return BootEvent(
                kind: .exception(vector: UInt16(truncatingIfNeeded: to & 0xFFFF)),
                pc: from,
                extra: extra,
                target: to,
                line: lineNumber
            )
        case "A":
            guard fields.count >= 2,
                  let op = UInt16(fields[0], radix: 16),
                  let pc = UInt64(fields[1], radix: 16)
            else { return nil }
            return BootEvent(kind: .aline(op: op), pc: pc, line: lineNumber)
        default:
            return nil
        }
    }

    public static func parseTick(_ line: Substring, lineNumber: Int) -> Tick? {
        let fields = line.split(separator: " ", omittingEmptySubsequences: true)
        guard fields.count >= 4, fields[0] == "T",
              let ms = Int64(fields[1]), let nx = Int(fields[2]), let na = Int(fields[3])
        else { return nil }
        return Tick(epochMS: ms, exceptions: nx, alines: na, line: lineNumber)
    }

    /// Stream a whole file. Byte-level line scanning; 100 MB parses in seconds.
    public static func load(_ url: URL, options: Options = Options()) throws -> Stream {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var stream = Stream()
        var started = !options.startAtROM
        var pending = Data()
        var lineNumber = 0
        var byteOffset = 0
        let chunkSize = 4 << 20

        if options.startByteOffset > 0 {
            try handle.seek(toOffset: UInt64(options.startByteOffset))
            byteOffset = options.startByteOffset
        }

        func handleLine(_ data: Data, at offset: Int) -> Bool {
            lineNumber += 1
            guard let first = data.first else { return true }
            if first == UInt8(ascii: "#") {
                if stream.header == nil { stream.header = String(decoding: data, as: UTF8.self) }
                return true
            }
            let text = String(decoding: data, as: UTF8.self)
            if first == UInt8(ascii: "T") {
                if let tick = parseTick(text[...], lineNumber: lineNumber) { stream.ticks.append(tick) }
                return true
            }
            guard let event = parseEventLine(text[...], lineNumber: lineNumber) else { return true }
            if !started {
                guard let address = options.layout.address(of: event),
                      address.region == .rom68k || address.region == .romPPC
                else { return true }
                started = true
            }
            if case .exception(let vector) = event.kind, let keep = options.vectors, !keep.contains(vector) {
                return true
            }
            stream.events.append(event)
            stream.eventByteOffsets.append(offset)
            if let max = options.maxEvents, stream.events.count >= max { return false }
            return true
        }

        outer: while true {
            let chunk = handle.readData(ofLength: chunkSize)
            if chunk.isEmpty { break }
            pending.append(chunk)
            var start = pending.startIndex
            while let nl = pending[start...].firstIndex(of: UInt8(ascii: "\n")) {
                let lineStartOffset = byteOffset + (start - pending.startIndex)
                if let end = options.endByteOffset, lineStartOffset >= end { break outer }
                if !handleLine(pending[start..<nl], at: lineStartOffset) { break outer }
                start = nl + 1
            }
            byteOffset += start - pending.startIndex
            pending = Data(pending[start...])
        }
        if !pending.isEmpty {
            _ = handleLine(pending, at: byteOffset)
        }
        stream.lineCount = lineNumber
        return stream
    }

    /// `phases.json` written by `capture.py`: phase -> events.txt byte offset.
    public struct Phases: Sendable {
        public struct Mark: Sendable { public var elapsed: Double; public var bytes: Int; public var shot: Int }
        public var marks: [String: Mark]

        public static func load(_ url: URL) throws -> Phases {
            let data = try Data(contentsOf: url)
            let object = try JSONSerialization.jsonObject(with: data) as? [String: [String: Any]] ?? [:]
            var marks: [String: Mark] = [:]
            for (name, value) in object {
                marks[name] = Mark(
                    elapsed: (value["elapsed_s"] as? Double) ?? 0,
                    bytes: (value["events_bytes"] as? Int) ?? 0,
                    shot: (value["shot"] as? Int) ?? 0
                )
            }
            return Phases(marks: marks)
        }

        /// Phase active at a byte offset (last mark at or before it).
        public func phase(atByteOffset offset: Int) -> String? {
            marks.filter { $0.value.bytes <= offset && $0.key != "end" }
                .max { $0.value.bytes < $1.value.bytes }?.key
        }
    }
}
