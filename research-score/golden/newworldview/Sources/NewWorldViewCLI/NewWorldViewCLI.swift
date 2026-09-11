import Foundation
import NewWorldROM

@main
struct NewWorldViewCLI {
    static func main() {
        var args = Array(CommandLine.arguments.dropFirst())
        guard let command = args.first else {
            printUsage()
            exit(1)
        }
        args.removeFirst()

        do {
            switch command {
            case "export":
                try runExport(args)
            case "lookup":
                try runLookup(args)
            case "analyze-logs":
                try runAnalyzeLogs(args)
            case "decode-rom":
                try runDecodeROM(args)
            case "compare-disasm":
                try runCompareDisasm(args)
            case "diff-rom":
                try runDiffROM(args)
            case "build-context":
                try runBuildContext(args)
            case "build-grok-pack":
                try runBuildGrokPack(args)
            case "build-grok-pack-from-log":
                try runBuildGrokPackFromLog(args)
            case "analyze-log":
                try runAnalyzeLog(args)
            case "export-annotations":
                try runExportAnnotations(args)
            case "export-histogram":
                try runExportHistogram(args)
            case "export-pipeline":
                try runExportPipeline(args)
            case "toast-ls":
                try runToastLS(args)
            case "toast-get":
                try runToastGet(args)
            case "diff":
                try runDiff(args)
            case "golden-stats":
                try runGoldenStats(args)
            case "golden-atraps":
                try runGoldenATraps(args)
            case "trap-table":
                try runTrapTable(args)
            case "drivers":
                try runDrivers(args)
            case "help", "-h", "--help":
                printUsage()
            default:
                fputs("Unknown command: \(command)\n", stderr)
                printUsage()
                exit(1)
            }
        } catch {
            fputs("Error: \(error.localizedDescription)\n", stderr)
            exit(1)
        }
    }

    private static func printUsage() {
        print("""
        newworldview — headless NewWorld ROM tools

        Usage:
          swift run NewWorldViewCLI export <rom.tbxi> <output-dir> [--mill-traps]
          swift run NewWorldViewCLI lookup <rom.tbxi> <address>
          swift run NewWorldViewCLI analyze-logs [log-directory] [--rom <rom.tbxi>] [--limit N] [--histogram-limit N]
          swift run NewWorldViewCLI decode-rom <rom.tbxi> <MacROM.bin>
          swift run NewWorldViewCLI compare-disasm <rom.tbxi> <rom-offset-hex>
          swift run NewWorldViewCLI build-context <rom.tbxi> <address> [--json]
          swift run NewWorldViewCLI build-grok-pack <rom.tbxi> <address> -o <output-dir>
          swift run NewWorldViewCLI build-grok-pack-from-log <log> <address> -o <output-dir> [--rom <rom.tbxi>]
          swift run NewWorldViewCLI analyze-log <log> [--rom <rom.tbxi>]
          swift run NewWorldViewCLI export-annotations <store.json> -o <output-dir>
          swift run NewWorldViewCLI export-histogram [log-directory] -o <output-dir> [--rom <rom.tbxi>] [--limit N] [--histogram-limit N] [--annotations <store.json>]
          swift run NewWorldViewCLI export-pipeline [log-directory] -o <output-dir> [--rom <rom.tbxi>] [--limit N] [--histogram-limit N] [--annotations <store.json>]
          swift run NewWorldViewCLI toast-ls [--path HFS/path] [--toast image] [--cli ResViewerCLI]
          swift run NewWorldViewCLI toast-get --type DLOG --id 510 [--path HFS/path] [--format json|disasm] [--pc 0x10101efc --base 0x101013d0] [--at N] [--count N] [--text] [--toast image] [--cli ResViewerCLI]
          swift run NewWorldViewCLI diff-rom <rom-a.tbxi> <rom-b.tbxi>
          swift run NewWorldViewCLI diff --qemu <events.txt> --nwboot <nw-boot.log> [--rom <rom.tbxi>] [--phases <phases.json>] [--ignore X700,X500] [--context N] [--compare-dar] [--strict-async]
          swift run NewWorldViewCLI golden-stats <events.txt> [--phases <phases.json>] [--rom <rom.tbxi>] [--from-boot]
          swift run NewWorldViewCLI golden-atraps <events.txt> [--limit N] [--unique] [--phase name --phases <phases.json>] [--rom <rom.tbxi>] [--from-boot]
          swift run NewWorldViewCLI trap-table <rom.tbxi> [--trap A9F2] [--at 68k-offset]
          swift run NewWorldViewCLI drivers <rom.tbxi> [--devtree <devtree-mac99.txt>] [--json]
          swift run NewWorldViewCLI export ~/Downloads/Mac\\ OS\\ ROM ./out
          swift run NewWorldViewCLI lookup ~/Downloads/Mac\\ OS\\ ROM 50326564
          swift run NewWorldViewCLI analyze-logs ~/Documents/GitHub/macemu/research-score
          swift run NewWorldViewCLI decode-rom ~/Downloads/Mac\\ OS\\ ROM ./MacROM.bin
          swift run NewWorldViewCLI compare-disasm ~/Downloads/Mac\\ OS\\ ROM 0x326564
          swift run NewWorldViewCLI diff-rom <rom-a.tbxi> <rom-b.tbxi>
        """)
    }

