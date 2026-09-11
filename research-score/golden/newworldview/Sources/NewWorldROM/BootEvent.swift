import Foundation

/// One New World boot event, from either the QEMU golden stream (`nwgolden`
/// plugin) or a SheepShaver `NW-BOOT` log. The two are compared on this type.
public struct BootEvent: Sendable, Equatable {
    public enum Kind: Sendable, Equatable, Hashable {
        /// PPC exception / interrupt steered to `vector` (0x300 DSI, 0x400 ISI,
        /// 0x500 external, 0x600 alignment, 0x700 program, 0x800 FPU,
        /// 0x900 decrementer, 0xC00 system call, 0xF20 AltiVec).
        case exception(vector: UInt16)
        /// 68k A-line dispatch inside the ROM emulator.
        case aline(op: UInt16)

        public var channel: String {
            switch self {
            case .exception(let vector): return String(format: "X%03X", vector)
            case .aline: return "A"
            }
        }

        public var display: String {
            switch self {
            case .exception(let vector):
                return "\(BootEvent.vectorName(vector)) (0x\(String(format: "%03X", vector)))"
            case .aline(let op):
                return "A-line \(String(format: "%04X", op)) \(ATrapTable.name(for: op))"
            }
        }
    }

    public var kind: Kind
    /// Raw PC as logged: faulting PC for exceptions, 68k address of the A-line
    /// word for dispatches. `nil` when the log did not carry one.
    public var pc: UInt64?
    /// DAR for 0x300/0x600, SRR1 for 0x700, else nil.
    public var extra: UInt64?
    /// Vector address the CPU was steered to (QEMU only; shows MSR[IP]).
    public var target: UInt64?
    /// 1-based line in the source file.
    public var line: Int

    public init(kind: Kind, pc: UInt64?, extra: UInt64? = nil, target: UInt64? = nil, line: Int) {
        self.kind = kind
        self.pc = pc
        self.extra = extra
        self.target = target
        self.line = line
    }

    public static func vectorName(_ vector: UInt16) -> String {
        switch vector {
        case 0x100: return "RESET"
        case 0x200: return "MCHECK"
        case 0x300: return "DSI"
        case 0x400: return "ISI"
        case 0x500: return "EXTERNAL"
        case 0x600: return "ALIGN"
        case 0x700: return "PROGRAM"
        case 0x800: return "FPU"
        case 0x900: return "DEC"
        case 0xC00: return "SC"
        case 0xD00: return "TRACE"
        case 0xF00: return "PERFMON"
        case 0xF20: return "VPU"
        case 0x1000, 0x1100, 0x1200: return "TLBMISS"
        case 0x1300: return "IABR"
        case 0x1400: return "SMI"
        case 0x1600: return "VPUASSIST"
        default: return String(format: "VEC%03X", vector)
        }
    }
}

/// Where a logged address lives, after mapping each side's layout onto the
/// 4 MiB MacROM image. Both QEMU (mac99) and SheepShaver run the same ROM
/// bytes, so ROM offsets are the common coordinate; RAM addresses are not.
public struct BootAddress: Sendable, Equatable {
    public enum Region: String, Sendable {
        /// 68k Toolbox part of the image (MacROM+0x000000 … +0x300000).
        case rom68k
        /// PowerPC part (ConfigInfo / NanoKernel / emulator, MacROM+0x300000 … +0x400000).
        case romPPC
        case openBIOS
        case ram
        case unknown
    }

    public var raw: UInt64
    public var region: Region
    /// MacROM file offset when `region` is `.rom68k` / `.romPPC`.
    public var romOffset: UInt64?

    public var display: String {
        switch region {
        case .rom68k, .romPPC:
            return String(format: "ROM+0x%06X", romOffset ?? 0)
        case .openBIOS:
            return String(format: "OpenBIOS 0x%08X", raw)
        case .ram:
            return String(format: "RAM 0x%08X", raw)
        case .unknown:
            return String(format: "0x%08X", raw)
        }
    }
}

