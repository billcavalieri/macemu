import Foundation

/// First-divergence diff between a SheepShaver `NW-BOOT` log and the QEMU
/// golden event stream.
///
/// The SheepShaver side is sparse (the g3 tree logs the first 8 of each
/// exception kind and the first 24 A-lines; the S4 contract streams all of
/// them). So the comparison is per channel: the n-th DSI in the SheepShaver
/// log is compared with the n-th DSI in the golden stream, the n-th A-line
/// with the n-th A-line, and so on. Addresses are compared as MacROM offsets
/// (`BootLayout`), never as raw virtual addresses. The first SheepShaver
/// event whose counterpart differs (or does not exist) is the divergence.
public enum BootTraceDiff {
    public struct Options: Sendable {
        public var sheepLayout: BootLayout = .sheepShaver
        public var goldenLayout: BootLayout = .qemuMac99
        /// Asynchronous vectors whose exact PC is timing-dependent; compared
        /// only for presence, not PC. Default: external and decrementer.
        public var asyncVectors: Set<UInt16> = [0x500, 0x900]
        /// Channels to ignore entirely (e.g. `X700` if the emulator `twi`
        /// storm is not logged on the SheepShaver side).
        public var ignoreChannels: Set<String> = []
        public var context = 6
        /// Compare DAR for DSI when both sides are ROM addresses.
        public var compareDAR = false

        public init() {}
    }

    public struct Side: Sendable, Equatable {
        public var event: BootEvent
        public var address: BootAddress?
        public var extraAddress: BootAddress?
        public var symbol: String?
    }

    public struct Divergence: Sendable {
        public var channel: String
        public var occurrence: Int
        public var reason: String
        public var sheep: Side
        public var golden: Side?
        public var sheepContext: [String]
        public var goldenContext: [Side]
        public var goldenPhase: String?
    }

    public struct ChannelSummary: Sendable, Equatable {
        public var channel: String
        public var sheepCount: Int
        public var goldenCount: Int
        public var matched: Int
    }

    public struct Report: Sendable {
        public var checked: Int
        public var divergence: Divergence?
        public var channels: [ChannelSummary]
        public var lastMatched: Side?
    }

    public struct Symbolicator {
        public var database: AnalysisDatabase?
        public var trapTable: SuperMarioTrapTable.Table?

        public init(database: AnalysisDatabase? = nil, trapTable: SuperMarioTrapTable.Table? = nil) {
            self.database = database
            self.trapTable = trapTable
        }

        public func symbol(for address: BootAddress?) -> String? {
            guard let address, let offset = address.romOffset else { return nil }
            switch address.region {
            case .rom68k:
                if let entry = trapTable?.trap(containing: UInt32(offset)), let start = entry.romOffset {
                    let delta = offset - UInt64(start)
                    // Trap implementations are dense; past a few KiB the
                    // preceding entry is almost certainly a different routine.
                    if delta == 0 { return entry.name }
                    if delta <= 0x1000 { return String(format: "%@+0x%X", entry.name, delta) }
                    return String(format: "%@+0x%X?", entry.name, delta)
                }
                if let database,
                   let symbol = database.symbol(at: ProgramAddress(space: .m68kToolbox, address: offset)) {
                    return symbol.name
                }
                return nil
            case .romPPC:
                if let database,
                   let symbol = database.symbol(at: ProgramAddress(space: .ppcMacROM, address: AddressSpaces.ppcMacROMBase + offset)) {
                    return symbol.name
                }
                return NKRegions.name(forROMOffset: offset)
            default:
                return nil
            }
        }
    }