    private static func runExport(_ args: [String]) throws {
        let positional = args.filter { !$0.hasPrefix("--") }
        guard positional.count >= 2 else {
            throw CLIError.usage("export <rom.tbxi> <output-dir>")
        }
        let millTraps = args.contains("--mill-traps")
        let romURL = URL(fileURLWithPath: positional[0])
        let outURL = URL(fileURLWithPath: positional[1])
        let data = try Data(contentsOf: romURL)
        let parsed = ROMParser.parse(data: data, fileName: romURL.lastPathComponent)
        let database = AnalysisEngine.analyze(parsed)
        let ghidraDir = outURL.appendingPathComponent("NewWorldView-ghidra", isDirectory: true)
        let options = GhidraExport.ExportOptions(
            includeMillTags: true,
            trapFilter: millTraps ? .millCritical : .all
        )
        try GhidraExport.write(database: database, parsed: parsed, to: ghidraDir, options: options)
        let manifest = try ROMBinaryExport.writeMacROM(rawData: data, parsed: parsed, to: outURL)
        print("Exported Ghidra metadata to \(ghidraDir.path)")
        print("Exported MacROM.bin (\(manifest.macROMSize) bytes, source=\(manifest.decodeSource), g0=\(manifest.newWorldOK))")
    }

    private static func runLookup(_ args: [String]) throws {
        let positional = args.filter { !$0.hasPrefix("--") }
        guard positional.count >= 2 else {
            throw CLIError.usage("lookup <rom.tbxi> <address>")
        }
        let romURL = URL(fileURLWithPath: positional[0])
        let query = positional[1]
        let data = try Data(contentsOf: romURL)
        let parsed = ROMParser.parse(data: data, fileName: romURL.lastPathComponent)
        let database = AnalysisEngine.analyze(parsed)
        guard let result = AnalysisLookup.lookup(query, in: parsed, database: database) else {
            throw CLIError.message("Could not parse address: \(query)")
        }
        print("address: \(result.address.display)")
        if let romOffset = result.romOffset {
            print("romOffset: 0x\(String(format: "%X", romOffset))")
        }
        if let nodeName = result.nodeName {
            print("node: \(nodeName)")
        }
        if let symbol = result.symbol?.name {
            print("symbol: \(symbol)")
        }
        if !result.tags.isEmpty {
            print("tags: \(result.tags.joined(separator: ", "))")
        }
        if result.address.space == .pef, G3PEFPlantMap.isHosted(result.address.address) {
            print("pefPlant: \(G3PEFPlantMap.format(result.address.address))")
            if let client = try? ToastCatalogLookup.Client.resolved(),
               let lines = try? client.disasmAtHostedPC(result.address.address) {
                print("\nDisassembly (toast CODE 1 via ResViewer):")
                for line in lines.prefix(MillAnalysisContextBuilder.defaultDisasmLineLimit) {
                    print(line)
                }
            }
        }
        print("incoming: \(result.incomingXRefs.count)  outgoing: \(result.outgoingXRefs.count)")
    }