/// Address layout of one boot side.
public struct BootLayout: Sendable, Equatable {
    public var name: String
    /// Base of the 68k Toolbox ROM as seen by 68k code (A-line `pc`).
    public var rom68kBase: UInt64
    /// Base at which MacROM+0x300000 (the PPC part) executes.
    public var ppcPartBase: UInt64
    public var ppcPartSize: UInt64 = 0x10_0000
    /// Optional second mapping of the whole 4 MiB image (SheepShaver runs the
    /// NK straight out of the image at `ROMBase`).
    public var fullImageBase: UInt64?
    /// RAM copy of the NanoKernel (MacROM+0x310000 … +0x360000). On mac99 the
    /// NK relocates itself to 0x00F10000; exceptions are then raised from RAM.
    public var nkCopyBase: UInt64?
    public var nkCopySize: UInt64 = 0x5_0000
    /// PCs at or above this are firmware (OpenBIOS), not the Mac ROM.
    public var firmwareBase: UInt64?

    /// QEMU `-M mac99`: 68k ROM at 0xFFC00000, PPC part relocated to 0x68000000,
    /// NK copy at 0x00F10000 (`00f146e4` = MacROM+0x3146E4), OpenBIOS at 0xFFF00000.
    public static let qemuMac99 = BootLayout(
        name: "qemu-mac99",
        rom68kBase: 0xFFC0_0000,
        ppcPartBase: 0x6800_0000,
        fullImageBase: nil,
        nkCopyBase: 0x00F1_0000,
        firmwareBase: 0xFFF0_0000
    )

    /// SheepShaver New World: image at 0x50000000 (68k and NK run from it),
    /// PPC-part copy at 0x50400000 (`5046e8c0` = MacROM+0x36E8C0).
    public static let sheepShaver = BootLayout(
        name: "sheepshaver",
        rom68kBase: 0x5000_0000,
        ppcPartBase: 0x5040_0000,
        fullImageBase: 0x5000_0000,
        nkCopyBase: nil,
        firmwareBase: nil
    )

    public init(
        name: String,
        rom68kBase: UInt64,
        ppcPartBase: UInt64,
        ppcPartSize: UInt64 = 0x10_0000,
        fullImageBase: UInt64?,
        nkCopyBase: UInt64? = nil,
        firmwareBase: UInt64?
    ) {
        self.name = name
        self.rom68kBase = rom68kBase
        self.ppcPartBase = ppcPartBase
        self.ppcPartSize = ppcPartSize
        self.fullImageBase = fullImageBase
        self.nkCopyBase = nkCopyBase
        self.firmwareBase = firmwareBase
    }

    /// Classify a PPC PC (exception SRR0).
    public func ppc(_ pc: UInt64) -> BootAddress {
        if pc >= ppcPartBase, pc < ppcPartBase + ppcPartSize {
            return BootAddress(raw: pc, region: .romPPC, romOffset: pc - ppcPartBase + 0x30_0000)
        }
        if let nk = nkCopyBase, pc >= nk, pc < nk + nkCopySize {
            return BootAddress(raw: pc, region: .romPPC, romOffset: pc - nk + 0x31_0000)
        }
        if let full = fullImageBase, pc >= full, pc < full + 0x40_0000 {
            let offset = pc - full
            return BootAddress(raw: pc, region: offset >= 0x30_0000 ? .romPPC : .rom68k, romOffset: offset)
        }
        if let fw = firmwareBase, pc >= fw {
            return BootAddress(raw: pc, region: .openBIOS, romOffset: nil)
        }
        if pc >= rom68kBase, pc < rom68kBase + 0x30_0000 {
            return BootAddress(raw: pc, region: .rom68k, romOffset: pc - rom68kBase)
        }
        if pc < 0x2000_0000 {
            return BootAddress(raw: pc, region: .ram, romOffset: nil)
        }
        return BootAddress(raw: pc, region: .unknown, romOffset: nil)
    }

    /// Classify a 68k PC (A-line word address).
    public func m68k(_ pc: UInt64) -> BootAddress {
        if pc >= rom68kBase, pc < rom68kBase + 0x30_0000 {
            return BootAddress(raw: pc, region: .rom68k, romOffset: pc - rom68kBase)
        }
        if let full = fullImageBase, pc >= full, pc < full + 0x30_0000 {
            return BootAddress(raw: pc, region: .rom68k, romOffset: pc - full)
        }
        if pc < 0x2000_0000 {
            return BootAddress(raw: pc, region: .ram, romOffset: nil)
        }
        return BootAddress(raw: pc, region: .unknown, romOffset: nil)
    }

    public func address(of event: BootEvent) -> BootAddress? {
        guard let pc = event.pc else { return nil }
        switch event.kind {
        case .exception: return ppc(pc)
        case .aline: return m68k(pc)
        }
    }
}
