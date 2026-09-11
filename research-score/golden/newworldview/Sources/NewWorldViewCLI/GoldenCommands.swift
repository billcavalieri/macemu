import Foundation
import NewWorldROM

/// S3 golden-reference commands: QEMU `nwgolden` stream vs SheepShaver `NW-BOOT`.
extension NewWorldViewCLI {
    struct ArgScanner {
        var args: [String]
        var positional: [String] = []
        var flags: Set<String> = []
        var values: [String: String] = [:]

        init(_ args: [String], valued: Set<String>) {
            self.args = args
            var index = 0
            while index < args.count {
                let arg = args[index]
                if arg.hasPrefix("--") {
                    if valued.contains(arg), index + 1 < args.count {
                        values[arg] = args[index + 1]
                        index += 2
                        continue
                    }
                    flags.insert(arg)
                } else {
                    positional.append(arg)
                }
                index += 1
            }
        }

        func int(_ key: String) -> Int? { values[key].flatMap { Int($0) } }
    }

    static func loadSymbolicator(romPath: String?) throws -> BootTraceDiff.Symbolicator {
        guard let romPath else { return BootTraceDiff.Symbolicator() }
        let (parsed, database, _) = try loadROM(at: romPath)
        return BootTraceDiff.Symbolicator(database: database, trapTable: AnalysisEngine.trapTable(in: parsed))
    }

    static func loadGolden(
        _ path: String,
        phasesPath: String?,
        fromBoot: Bool,
        vectors: Set<UInt16>? = nil,
        phaseRange: String? = nil
    ) throws -> (GoldenTraceImporter.Stream, GoldenTraceImporter.Phases?) {
        var options = GoldenTraceImporter.Options()
        options.startAtROM = !fromBoot
        options.vectors = vectors
        var phases: GoldenTraceImporter.Phases?
        if let phasesPath {
            phases = try GoldenTraceImporter.Phases.load(URL(fileURLWithPath: phasesPath))
        } else {
            let sibling = URL(fileURLWithPath: path).deletingLastPathComponent().appendingPathComponent("phases.json")
            phases = try? GoldenTraceImporter.Phases.load(sibling)
        }
        if let phaseRange, let phases {
            let parts = phaseRange.split(separator: "..", omittingEmptySubsequences: false).map(String.init)
            let startName = parts.first ?? ""
            let endName = parts.count > 1 ? parts[1] : nil
            if !startName.isEmpty {
                guard let mark = phases.marks[startName] else {
                    throw CLIError.message("phase '\(startName)' not in phases.json (\(phases.marks.keys.sorted().joined(separator: ", ")))")
                }
                options.startByteOffset = mark.bytes
                options.startAtROM = false
            }
            if let endName, !endName.isEmpty {
                guard let mark = phases.marks[endName] else {
                    throw CLIError.message("phase '\(endName)' not in phases.json")
                }
                options.endByteOffset = mark.bytes
            } else if endName == nil, !startName.isEmpty {
                // "--phase desktop" = from desktop mark to the next mark.
                let sorted = phases.marks.filter { $0.key != "end" }.sorted { $0.value.bytes < $1.value.bytes }
                if let index = sorted.firstIndex(where: { $0.key == startName }), index + 1 < sorted.count {
                    options.endByteOffset = sorted[index + 1].value.bytes
                }
            }
        }
        let stream = try GoldenTraceImporter.load(URL(fileURLWithPath: path), options: options)
        return (stream, phases)
    }