    private static func runAnalyzeLogs(_ args: [String]) throws {
        var romPath: String?
        var directoryPath = MillResearchEngine.defaultLogDirectory.path
        var scanLimit = MillResearchEngine.defaultScanLimit
        var histogramLimit = MillResearchEngine.defaultHistogramLimit
        var keepLogsOnly = MillLogDiscovery.Options.default.keepLogsOnly
        var index = 0
        while index < args.count {
            switch args[index] {
            case "--rom":
                index += 1
                guard index < args.count else { throw CLIError.usage("analyze-logs [--rom <tbxi>] [--limit N] [--histogram-limit N] [--all-logs] [log-dir]") }
                romPath = args[index]
            case "--limit":
                index += 1
                guard index < args.count, let value = Int(args[index]) else {
                    throw CLIError.usage("analyze-logs [--rom <tbxi>] [--limit N] [--histogram-limit N] [--all-logs] [log-dir]")
                }
                scanLimit = value
            case "--histogram-limit":
                index += 1
                guard index < args.count, let value = Int(args[index]) else {
                    throw CLIError.usage("analyze-logs [--rom <tbxi>] [--limit N] [--histogram-limit N] [--all-logs] [log-dir]")
                }
                histogramLimit = value
            case "--all-logs":
                keepLogsOnly = false
            default:
                if !args[index].hasPrefix("--") {
                    directoryPath = args[index]
                }
            }
            index += 1
        }
        let directory = URL(fileURLWithPath: directoryPath)
        var database = AnalysisDatabase.empty
        if let romPath {
            let data = try Data(contentsOf: URL(fileURLWithPath: romPath))
            let parsed = ROMParser.parse(data: data, fileName: URL(fileURLWithPath: romPath).lastPathComponent)
            database = AnalysisEngine.analyze(parsed)
        }
        let report = try MillResearchEngine.analyzeHistoricalLogs(
            in: directory,
            database: database,
            limit: scanLimit,
            histogramLimit: histogramLimit,
            discoveryOptions: MillLogDiscovery.Options(
                directory: directory,
                keepLogsOnly: keepLogsOnly
            )
        )
        print("Discovered \(report.discoveredLogs) logs; scanned \(report.scannedLogs) in \(directory.path)")
        print("KEEP logs only: \(keepLogsOnly ? "yes" : "no")")
        print("Histogram limit: \(histogramLimit == 0 ? "unlimited" : String(histogramLimit))")
        for line in report.skipRecommendations {
            print("- \(line)")
        }
        print("\nTop 68k offsets (\(report.top68kOffsets.count) total):")
        for entry in report.top68kOffsets.prefix(10) {
            let rec = entry.recommendation.map { " — \($0)" } ?? ""
            print("  0x\(String(format: "%X", entry.offset))  \(entry.count)×\(rec)")
        }
        print("\nTop PPC offsets (\(report.topPPCOffsets.count) total):")
        for entry in report.topPPCOffsets.prefix(10) {
            print("  0x\(String(format: "%X", entry.offset))  \(entry.count)×")
        }
    }

    private static func runDecodeROM(_ args: [String]) throws {
        guard args.count >= 2 else {
            throw CLIError.usage("decode-rom <rom.tbxi> <MacROM.bin>")
        }
        let data = try Data(contentsOf: URL(fileURLWithPath: args[0]))
        let decoded = try MacROMImageDecoder.decode(data)
        try decoded.image.write(to: URL(fileURLWithPath: args[1]))
        print("Wrote \(decoded.image.count) bytes (\(decoded.source), NewWorld=\(decoded.newWorldOK))")
    }

