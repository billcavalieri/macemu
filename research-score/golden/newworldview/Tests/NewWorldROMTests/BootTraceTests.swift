import Foundation
import NewWorldROM
import Testing

/// S3 golden-reference tooling: QEMU `nwgolden` event import, SheepShaver
/// `NW-BOOT` import, first-divergence diff, A-trap naming, SuperMario trap
/// table decode, OpenFirmware dump parsing and driver matching.
struct BootTraceTests {

    // MARK: - GoldenTraceImporter

    @Test func parsesExceptionAndALineEvents() {
        let x = GoldenTraceImporter.parseEventLine("X E 6806e8c0 00000700 0002d032", lineNumber: 7)
        #expect(x?.kind == .exception(vector: 0x700))
        #expect(x?.pc == 0x6806E8C0)
        #expect(x?.extra == 0x0002D032)
        #expect(x?.target == 0x700)
        #expect(x?.line == 7)

        let dsi = GoldenTraceImporter.parseEventLine("X E 00f15fbc 00000300 68ffffac", lineNumber: 8)
        #expect(dsi?.kind == .exception(vector: 0x300))
        #expect(dsi?.extra == 0x68FFFFAC)

        let a = GoldenTraceImporter.parseEventLine("A a247 ffc07878 0", lineNumber: 9)
        #expect(a?.kind == .aline(op: 0xA247))
        #expect(a?.pc == 0xFFC07878)

        #expect(GoldenTraceImporter.parseEventLine("# nwgolden 1 target=ppc", lineNumber: 1) == nil)
        #expect(GoldenTraceImporter.parseEventLine("T 1700000000000 12 3", lineNumber: 2) == nil)
        let tick = GoldenTraceImporter.parseTick("T 1700000000000 12 3", lineNumber: 2)
        #expect(tick?.exceptions == 12)
        #expect(tick?.alines == 3)
    }

    // MARK: - NWBootLogImporter

    @Test func importsLegacyG3LinesAndCanonicalLines() {
        let log = """
        NW-BOOT G2: MSR[DR] on
        NW-BOOT G3: ISI n=1 SRR0=5046e8c0 to=50313960 msr=0000d032
        NW-BOOT G3: DSI n=1 SRR0=5046e908 DAR=50ffffac to=50313900
        NW-BOOT G3: sc n=1 r0=00000000 SPRG3=00000000 to=50313c00
        NW-BOOT G3: 68k A-line a247 pc=50007878 d0=00000000 a0=00000000
        NW-BOOT X E 5046e8c0 00000700 0002d032
        NW-BOOT A a019 50000678 0
        """
        let result = NWBootLogImporter.parse(log)
        #expect(result.events.count == 6)
        #expect(result.otherBootLines == 1)
        #expect(result.events[0].kind == .exception(vector: 0x400))
        #expect(result.events[0].pc == 0x5046E8C0)
        #expect(result.events[1].kind == .exception(vector: 0x300))
        #expect(result.events[1].extra == 0x50FFFFAC)
        #expect(result.events[2].kind == .exception(vector: 0xC00))
        #expect(result.events[3].kind == .aline(op: 0xA247))
        #expect(result.events[3].pc == 0x50007878)
        #expect(result.events[4].kind == .exception(vector: 0x700))
        #expect(result.events[5].kind == .aline(op: 0xA019))
    }

    // MARK: - Layouts

    @Test func layoutsNormaliseToROMOffsets() {
        let sheep = BootLayout.sheepShaver.ppc(0x5046E8C0)
        let qemu = BootLayout.qemuMac99.ppc(0x6806E8C0)
        #expect(sheep.romOffset == 0x36E8C0)
        #expect(qemu.romOffset == 0x36E8C0)
        #expect(sheep.region == .romPPC)
        #expect(qemu.region == .romPPC)

        // NanoKernel RAM copy in QEMU maps back to MacROM+0x310000.
        let nk = BootLayout.qemuMac99.ppc(0x00F113C0)
        #expect(nk.romOffset == 0x3113C0)

        // 68k ROM at 0xFFC00000 in QEMU, 0x50000000 in SheepShaver.
        #expect(BootLayout.qemuMac99.m68k(0xFFC07878).romOffset == 0x7878)
        #expect(BootLayout.sheepShaver.m68k(0x50007878).romOffset == 0x7878)
    }