    static func runDiff(_ args: [String]) throws {
        let scan = ArgScanner(args, valued: ["--qemu", "--nwboot", "--rom", "--phases", "--ignore", "--context"])
        guard let qemuPath = scan.values["--qemu"], let nwbootPath = scan.values["--nwboot"] else {
            throw CLIError.usage("diff --qemu <events.txt> --nwboot <nw-boot.log> [--rom <rom.tbxi>] [--phases <phases.json>] [--ignore X700,X500] [--context N] [--compare-dar] [--strict-async]")
        }
        let symbolicator = try loadSymbolicator(romPath: scan.values["--rom"])
        let (golden, phases) = try loadGolden(qemuPath, phasesPath: scan.values["--phases"], fromBoot: false)
        let sheep = try NWBootLogImporter.load(URL(fileURLWithPath: nwbootPath))

        var options = BootTraceDiff.Options()
        if let ignore = scan.values["--ignore"] {
            options.ignoreChannels = Set(ignore.split(separator: ",").map { String($0).uppercased() })
        }
        if let context = scan.int("--context") { options.context = context }
        options.compareDAR = scan.flags.contains("--compare-dar")
        if scan.flags.contains("--strict-async") { options.asyncVectors = [] }

        print("Golden: \(golden.events.count) event(s) from NK handoff (\(golden.lineCount) lines, \(golden.ticks.count) ticks) — \(qemuPath)")
        print("SheepShaver: \(sheep.events.count) event(s) in \(sheep.lines.count) lines (\(sheep.otherBootLines) other NW-BOOT lines) — \(nwbootPath)")
        if sheep.events.isEmpty {
            print("No comparable events in the SheepShaver log (need `NW-BOOT X …`/`NW-BOOT A …` or G3 DSI/ISI/sc/DEC/68k A-line lines).")
            return
        }
        let report = BootTraceDiff.compare(
            sheep: sheep.events,
            golden: golden.events,
            options: options,
            symbolicator: symbolicator,
            goldenPhases: phases,
            goldenByteOffsets: golden.eventByteOffsets,
            sheepLines: sheep.lines
        )
        print("")
        print(BootTraceDiff.render(report))
    }

    static func runGoldenStats(_ args: [String]) throws {
        let scan = ArgScanner(args, valued: ["--phases", "--rom", "--top"])
        guard let path = scan.positional.first else {
            throw CLIError.usage("golden-stats <events.txt> [--phases <phases.json>] [--rom <rom.tbxi>] [--from-boot] [--top N]")
        }
        let (stream, phases) = try loadGolden(path, phasesPath: scan.values["--phases"], fromBoot: scan.flags.contains("--from-boot"))
        let top = scan.int("--top") ?? 20
        let layout = BootLayout.qemuMac99

        print("\(stream.header ?? "(no header)")")
        print("Events: \(stream.events.count)  lines: \(stream.lineCount)  ticks: \(stream.ticks.count)")
        if let first = stream.events.first, let last = stream.events.last {
            print("First event line \(first.line): \(BootTraceDiff.format(BootTraceDiff.side(first, layout: layout, symbolicator: BootTraceDiff.Symbolicator())))")
            print("Last event line \(last.line): \(last.kind.display)")
        }
        if let phases {
            print("\nPhases (events.txt byte offsets):")
            for (name, mark) in phases.marks.sorted(by: { $0.value.bytes < $1.value.bytes }) {
                let count = stream.eventByteOffsets.filter { $0 < mark.bytes }.count
                print(String(format: "  %-8@ t=%6.1fs  bytes=%10d  events-before=%d  shot=%04d", name, mark.elapsed, mark.bytes, count, mark.shot))
            }
        }

        var vectorCounts: [UInt16: Int] = [:]
        var alineCount = 0
        var trapCounts: [UInt16: Int] = [:]
        var regionCounts: [String: Int] = [:]
        for event in stream.events {
            switch event.kind {
            case .exception(let vector):
                vectorCounts[vector, default: 0] += 1
                if let address = layout.address(of: event) {
                    regionCounts["X \(address.region.rawValue)", default: 0] += 1
                }
            case .aline(let op):
                alineCount += 1
                trapCounts[ATrapTable.canonical(op), default: 0] += 1
                if let address = layout.address(of: event) {
                    regionCounts["A \(address.region.rawValue)", default: 0] += 1
                }
            }
        }
        print("\nExceptions by vector:")
        for (vector, count) in vectorCounts.sorted(by: { $0.value > $1.value }) {
            print(String(format: "  %-9@ 0x%03X  %d", BootEvent.vectorName(vector), vector, count))
        }
        print("\nA-line dispatches: \(alineCount) (\(trapCounts.count) distinct traps)")
        for (trap, count) in trapCounts.sorted(by: { $0.value > $1.value }).prefix(top) {
            print(String(format: "  %04X  %-28@ %d", trap, ATrapTable.bareName(for: trap), count))
        }
        print("\nPC regions:")
        for (region, count) in regionCounts.sorted(by: { $0.key < $1.key }) {
            print("  \(region): \(count)")
        }
    }