    private static func runCompareDisasm(_ args: [String]) throws {
        guard args.count >= 2 else {
            throw CLIError.usage("compare-disasm <rom.tbxi> <rom-offset-hex>")
        }
        let data = try Data(contentsOf: URL(fileURLWithPath: args[0]))
        let offsetText = args[1].replacingOccurrences(of: "0x", with: "", options: .caseInsensitive)
        guard let offset = UInt64(offsetText, radix: 16) else {
            throw CLIError.message("Invalid offset: \(args[1])")
        }
        let decoded = try MacROMImageDecoder.decode(data)
        guard let result = DisasmCompare.compareAtROMOffset(offset, macROM: decoded.image) else {
            throw CLIError.message("Offset out of range")
        }
        print("rom_disasm-style compare at ROM+0x\(String(format: "%X", result.romOffset)) ISA=\(result.isa.rawValue)")
        for line in result.lines {
            let tagText = line.tags.isEmpty ? "" : "  ; \(line.tags.joined(separator: ","))"
            let mill = line.millable ? "millable" : "hard"
            print("\(line.formatted)  ; \(mill)\(tagText)")
        }
    }

    private static func runDiffROM(_ args: [String]) throws {
        guard args.count >= 2 else {
            throw CLIError.usage("diff-rom <rom-a.tbxi> <rom-b.tbxi>")
        }
        let result = try ROMDiff.compareFiles(
            at: URL(fileURLWithPath: args[0]),
            rightURL: URL(fileURLWithPath: args[1])
        )
        print("Changed bytes: \(result.changedBytes)")
        print("Left NewWorld: \(result.leftNewWorldOK)  Right NewWorld: \(result.rightNewWorldOK)")
        for change in result.firstChanges.prefix(20) {
            let oldText = change.oldByte.map { String(format: "%02X", $0) } ?? "--"
            let newText = change.newByte.map { String(format: "%02X", $0) } ?? "--"
            print("  ROM+0x\(String(format: "%X", change.offset)): \(oldText) -> \(newText)")
        }
    }

    private static func runBuildContext(_ args: [String]) throws {
        let jsonOutput = args.contains("--json")
        let positional = args.filter { !$0.hasPrefix("--") }
        guard positional.count >= 2 else {
            throw CLIError.usage("build-context <rom.tbxi> <address> [--json]")
        }
        let (parsed, database, macROM) = try loadROM(at: positional[0])
        guard let parsedAddress = AddressTranslation.parse(positional[1]) else {
            throw CLIError.message("Could not parse address: \(positional[1])")
        }
        let address = parsedAddress.programAddress
        let reachability = MillReachabilityEngine.analyze(target: address, database: database)
        let context = MillAnalysisContextBuilder.make(
            address: address,
            parsed: parsed,
            database: database,
            macROM: macROM,
            reachability: reachability
        )
        if jsonOutput {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            print(String(data: try encoder.encode(context), encoding: .utf8) ?? "")
        } else {
            print(MillAnalysisContextBuilder.markdown(from: context))
        }
    }

    private static func runBuildGrokPack(_ args: [String]) throws {
        guard let outputIndex = args.firstIndex(of: "-o"), outputIndex + 1 < args.count else {
            throw CLIError.usage("build-grok-pack <rom.tbxi> <address> -o <output-dir>")
        }
        let outputPath = args[outputIndex + 1]
        let positional = args.enumerated().compactMap { index, value -> String? in
            if value.hasPrefix("-") { return nil }
            if index == outputIndex + 1 { return nil }
            return value
        }
        guard positional.count >= 2 else {
            throw CLIError.usage("build-grok-pack <rom.tbxi> <address> -o <output-dir>")
        }
        let (parsed, database, macROM) = try loadROM(at: positional[0])
        guard let parsedAddress = AddressTranslation.parse(positional[1]) else {
            throw CLIError.message("Could not parse address: \(positional[1])")
        }
        let address = parsedAddress.programAddress
        let reachability = MillReachabilityEngine.analyze(target: address, database: database)
        let context = MillAnalysisContextBuilder.make(
            address: address,
            parsed: parsed,
            database: database,
            macROM: macROM,
            reachability: reachability
        )
        let bundle = MillEscalationPackBuilder.build(
            context: context,
            classification: nil,
            annotation: nil
        )
        let directory = URL(fileURLWithPath: outputPath, isDirectory: true)
        try MillEscalationPackBuilder.write(bundle: bundle, to: directory)
        print("Wrote pack-escalation.md and grok-prompt.md to \(directory.path)")
    }