    public static func compare(
        sheep: [BootEvent],
        golden: [BootEvent],
        options: Options = Options(),
        symbolicator: Symbolicator = Symbolicator(),
        goldenPhases: GoldenTraceImporter.Phases? = nil,
        goldenByteOffsets: [Int] = [],
        sheepLines: [String] = []
    ) -> Report {
        var goldenByChannel: [String: [Int]] = [:]
        for (index, event) in golden.enumerated() {
            goldenByChannel[event.kind.channel, default: []].append(index)
        }

        var seen: [String: Int] = [:]
        var matchedPerChannel: [String: Int] = [:]
        var sheepPerChannel: [String: Int] = [:]
        var checked = 0
        var lastMatched: Side?
        var lastGoldenIndex = -1
        var divergence: Divergence?

        for (sheepIndex, event) in sheep.enumerated() {
            let channel = event.kind.channel
            if options.ignoreChannels.contains(channel) { continue }
            let occurrence = seen[channel, default: 0]
            seen[channel] = occurrence + 1
            sheepPerChannel[channel, default: 0] += 1
            checked += 1

            let sheepSide = side(event, layout: options.sheepLayout, symbolicator: symbolicator)
            guard let goldenIndices = goldenByChannel[channel], occurrence < goldenIndices.count else {
                // Show what the golden run did instead: the events that
                // follow the last golden event we matched (or the start of
                // the stream when nothing has matched yet).
                let resume = min(lastGoldenIndex + 1, max(golden.count - 1, 0))
                divergence = Divergence(
                    channel: channel,
                    occurrence: occurrence,
                    reason: "golden stream has only \(goldenByChannel[channel]?.count ?? 0) \(event.kind.channel) event(s); SheepShaver raised \(event.kind.channel) #\(occurrence + 1)",
                    sheep: sheepSide,
                    golden: nil,
                    sheepContext: context(sheepLines, around: event.line, radius: options.context),
                    goldenContext: golden.isEmpty ? [] : goldenContext(
                        golden, around: resume + options.context, radius: options.context,
                        layout: options.goldenLayout, symbolicator: symbolicator
                    ),
                    goldenPhase: goldenByteOffsets.indices.contains(resume)
                        ? goldenPhases?.phase(atByteOffset: goldenByteOffsets[resume]) : nil
                )
                break
            }
            let goldenIndex = goldenIndices[occurrence]
            let goldenEvent = golden[goldenIndex]
            let goldenSide = side(goldenEvent, layout: options.goldenLayout, symbolicator: symbolicator)

            if let reason = mismatch(sheepSide, goldenSide, options: options) {
                divergence = Divergence(
                    channel: channel,
                    occurrence: occurrence,
                    reason: reason,
                    sheep: sheepSide,
                    golden: goldenSide,
                    sheepContext: context(sheepLines, around: event.line, radius: options.context),
                    goldenContext: goldenContext(golden, around: goldenIndex, radius: options.context,
                                                 layout: options.goldenLayout, symbolicator: symbolicator),
                    goldenPhase: goldenByteOffsets.indices.contains(goldenIndex)
                        ? goldenPhases?.phase(atByteOffset: goldenByteOffsets[goldenIndex]) : nil
                )
                break
            }
            matchedPerChannel[channel, default: 0] += 1
            lastMatched = goldenSide
            lastGoldenIndex = max(lastGoldenIndex, goldenIndex)
            _ = sheepIndex
        }

        let channels = Set(sheepPerChannel.keys).union(goldenByChannel.keys).sorted().map { channel in
            ChannelSummary(
                channel: channel,
                sheepCount: sheepPerChannel[channel] ?? 0,
                goldenCount: goldenByChannel[channel]?.count ?? 0,
                matched: matchedPerChannel[channel] ?? 0
            )
        }
        return Report(checked: checked, divergence: divergence, channels: channels, lastMatched: lastMatched)
    }

    public static func side(_ event: BootEvent, layout: BootLayout, symbolicator: Symbolicator) -> Side {
        let address = layout.address(of: event)
        var extraAddress: BootAddress?
        if case .exception(let vector) = event.kind, vector == 0x300 || vector == 0x600, let dar = event.extra {
            extraAddress = layout.ppc(dar)
        }
        return Side(event: event, address: address, extraAddress: extraAddress, symbol: symbolicator.symbol(for: address))
    }

    static func mismatch(_ sheep: Side, _ golden: Side, options: Options) -> String? {
        guard sheep.event.kind == golden.event.kind else {
            return "kind differs: \(sheep.event.kind.display) vs \(golden.event.kind.display)"
        }
        if case .exception(let vector) = sheep.event.kind, options.asyncVectors.contains(vector) {
            return nil
        }
        guard let s = sheep.address, let g = golden.address else {
            return nil // one side did not log a PC; presence-only match
        }
        let sROM = s.romOffset
        let gROM = g.romOffset
        switch (sROM, gROM) {
        case let (sOff?, gOff?):
            if sOff != gOff {
                return "PC differs: \(s.display) vs \(g.display)"
            }
        case (nil, nil):
            break // both outside the ROM image (RAM / firmware); not comparable
        case (nil, _?):
            return "SheepShaver PC \(s.display) is outside the ROM image; golden is \(g.display)"
        case (_?, nil):
            return "golden PC \(g.display) is outside the ROM image; SheepShaver is \(s.display)"
        }
        if options.compareDAR,
           let sd = sheep.extraAddress?.romOffset, let gd = golden.extraAddress?.romOffset, sd != gd {
            return String(format: "DAR differs: ROM+0x%06X vs ROM+0x%06X", sd, gd)
        }
        return nil
    }

    static func context(_ lines: [String], around line: Int, radius: Int) -> [String] {
        guard !lines.isEmpty, line >= 1 else { return [] }
        let index = line - 1
        let low = max(0, index - radius)
        let high = min(lines.count - 1, index + radius)
        return (low...high).map { i in
            (i == index ? ">> " : "   ") + lines[i]
        }
    }