    static func runGoldenATraps(_ args: [String]) throws {
        let scan = ArgScanner(args, valued: ["--phases", "--rom", "--limit", "--phase", "--skip"])
        guard let path = scan.positional.first else {
            throw CLIError.usage("golden-atraps <events.txt> [--limit N] [--skip N] [--unique] [--phase name[..name] --phases <phases.json>] [--rom <rom.tbxi>] [--from-boot]")
        }
        let symbolicator = try loadSymbolicator(romPath: scan.values["--rom"])
        let (stream, _) = try loadGolden(
            path,
            phasesPath: scan.values["--phases"],
            fromBoot: scan.flags.contains("--from-boot"),
            vectors: [],
            phaseRange: scan.values["--phase"]
        )
        let limit = scan.int("--limit") ?? 200
        let skip = scan.int("--skip") ?? 0
        let unique = scan.flags.contains("--unique")
        let layout = BootLayout.qemuMac99

        var printed = 0
        var seen = Set<UInt16>()
        var index = 0
        for event in stream.events {
            guard case .aline(let op) = event.kind else { continue }
            if unique {
                guard seen.insert(ATrapTable.canonical(op)).inserted else { continue }
            }
            index += 1
            if index <= skip { continue }
            let side = BootTraceDiff.side(event, layout: layout, symbolicator: symbolicator)
            print(String(format: "%7d  ", index) + BootTraceDiff.format(side))
            printed += 1
            if printed >= limit { break }
        }
        print("\n\(printed) A-line dispatch(es) shown\(unique ? " (first occurrence of each trap)" : ""); \(stream.events.count) in range.")
    }

    static func runTrapTable(_ args: [String]) throws {
        let scan = ArgScanner(args, valued: ["--trap", "--at"])
        guard let romPath = scan.positional.first else {
            throw CLIError.usage("trap-table <rom.tbxi> [--trap A9F2] [--at 68k-offset]")
        }
        let (parsed, _, _) = try loadROM(at: romPath)
        guard let table = AnalysisEngine.trapTable(in: parsed) else {
            throw CLIError.message("No SuperMario dispatch table found in \(romPath)")
        }
        if let trapText = scan.values["--trap"] {
            let clean = trapText.replacingOccurrences(of: "0x", with: "").replacingOccurrences(of: "$", with: "")
            guard let trap = UInt16(clean, radix: 16) else { throw CLIError.message("bad trap \(trapText)") }
            guard let entry = table.entry(for: trap) else { throw CLIError.message("trap \(trapText) not in table") }
            if let offset = entry.romOffset {
                print(String(format: "%04X %@ -> 68k ROM+0x%06X", entry.trap, entry.name, offset))
            } else {
                print(String(format: "%04X %@ -> not implemented in ROM", entry.trap, entry.name))
            }
            return
        }
        if let atText = scan.values["--at"] {
            let clean = atText.replacingOccurrences(of: "0x", with: "")
            guard let offset = UInt32(clean, radix: 16) else { throw CLIError.message("bad offset \(atText)") }
            if let entry = table.trap(containing: offset), let start = entry.romOffset {
                print(String(format: "68k ROM+0x%06X is %@+0x%X (%04X starts at 0x%06X)", offset, entry.name, offset - start, entry.trap, start))
            } else {
                print("no trap implementation at or below that offset")
            }
            return
        }
        print(SuperMarioTrapTable.listing(table))
    }

    static func runDrivers(_ args: [String]) throws {
        let scan = ArgScanner(args, valued: ["--devtree"])
        guard let romPath = scan.positional.first else {
            throw CLIError.usage("drivers <rom.tbxi> [--devtree <devtree-mac99.txt>] [--json]")
        }
        let (parsed, _, _) = try loadROM(at: romPath)
        var inventory = ROMDriverInventory.build(parsed)
        if let devtreePath = scan.values["--devtree"] {
            let text = try String(contentsOf: URL(fileURLWithPath: devtreePath), encoding: .utf8)
            let tree = OpenFirmwareTreeDump.parse(text)
            inventory.match(against: tree)
        }
        if scan.flags.contains("--json") {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            print(String(data: try encoder.encode(inventory), encoding: .utf8) ?? "{}")
        } else {
            print(ROMDriverInventory.render(inventory))
        }
    }
}