    private static func runBuildGrokPackFromLog(_ args: [String]) throws {
        guard let outputIndex = args.firstIndex(of: "-o"), outputIndex + 1 < args.count else {
            throw CLIError.usage("build-grok-pack-from-log <log> <address> -o <output-dir> --rom <rom.tbxi>")
        }
        let outputPath = args[outputIndex + 1]
        var romPath: String?
        if let romIndex = args.firstIndex(of: "--rom"), romIndex + 1 < args.count {
            romPath = args[romIndex + 1]
        }
        let positional = args.enumerated().compactMap { index, value -> String? in
            if value.hasPrefix("-") { return nil }
            if index == outputIndex + 1 { return nil }
            if let romPath, value == romPath { return nil }
            return value
        }
        guard positional.count >= 2, let romPath else {
            throw CLIError.usage("build-grok-pack-from-log <log> <address> -o <output-dir> --rom <rom.tbxi>")
        }
        let logURL = URL(fileURLWithPath: positional[0])
        guard let parsedAddress = AddressTranslation.parse(positional[1]) else {
            throw CLIError.message("Could not parse address: \(positional[1])")
        }
        let (parsed, database, macROM) = try loadROM(at: romPath)
        let bundle = try MillLogEscalationExporter.buildPack(
            logURL: logURL,
            address: parsedAddress.programAddress,
            parsed: parsed,
            database: database,
            macROM: macROM
        )
        let directory = URL(fileURLWithPath: outputPath, isDirectory: true)
        try MillEscalationPackBuilder.write(bundle: bundle, to: directory)
        print("Wrote pack-escalation.md and grok-prompt.md to \(directory.path)")
    }

    private static func runAnalyzeLog(_ args: [String]) throws {
        var romPath: String?
        var logPath: String?
        var index = 0
        while index < args.count {
            switch args[index] {
            case "--rom":
                index += 1
                guard index < args.count else { throw CLIError.usage("analyze-log <log> [--rom <rom.tbxi>]") }
                romPath = args[index]
            default:
                if !args[index].hasPrefix("--") {
                    logPath = args[index]
                }
            }
            index += 1
        }
        guard let logPath else {
            throw CLIError.usage("analyze-log <log> [--rom <rom.tbxi>]")
        }
        var database = AnalysisDatabase.empty
        if let romPath {
            let loaded = try loadROM(at: romPath)
            database = loaded.1
        }
        let report = try MillResearchEngine.analyzeLog(
            at: URL(fileURLWithPath: logPath),
            database: database
        )
        print("Analyzed \(logPath)")
        print("68k offsets: \(report.top68kOffsets.count)  launchA9F2: \(report.launchA9F2Count)/\(report.launchA9F2Logs)  loadSeg66(false): \(report.loadSeg66Logs)  overlay: \(report.getNewDialogOverlayHits)")
        print("getNewDialogStubHits (ROM 0x5C86C): \(report.getNewDialogStubHits) — not toast PEF dialog ids")
        if !report.pefDialogHistogram.isEmpty {
            print("\nPEF installer dialog histogram:")
            for line in report.pefDialogHistogram.summaryLines {
                print("  \(line)")
            }
        } else if report.launchA9F2Logs > 0 {
            print("\nPEF installer dialog histogram: (none — pre-PEF or no ShowWindow/ModalDialog/GetNewDialog stamps)")
        }
        if !report.pefHostedLocationSamples.isEmpty {
            print("\nHosted PEF samples:")
            for sample in report.pefHostedLocationSamples.prefix(5) {
                print("  \(sample)")
            }
        }
        for entry in report.skipRecommendations.prefix(4) {
            print("- \(entry)")
        }
        print("\nTop 68k ROM heat (skip-68k candidates — not installer dialogs):")
        for entry in report.top68kOffsets.prefix(10) {
            print("  0x\(String(format: "%X", entry.offset))  \(entry.count)×")
        }
        if let hostedPC = firstHostedPC(in: report) {
            print("\nResViewer disasm @ \(String(format: "0x%08X", hostedPC)):")
            if let client = try? ToastCatalogLookup.Client.resolved(),
               let lines = try? client.disasmAtHostedPC(hostedPC) {
                for line in lines.prefix(MillAnalysisContextBuilder.defaultDisasmLineLimit) {
                    print(line)
                }
            } else {
                print("  (ResViewerCLI unavailable — set RESVIEWER_CLI and RESVIEWER_TOAST)")
            }
        }
    }

