/*
 *  BenchHost.swift - Host side of SheepBench's mailbox commands (see BenchProtocol.swift and guest/sheepbench/).
 *
 *  Called from ShearsHost.handle() on the emulation thread while the guest waits. The emulator's side (run info,
 *  native kernels, the JIT switch) is C++ in sheepbench_host.cpp; this adds what the app knows (build, host model,
 *  macOS version), the bench_switch pref and the report files.
 *
 *  Prefs: `bench_switch true` lets the guest switch the JIT and SheepForce QuickDraw (off by default). Reports go to
 *  "SheepBench" next to the VM's prefs file.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

@_silgen_name("SheepBenchInfoText")
private func SheepBenchInfoText(_ out: UnsafeMutablePointer<CChar>, _ cap: Int32) -> Int32
@_silgen_name("SheepBenchNative")
private func SheepBenchNative(_ kernel: UInt32, _ iters: UInt32, _ param: UInt32,
                              _ ps: UnsafeMutablePointer<UInt64>, _ checksum: UnsafeMutablePointer<UInt32>) -> Int32
@_silgen_name("SheepBenchRequestModes")
private func SheepBenchRequestModes(_ ppc: Int32, _ m68k: Int32, _ qd: Int32, _ restore: Int32) -> Int32
@_silgen_name("SheepBenchOverrideActive")
private func SheepBenchOverrideActive() -> Int32

final class BenchHost: @unchecked Sendable {
    static let shared = BenchHost()

    private let lock = NSLock()
    private var allowSwitch = false
    private var reportDirectory: URL?
    private var hostLines = ""
    private var assembler = BenchResultAssembler()

    /// Reads the prefs. Call when the guest starts (with ShearsHost.install).
    @MainActor
    func install() {
        let allow = PrefsBridge.bool("bench_switch")
        let prefs = PrefsBridge.path()
        let dir = prefs.isEmpty ? nil : URL(fileURLWithPath: prefs).deletingLastPathComponent().appendingPathComponent("SheepBench")
        let info = Bundle.main.infoDictionary ?? [:]
        var lines = ""
        lines += "build=\(info["SheepGitCommit"] as? String ?? "unknown")\n"
        lines += "app_version=\(info["CFBundleShortVersionString"] as? String ?? "unknown")\n"
        lines += "host_model=\(BenchHost.sysctlString("hw.model") ?? "unknown")\n"
        lines += "host_cpu=\(BenchHost.sysctlString("machdep.cpu.brand_string") ?? "unknown")\n"
        lines += "host_cores=\(ProcessInfo.processInfo.activeProcessorCount)\n"
        let v = ProcessInfo.processInfo.operatingSystemVersion
        lines += "macos=\(v.majorVersion).\(v.minorVersion).\(v.patchVersion)\n"
        lock.lock()
        allowSwitch = allow
        reportDirectory = dir
        hostLines = lines
        lock.unlock()
    }

    /// Whether a SheepBench run has the emulator in other modes than it started with.
    var overrideActive: Bool { SheepBenchOverrideActive() != 0 }

    private static func sysctlString(_ name: String) -> String? {
        var size = 0
        guard sysctlbyname(name, nil, &size, nil, 0) == 0, size > 0 else { return nil }
        var buf = [CChar](repeating: 0, count: size)
        guard sysctlbyname(name, &buf, &size, nil, 0) == 0 else { return nil }
        return String(cString: buf)
    }

    func handle(_ command: ShearsCommand, _ payload: [UInt8]) -> (ShearsStatus, [UInt8]) {
        switch command {
        case .benchInfo:
            var buf = [CChar](repeating: 0, count: Shears.maxPayload)
            let n = Int(SheepBenchInfoText(&buf, Int32(buf.count)))
            lock.lock(); let host = hostLines; let allow = allowSwitch; lock.unlock()
            var text = Array(buf.prefix(max(0, min(n, buf.count))).map { UInt8(bitPattern: $0) })
            text += Array(host.utf8)
            text += Array("bench_switch_allowed=\(allow ? 1 : 0)\n".utf8)
            return (.ok, Array(text.prefix(Shears.maxPayload)))

        case .benchNative:
            guard let req = BenchNativeRequest(payload: payload) else { return (.badRequest, []) }
            var ps: UInt64 = 0
            var sum: UInt32 = 0
            guard SheepBenchNative(req.kernel, req.iterations, req.param, &ps, &sum) == 0 else { return (.unsupported, []) }
            return (.ok, BenchNativeRequest.reply(picoseconds: ps, checksum: sum))

        case .benchSetJIT:
            guard let req = BenchModeRequest(payload: payload) else { return (.badRequest, []) }
            lock.lock(); let allow = allowSwitch; lock.unlock()
            guard allow else { return (.disabled, []) }
            let code = SheepBenchRequestModes(req.ppcJIT ? 1 : 0, req.m68kJIT ? 1 : 0, req.quickDrawGPU ? 1 : 0,
                                              req.restore ? 1 : 0)
            var out: [UInt8] = []
            ShearsCodec.put(&out, UInt32(bitPattern: code))
            return (code == 0 ? .ok : .unsupported, out)

        case .benchResult:
            guard let chunk = BenchResultChunk(payload: payload) else { return (.badRequest, []) }
            lock.lock()
            let outcome = assembler.add(chunk)
            let dir = reportDirectory
            lock.unlock()
            switch outcome {
            case .partial:
                return (.ok, [])
            case .refused:
                return (.badRequest, [])
            case .complete(let text):
                guard let dir else { return (.unsupported, []) }
                return BenchHost.save(text, in: dir) ? (.ok, []) : (.unsupported, [])
            }

        default:
            return (.unsupported, [])
        }
    }

    private static func save(_ text: String, in dir: URL) -> Bool {
        let stamp = ISO8601DateFormatter.string(from: Date(), timeZone: .current,
                                                formatOptions: [.withFullDate, .withTime, .withColonSeparatorInTime])
            .replacingOccurrences(of: ":", with: "-")
        let base = dir.appendingPathComponent("SheepBench \(stamp)")
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            try text.write(to: base.appendingPathExtension("tsv"), atomically: true, encoding: .utf8)
            try BenchReport.json(BenchReport.parse(text)).write(to: base.appendingPathExtension("json"), options: .atomic)
        } catch {
            fputs("SheepBench: could not save the results in \(dir.path): \(error.localizedDescription)\n", stdout)
            fflush(stdout)
            return false
        }
        fputs("SheepBench: results saved \(base.appendingPathExtension("json").path)\n", stdout)
        fflush(stdout)
        return true
    }
}