    static func goldenContext(
        _ golden: [BootEvent],
        around index: Int,
        radius: Int,
        layout: BootLayout,
        symbolicator: Symbolicator
    ) -> [Side] {
        let low = max(0, index - radius)
        let high = min(golden.count - 1, index + radius)
        return (low...high).map { side(golden[$0], layout: layout, symbolicator: symbolicator) }
    }

    // MARK: - Rendering

    public static func format(_ side: Side, marker: String = "   ") -> String {
        var text = marker + side.event.kind.display
        if let address = side.address {
            text += "  pc=" + address.display
            if address.region == .rom68k || address.region == .romPPC {
                text += String(format: " (0x%08X)", address.raw)
            }
        } else {
            text += "  pc=?"
        }
        if let symbol = side.symbol { text += "  " + symbol }
        if let extra = side.extraAddress {
            text += "  dar=" + extra.display
        } else if let extra = side.event.extra, case .exception(let vector) = side.event.kind, vector == 0x700 {
            text += String(format: "  srr1=%08X%@", extra, programReason(srr1: extra))
        }
        text += "  [line \(side.event.line)]"
        return text
    }

    static func programReason(srr1: UInt64) -> String {
        if srr1 & 0x0002_0000 != 0 { return " trap" }
        if srr1 & 0x0004_0000 != 0 { return " priv" }
        if srr1 & 0x0008_0000 != 0 { return " illegal" }
        if srr1 & 0x0010_0000 != 0 { return " fp" }
        return ""
    }

    public static func render(_ report: Report) -> String {
        var out: [String] = []
        out.append("Channels (SheepShaver / golden / matched):")
        for channel in report.channels where channel.sheepCount > 0 || channel.channel == "A" {
            out.append("  \(channel.channel.padding(toLength: 5, withPad: " ", startingAt: 0)) \(channel.sheepCount) / \(channel.goldenCount) / \(channel.matched)")
        }
        out.append("Checked \(report.checked) SheepShaver event(s).")
        guard let d = report.divergence else {
            out.append("No divergence: every logged SheepShaver event matches the golden stream in order.")
            if let last = report.lastMatched {
                out.append("Last matched golden event: " + format(last))
            }
            return out.joined(separator: "\n")
        }
        out.append("")
        out.append("FIRST DIVERGENCE: \(d.channel) occurrence #\(d.occurrence + 1) — \(d.reason)")
        if let phase = d.goldenPhase { out.append("Golden phase: \(phase)") }
        out.append("")
        out.append("SheepShaver:")
        out.append(format(d.sheep, marker: ">> "))
        if !d.sheepContext.isEmpty {
            out.append("SheepShaver log context:")
            out.append(contentsOf: d.sheepContext.map { "    " + $0 })
        }
        out.append("")
        out.append("Golden:")
        if let g = d.golden {
            out.append(format(g, marker: ">> "))
        } else {
            out.append(">> (no counterpart)")
        }
        if !d.goldenContext.isEmpty {
            out.append(d.golden == nil ? "Golden stream after the last matched event:" : "Golden stream context:")
            for side in d.goldenContext {
                let marker = side.event.line == d.golden?.event.line ? ">> " : "   "
                out.append("    " + format(side, marker: marker))
            }
        }
        return out.joined(separator: "\n")
    }
}

/// Named NanoKernel / emulator regions of the PPC part, for PCs without a
/// database symbol. Offsets from `nw_boot_contract.h` and elliotnunn/NanoKernel.
public enum NKRegions {
    public static func name(forROMOffset offset: UInt64) -> String? {
        switch offset {
        case 0x3132A0..<0x313300: return "NK:DataStorageInt"
        case 0x312B1C..<0x312B40: return "NK:InterruptEntry"
        case 0x313200..<0x313300: return "NK:DecrementerInt"
        case 0x313960..<0x313A00: return "NK:InstructionStorageInt"
        case 0x310000..<0x360000: return "NK"
        case 0x366084: return "EMU:inner-loop"
        case 0x3695E0..<0x369660: return "EMU:A-line-OS-noA0"
        case 0x369660..<0x369720: return "EMU:A-line-OS"
        case 0x369720..<0x369780: return "EMU:A-line-Tool"
        case 0x369780..<0x3697E0: return "EMU:A-line-Tool-autopop"
        case 0x36E8C0..<0x36E900: return "EMU:twi-kernel-call"
        case 0x360000..<0x380000: return "EMU"
        case 0x380000..<0x400000: return "EMU:dispatch-table"
        case 0x30D000..<0x310000: return "ConfigInfo"
        default: return nil
        }
    }
}