    private static func firstHostedPC(in report: MillResearchEngine.HistoricalReport) -> UInt64? {
        for sample in report.pefHostedLocationSamples {
            guard let range = sample.range(of: "pc=0x") else { continue }
            let suffix = sample[range.upperBound...]
            let hex = suffix.prefix(while: { $0.isHexDigit || ($0 >= "A" && $0 <= "F") || ($0 >= "a" && $0 <= "f") })
            if let value = UInt64(hex, radix: 16) {
                return value
            }
        }
        return nil
    }

    private static func runExportAnnotations(_ args: [String]) throws {
        guard let outputIndex = args.firstIndex(of: "-o"), outputIndex + 1 < args.count else {
            throw CLIError.usage("export-annotations <store.json> -o <output-dir>")
        }
        let outputPath = args[outputIndex + 1]
        let positional = args.enumerated().compactMap { index, value -> String? in
            if value.hasPrefix("-") { return nil }
            if index == outputIndex + 1 { return nil }
            return value
        }
        guard let storePath = positional.first else {
            throw CLIError.usage("export-annotations <store.json> -o <output-dir>")
        }
        let data = try Data(contentsOf: URL(fileURLWithPath: storePath))
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let annotations = try decoder.decode([MillAnnotation].self, from: data)
        let approvedCount = annotations.filter { $0.status == .approved }.count
        let romKey = URL(fileURLWithPath: storePath).deletingPathExtension().lastPathComponent
        let url = try MillAnnotationExport.write(
            romKey: romKey,
            annotations: annotations,
            to: URL(fileURLWithPath: outputPath, isDirectory: true)
        )
        print("Exported \(approvedCount) approved annotation(s) to \(url.path)")
        if approvedCount == 0 {
            fputs("Warning: no approved entries — classify in the app, then Approve before export feeds mill_apply.\n", stderr)
        }
    }

    private struct LogAnalysisOptions {
        var directoryPath: String
        var outputPath: String
        var romPath: String?
        var scanLimit: Int
        var histogramLimit: Int
        var annotationsPath: String?
    }