    // MARK: - BootTraceDiff

    private func event(_ kind: BootEvent.Kind, pc: UInt64, line: Int) -> BootEvent {
        BootEvent(kind: kind, pc: pc, line: line)
    }

    @Test func diffMatchesSameROMOffsetAcrossLayouts() {
        let sheep = [
            event(.exception(vector: 0x700), pc: 0x5046E8C0, line: 1),
            event(.aline(op: 0xA247), pc: 0x50007878, line: 2)
        ]
        let golden = [
            event(.exception(vector: 0x700), pc: 0x00F113C0, line: 10), // NK priv trap first in QEMU
            event(.exception(vector: 0x700), pc: 0x6806E8C0, line: 11),
            event(.aline(op: 0xA247), pc: 0xFFC07878, line: 12)
        ]
        // Occurrence #1 of X700 differs (NK RAM copy vs emulator twi).
        let report = BootTraceDiff.compare(sheep: sheep, golden: golden)
        #expect(report.divergence?.channel == "X700")
        #expect(report.divergence?.occurrence == 0)

        // Aligning the sheep side to the same occurrence yields a clean match.
        let aligned = [golden[0].withLayoutSheep(), sheep[0], sheep[1]]
        let ok = BootTraceDiff.compare(sheep: aligned, golden: golden)
        #expect(ok.divergence == nil)
        #expect(ok.checked == 3)
    }

    @Test func diffReportsMissingCounterpartWithGoldenContext() {
        let sheep = [event(.exception(vector: 0x400), pc: 0x5046E8C0, line: 121)]
        let golden = [
            event(.exception(vector: 0x700), pc: 0x6806E8C0, line: 1),
            event(.exception(vector: 0x300), pc: 0x6806E908, line: 2)
        ]
        let report = BootTraceDiff.compare(sheep: sheep, golden: golden)
        #expect(report.divergence?.channel == "X400")
        #expect(report.divergence?.golden == nil)
        #expect(report.divergence?.goldenContext.isEmpty == false)
        let text = BootTraceDiff.render(report)
        #expect(text.contains("FIRST DIVERGENCE: X400"))
        #expect(text.contains("ISI"))
    }

    @Test func diffIgnoresAsyncVectorPCs() {
        let sheep = [event(.exception(vector: 0x900), pc: 0x50312000, line: 1)]
        let golden = [event(.exception(vector: 0x900), pc: 0x6806E8C0, line: 1)]
        #expect(BootTraceDiff.compare(sheep: sheep, golden: golden).divergence == nil)
        var strict = BootTraceDiff.Options()
        strict.asyncVectors = []
        #expect(BootTraceDiff.compare(sheep: sheep, golden: golden, options: strict).divergence != nil)
    }

    // MARK: - ATrapTable

    @Test func trapNamesResolveWithFlagsAndFixedSwap() {
        #expect(ATrapTable.name(for: 0xA97C) == "_GetNewDialog")
        #expect(ATrapTable.name(for: 0xA97D) == "_NewDialog")
        #expect(ATrapTable.name(for: 0xA1AD) == "_Gestalt")
        #expect(ATrapTable.name(for: 0xA9F2) == "_Launch")
        #expect(ATrapTable.name(for: 0xA148) == "_PtrZone")      // OS trap with bit 8
        #expect(ATrapTable.name(for: 0xA22E) == "_BlockMoveData") // flagged spelling present in table
        #expect(ATrapTable.canonical(0xA748) == 0xA048)
        #expect(ATrapTable.canonical(0xAD7C) == 0xA97C)
        #expect(ATrapTable.name(for: 0xABE9) == "_ABE9")
    }

    // MARK: - SuperMarioTrapTable

