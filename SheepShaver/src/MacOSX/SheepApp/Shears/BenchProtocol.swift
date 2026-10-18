/*
 *  BenchProtocol.swift - SheepBench's mailbox commands (benchInfo, benchNative, benchSetJIT, benchResult): payload
 *  decoding, the assembly of a report sent in chunks, and its conversion to JSON.
 *
 *  Pure Foundation, no AppKit and no emulator, so tools/shears/test.sh unit compiles it with the other pure files.
 *  Every number comes from the guest and is untrusted.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

/// benchNative: run kernel `kernel` (guest/sheepbench/kernels/sb_kernels.h) natively.
struct BenchNativeRequest: Equatable {
    var kernel: UInt32
    var iterations: UInt32
    var param: UInt32

    init?(payload: [UInt8]) {
        var r = ShearsReader(payload)
        guard let k = r.u32(), let n = r.u32(), let p = r.u32(), n > 0 else { return nil }
        kernel = k; iterations = n; param = p
    }

    init(kernel: UInt32, iterations: UInt32, param: UInt32) {
        self.kernel = kernel; self.iterations = iterations; self.param = param
    }

    /// Reply: picoseconds per iteration (high word, low word), then the checksum.
    static func reply(picoseconds: UInt64, checksum: UInt32) -> [UInt8] {
        var out: [UInt8] = []
        ShearsCodec.put(&out, UInt32(truncatingIfNeeded: picoseconds >> 32))
        ShearsCodec.put(&out, UInt32(truncatingIfNeeded: picoseconds))
        ShearsCodec.put(&out, checksum)
        return out
    }
}

/// benchSetJIT: which of the PowerPC JIT, the host 68k JIT and SheepForce QuickDraw should run, or "as at start-up".
struct BenchModeRequest: Equatable {
    var ppcJIT: Bool
    var m68kJIT: Bool
    var quickDrawGPU: Bool
    var restore: Bool

    init?(payload: [UInt8]) {
        var r = ShearsReader(payload)
        guard let p = r.u32(), let m = r.u32(), let q = r.u32(), let back = r.u32(),
              p <= 1, m <= 1, q <= 1, back <= 1 else { return nil }
        ppcJIT = p == 1; m68kJIT = m == 1; quickDrawGPU = q == 1; restore = back == 1
    }

    init(ppcJIT: Bool, m68kJIT: Bool, quickDrawGPU: Bool, restore: Bool) {
        self.ppcJIT = ppcJIT; self.m68kJIT = m68kJIT; self.quickDrawGPU = quickDrawGPU; self.restore = restore
    }
}

/// benchResult: one piece of the tab-separated report. Payload: chunk index, flags (bit 0: last), then text.
struct BenchResultChunk: Equatable {
    var index: UInt32
    var last: Bool
    var text: [UInt8]

    init?(payload: [UInt8]) {
        var r = ShearsReader(payload)
        guard let i = r.u32(), let flags = r.u32() else { return nil }
        index = i; last = flags & 1 != 0; text = r.rest()
    }

    init(index: UInt32, last: Bool, text: [UInt8]) {
        self.index = index; self.last = last; self.text = text
    }
}

/// Puts a report back together. Chunk 0 starts a new report; anything out of order or too large is refused and
/// the partial report dropped.
struct BenchResultAssembler {
    static let maxBytes = 1 << 20
    enum Outcome: Equatable { case partial, complete(String), refused }

    private var bytes: [UInt8] = []
    private var next: UInt32 = 0

    mutating func add(_ chunk: BenchResultChunk) -> Outcome {
        if chunk.index == 0 { bytes = []; next = 0 }
        guard chunk.index == next, bytes.count + chunk.text.count <= BenchResultAssembler.maxBytes else {
            bytes = []; next = 0
            return .refused
        }
        bytes += chunk.text
        next += 1
        guard chunk.last else { return .partial }
        // Mac line ends become Unix ones; anything that is not printable ASCII or a tab is dropped.
        let clean = bytes.map { $0 == 0x0D ? 0x0A : $0 }.filter { $0 == 0x0A || $0 == 0x09 || (0x20..<0x7F).contains($0) }
        bytes = []; next = 0
        return .complete(String(decoding: clean, as: UTF8.self))
    }
}

/// The report format the guest writes (guest/sheepbench, Save): lines "# key<TAB>value" carry the run info, the
/// first other line names the columns, and every further line is one result.
enum BenchReport {
    struct Parsed: Equatable {
        var info: [(String, String)]
        var columns: [String]
        var rows: [[String]]

        static func == (a: Parsed, b: Parsed) -> Bool {
            a.info.map { $0.0 + "\t" + $0.1 } == b.info.map { $0.0 + "\t" + $0.1 } && a.columns == b.columns && a.rows == b.rows
        }
    }

    static func parse(_ text: String) -> Parsed {
        var info: [(String, String)] = []
        var columns: [String] = []
        var rows: [[String]] = []
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            if line.hasPrefix("#") {
                let body = line.dropFirst().drop(while: { $0 == " " })
                let parts = body.split(separator: "\t", maxSplits: 1, omittingEmptySubsequences: false)
                info.append((String(parts[0]), parts.count > 1 ? String(parts[1]) : ""))
            } else if columns.isEmpty {
                columns = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            } else {
                rows.append(line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init))
            }
        }
        return Parsed(info: info, columns: columns, rows: rows)
    }

    /// JSON: {"info": {key: value}, "results": [{column: value}]}, numbers where a cell is a number.
    static func json(_ parsed: Parsed) -> Data {
        func cell(_ s: String) -> Any {
            if let d = Double(s), d.isFinite { return d }
            return s
        }
        var info: [String: Any] = [:]
        for (k, v) in parsed.info { info[k] = v }
        let results: [[String: Any]] = parsed.rows.map { row in
            var o: [String: Any] = [:]
            for (i, name) in parsed.columns.enumerated() where i < row.count { o[name] = cell(row[i]) }
            return o
        }
        let object: [String: Any] = ["info": info, "results": results]
        return (try? JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])) ?? Data()
    }
}