    private static func parseLogAnalysisOptions(
        _ args: [String],
        command: String
    ) throws -> LogAnalysisOptions {
        guard let outputIndex = args.firstIndex(of: "-o"), outputIndex + 1 < args.count else {
            throw CLIError.usage("\(command) [log-directory] -o <output-dir> [--rom <rom.tbxi>] [--limit N] [--histogram-limit N] [--annotations <store.json>]")
        }
        var directoryPath = MillResearchEngine.defaultLogDirectory.path
        var romPath: String?
        var scanLimit = MillResearchEngine.defaultScanLimit
        var histogramLimit = MillResearchEngine.defaultHistogramLimit
        var annotationsPath: String?
        let outputPath = args[outputIndex + 1]
        var index = 0
        while index < args.count {
            switch args[index] {
            case "-o":
                index += 2
                continue
            case "--rom":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) [log-directory] -o <output-dir>") }
                romPath = args[index]
            case "--limit":
                index += 1
                guard index < args.count, let value = Int(args[index]) else {
                    throw CLIError.usage("\(command) [log-directory] -o <output-dir> [--limit N]")
                }
                scanLimit = value
            case "--histogram-limit":
                index += 1
                guard index < args.count, let value = Int(args[index]) else {
                    throw CLIError.usage("\(command) [log-directory] -o <output-dir> [--histogram-limit N]")
                }
                histogramLimit = value
            case "--annotations":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) [log-directory] -o <output-dir> [--annotations <store.json>]") }
                annotationsPath = args[index]
            default:
                if !args[index].hasPrefix("-"), index != outputIndex + 1 {
                    directoryPath = args[index]
                }
            }
            index += 1
        }
        return LogAnalysisOptions(
            directoryPath: directoryPath,
            outputPath: outputPath,
            romPath: romPath,
            scanLimit: scanLimit,
            histogramLimit: histogramLimit,
            annotationsPath: annotationsPath
        )
    }

    private static func loadAnnotations(from storePath: String?) throws -> ([MillAnnotation], String) {
        guard let storePath else { return ([], "unknown") }
        let storeURL = URL(fileURLWithPath: storePath)
        let data = try Data(contentsOf: storeURL)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let annotations = try decoder.decode([MillAnnotation].self, from: data)
        let romKey = storeURL.deletingPathExtension().lastPathComponent
        return (annotations, romKey)
    }

    private static func analyzeLogsForExport(options: LogAnalysisOptions) throws -> (MillResearchEngine.HistoricalReport, AnalysisDatabase) {
        let directory = URL(fileURLWithPath: options.directoryPath)
        var database = AnalysisDatabase.empty
        if let romPath = options.romPath {
            let data = try Data(contentsOf: URL(fileURLWithPath: romPath))
            let parsed = ROMParser.parse(data: data, fileName: URL(fileURLWithPath: romPath).lastPathComponent)
            database = AnalysisEngine.analyze(parsed)
        }
        let report = try MillResearchEngine.analyzeHistoricalLogs(
            in: directory,
            database: database,
            limit: options.scanLimit,
            histogramLimit: options.histogramLimit
        )
        return (report, database)
    }

    private static func runExportHistogram(_ args: [String]) throws {
        let options = try parseLogAnalysisOptions(args, command: "export-histogram")
        let (report, _) = try analyzeLogsForExport(options: options)
        let (annotations, romKey) = try loadAnnotations(from: options.annotationsPath)
        let directory = URL(fileURLWithPath: options.outputPath, isDirectory: true)
        let url = try MillHistogramExport.write(
            romKey: romKey,
            report: report,
            annotations: annotations,
            to: directory
        )
        print("Exported histogram (\(report.top68kOffsets.count) 68k entries) to \(url.path)")
    }

    private struct ToastCatalogOptions {
        var configuration: ToastCatalogLookup.Configuration
        var path: String?
        var type: String?
        var id: Int?
        var format: String
        var offset: UInt64?
        var disasm: ToastCatalogLookup.DisasmWindow?
        var textOutput: Bool
    }

    private static func parseToastCatalogOptions(_ args: [String], command: String) throws -> ToastCatalogOptions {
        var cliPath: String?
        var toastPath: String?
        var catalogPath: String?
        var offset: UInt64?
        var type: String?
        var id: Int?
        var format = "json"
        var textOutput = false
        var pc: UInt64?
        var base: UInt64?
        var at: UInt64?
        var count: Int?
        var index = 0
        while index < args.count {
            switch args[index] {
            case "--cli":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) --cli <path>") }
                cliPath = args[index]
            case "--toast":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) --toast <image>") }
                toastPath = args[index]
            case "--path":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) --path <HFS/path>") }
                catalogPath = args[index]
            case "--type":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) --type <FourCC>") }
                type = args[index]
            case "--id":
                index += 1
                guard index < args.count, let parsed = Int(args[index]) else {
                    throw CLIError.usage("\(command) --id <integer>")
                }
                id = parsed
            case "--format":
                index += 1
                guard index < args.count else { throw CLIError.usage("\(command) --format json|rez|hex|disasm") }
                format = args[index]
            case "--offset":
                index += 1
                guard index < args.count, let parsed = parseCLIAddress(args[index]) else {
                    throw CLIError.usage("\(command) --offset <integer>")
                }
                offset = parsed
            case "--pc":
                index += 1
                guard index < args.count, let parsed = parseCLIAddress(args[index]) else {
                    throw CLIError.usage("\(command) --pc <hex-or-decimal>")
                }
                pc = parsed
            case "--base":
                index += 1
                guard index < args.count, let parsed = parseCLIAddress(args[index]) else {
                    throw CLIError.usage("\(command) --base <hex-or-decimal>")
                }
                base = parsed
            case "--at":
                index += 1
                guard index < args.count, let parsed = parseCLIAddress(args[index]) else {
                    throw CLIError.usage("\(command) --at <hex-or-decimal>")
                }
                at = parsed
            case "--count":
                index += 1
                guard index < args.count, let parsed = Int(args[index]) else {
                    throw CLIError.usage("\(command) --count <integer>")
                }
                count = parsed
            case "--text":
                textOutput = true
            default:
                throw CLIError.usage("Unknown or unexpected argument for \(command): \(args[index])")
            }
            index += 1
        }
        guard let configuration = ToastCatalogLookup.resolveConfiguration(
            cliURL: cliPath.map { URL(fileURLWithPath: $0) },
            toastImageURL: toastPath.map { URL(fileURLWithPath: $0) }
        ) else {
            throw CLIError.message(
                "ResViewerCLI or toast image not found. Build ResViewerCLI or set RESVIEWER_CLI and RESVIEWER_TOAST."
            )
        }
        return ToastCatalogOptions(
            configuration: configuration,
            path: catalogPath,
            type: type,
            id: id,
            format: format,
            offset: offset,
            disasm: ToastCatalogLookup.DisasmWindow(pc: pc, base: base, at: at, count: count),
            textOutput: textOutput
        )
    }

    private static func parseCLIAddress(_ text: String) -> UInt64? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.lowercased().hasPrefix("0x") {
            return UInt64(trimmed.dropFirst(2), radix: 16)
        }
        return UInt64(trimmed)
    }

    private static func runToastLS(_ args: [String]) throws {
        let options = try parseToastCatalogOptions(args, command: "toast-ls")
        let client = ToastCatalogLookup.Client(configuration: options.configuration)
        let entries = try client.ls(path: options.path)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        print(String(data: try encoder.encode(entries), encoding: .utf8) ?? "[]")
        print("\n\(entries.count) entries")
    }

    private static func runToastGet(_ args: [String]) throws {
        let options = try parseToastCatalogOptions(args, command: "toast-get")
        guard let type = options.type, let id = options.id else {
            throw CLIError.usage("toast-get --type T --id N [--path HFS/path]")
        }
        let client = ToastCatalogLookup.Client(configuration: options.configuration)
        if options.format == "disasm", options.textOutput {
            let text = try client.getDisasmText(
                type: type,
                id: id,
                path: options.path,
                disasm: options.disasm ?? ToastCatalogLookup.DisasmWindow()
            )
            print(text)
            return
        }
        let result = try client.get(
            type: type,
            id: id,
            path: options.path,
            format: options.format,
            offset: options.offset,
            disasm: options.disasm
        )
        if options.textOutput, let disasm = result.disasm, !disasm.isEmpty {
            print(disasm.joined(separator: "\n"))
            return
        }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        print(String(data: try encoder.encode(result), encoding: .utf8) ?? "{}")
    }

    private static func runExportPipeline(_ args: [String]) throws {
        let options = try parseLogAnalysisOptions(args, command: "export-pipeline")
        let (report, _) = try analyzeLogsForExport(options: options)
        let (annotations, romKey) = try loadAnnotations(from: options.annotationsPath)
        let directory = URL(fileURLWithPath: options.outputPath, isDirectory: true)
        let result = try MillPipelineExport.write(
            romKey: romKey,
            report: report,
            annotations: annotations,
            to: directory
        )
        print("Exported macemu pipeline to \(result.directory.path)")
        print("- \(result.histogramURL.lastPathComponent) (\(report.top68kOffsets.count) 68k entries)")
        print("- \(result.annotationsURL.lastPathComponent)")
        print("- \(result.researchReportURL.lastPathComponent)")
        print("- \(result.manifestURL.lastPathComponent)")
    }

    static func loadROM(at path: String) throws -> (ParsedROM, AnalysisDatabase, Data?) {
        let romURL = URL(fileURLWithPath: path)
        let data = try Data(contentsOf: romURL)
        let parsed = ROMParser.parse(data: data, fileName: romURL.lastPathComponent)
        let database = AnalysisEngine.analyze(parsed)
        let macROM = try? MacROMImageDecoder.decode(data).image
        return (parsed, database, macROM)
    }

    enum CLIError: Error, LocalizedError {
        case usage(String)
        case message(String)

        var errorDescription: String? {
            switch self {
            case .usage(let text): return "Usage: \(text)"
            case .message(let text): return text
            }
        }
    }
}