    @Test func decodesLongFormDispatchTable() {
        // Minimal SuperMario header: DispTableOff at 0x22, RomRsrc at 0x1A,
        // RomSize at 0x40, table immediately followed by RomRsrc.
        let tableOffset = 0x100
        let romRsrc = tableOffset + 0x1400
        var rom = Data(count: romRsrc + 0x40)
        func put32(_ value: UInt32, at offset: Int) {
            rom[offset] = UInt8(value >> 24); rom[offset + 1] = UInt8((value >> 16) & 0xFF)
            rom[offset + 2] = UInt8((value >> 8) & 0xFF); rom[offset + 3] = UInt8(value & 0xFF)
        }
        put32(UInt32(romRsrc), at: 0x1A)
        put32(UInt32(tableOffset), at: 0x22)
        put32(UInt32(rom.count), at: 0x40)
        put32(0x2C010, at: tableOffset + Int(0x1F2) * 4)          // A9F2 Launch
        put32(0x62CE6, at: tableOffset + Int(0x17C) * 4)          // A97C GetNewDialog
        put32(0x00678, at: tableOffset + 4096 + Int(0x19) * 4)    // A019 InitZone

        let table = SuperMarioTrapTable.decode(rom)
        #expect(table?.encoding == "long")
        #expect(table?.entry(for: 0xA9F2)?.romOffset == 0x2C010)
        #expect(table?.entry(for: 0xAD7C)?.romOffset == 0x62CE6) // autopop flag folds to A97C
        #expect(table?.entry(for: 0xA019)?.romOffset == 0x678)
        #expect(table?.entry(for: 0xA119)?.romOffset == 0x678)   // OS flag bit folds to A019
        #expect(table?.implemented.count == 3)
        #expect(table?.trap(containing: 0x2C020)?.trap == 0xA9F2)
    }

    // MARK: - OpenFirmwareTreeDump / ROMDriverInventory

    @Test func parsesCRLFDeviceTreeDumpAndMatchesParcels() {
        let dump = """
        ### show-devs\r
        fff60e88 /pci@f2000000 (pci)\r
        ### /pci@f2000000\r
        .properties \r
        name                      "pci"\r
        device_type               "pci"\r
        model                     "AAPL,UniNorth"\r
        compatible                "uni-north"\r
        ### /pci@f2000000/mac-io@c/via-pmu@16000/rtc\r
        .properties \r
        name                      "rtc"\r
        device_type               "rtc"\r
        compatible                "rtc,via-pmu"\r
        ### /pci@f2000000/QEMU,VGA@e\r
        .properties \r
        name                      "QEMU,VGA"\r
        device_type               "display"\r
        compatible                "VGA"\r
        driver,AAPL,MacOS,PowerPC -- 4941 : 4a 6f 79 21 ...\r
         ok\r
        0 > \r
        """
        let tree = OpenFirmwareTreeDump.parse(dump)
        #expect(tree.nodes.count == 3)
        let pci = tree.node(at: "/pci@f2000000")
        #expect(pci?.name == "pci")
        #expect(pci?.compatible == ["uni-north"])
        #expect(pci?.model == "AAPL,UniNorth")
        let rtc = tree.node(at: "/pci@f2000000/mac-io@c/via-pmu@16000/rtc")
        #expect(rtc?.parentName == "via-pmu")
        let vga = tree.node(at: "/pci@f2000000/QEMU,VGA@e")
        #expect(vga?.hasFirmwareMacOSDriver == true)

        let uniNorth = ROMDriverInventory.ParcelDriver(match: "uni-north", deviceType: "pci", flags: "", code: [], nodeID: "p")
        let pmuRTC = ROMDriverInventory.ParcelDriver(match: "via-pmu", deviceType: "rtc", flags: "", code: [], nodeID: "r")
        let cofb = ROMDriverInventory.ParcelDriver(match: "cofb", deviceType: "display", flags: "", code: [], nodeID: "d")
        #expect(uniNorth.applies(to: pci!) == "compatible")
        #expect(pmuRTC.applies(to: rtc!) == "parent")
        #expect(pmuRTC.applies(to: pci!) == nil)
        #expect(cofb.applies(to: vga!) == nil)
    }
}

private extension BootEvent {
    /// Re-express a QEMU-layout event in SheepShaver addresses (same ROM offset).
    func withLayoutSheep() -> BootEvent {
        let offset = BootLayout.qemuMac99.address(of: self)?.romOffset ?? 0
        let pc: UInt64
        switch kind {
        case .aline: pc = 0x50000000 + offset
        case .exception: pc = 0x50000000 + offset
        }
        return BootEvent(kind: kind, pc: pc, extra: extra, target: target, line: line)
    }
}
