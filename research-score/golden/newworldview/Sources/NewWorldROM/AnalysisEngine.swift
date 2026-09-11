import Foundation

public enum AnalysisEngine {
    public static let ppcByteLimit = 512 * 1024
    public static let m68kByteLimit = 1024 * 1024

    public static func analyze(_ parsed: ParsedROM) -> AnalysisDatabase {
        let regions = parsed.root.descendants.filter { node in
            node.children.isEmpty && node.kind.isa != nil && node.data.count >= 2
        }

        var blocks: [MemoryBlock] = []
        var symbols: [ProgramSymbol] = []
        var xrefs: [XRef] = []
        var functionEntries: [String: RecoveredFunction] = [:]
        var occupied: [AddressSpace: [(UInt64, UInt64, String)]] = [:]

        for node in regions {
            guard let isa = node.kind.isa else { continue }
            let space = space(for: node, isa: isa)
            let base = baseAddress(for: node, space: space)
            let limit = byteLimit(for: isa)
            let size = min(node.data.count, limit)
            let block = MemoryBlock(
                name: node.name,
                address: ProgramAddress(space: space, address: base),
                size: size,
                nodeID: node.id,
                isa: isa
            )
            blocks.append(block)
            occupied[space, default: []].append((base, base + UInt64(size), node.id))

            let startAddress = ProgramAddress(space: space, address: base)
            symbols.append(ProgramSymbol(address: startAddress, name: sanitize(node.name), kind: .function, nodeID: node.id))
            functionEntries[startAddress.key] = RecoveredFunction(
                address: startAddress,
                name: sanitize(node.name),
                size: size,
                nodeID: node.id
            )
        }

        seedKnownSymbols(into: &symbols, functionEntries: &functionEntries, blocks: blocks)
        seedTrapImplementations(parsed, into: &symbols, functionEntries: &functionEntries, blocks: blocks)

        for node in regions {
            guard let isa = node.kind.isa else { continue }
            let space = space(for: node, isa: isa)
            let base = baseAddress(for: node, space: space)
            let limit = byteLimit(for: isa)
            let result = DisassemblyService.disassemble(
                node.data,
                isa: isa,
                baseAddress: base,
                byteLimit: limit
            )

            for instruction in result.instructions {
                let from = ProgramAddress(space: space, address: instruction.address)
                if isa == .m68k, let trap = aTrap(in: instruction.bytes) {
                    let trapAddress = ProgramAddress(space: .toolboxTrap, address: UInt64(trap))
                    let trapName = ATrapTable.name(for: trap)
                    symbols.append(ProgramSymbol(address: trapAddress, name: trapName, kind: .trap, nodeID: node.id))
                    xrefs.append(
                        XRef(
                            from: from,
                            to: trapAddress,
                            kind: .trap,
                            fromNodeID: node.id,
                            toSymbol: trapName
                        )
                    )
                }

                guard let flow = flowKind(isa: isa, mnemonic: instruction.mnemonic) else { continue }
                guard let targetValue = branchTarget(in: instruction.operands) else { continue }
                let toSpace = resolveSpace(for: targetValue, preferred: space, occupied: occupied)
                let to = ProgramAddress(space: toSpace, address: targetValue)
                let destNode = nodeID(containing: to, occupied: occupied)
                xrefs.append(
                    XRef(
                        from: from,
                        to: to,
                        kind: flow,
                        fromNodeID: node.id,
                        toNodeID: destNode,
                        toSymbol: nil
                    )
                )
                if flow == .call, let destNode, functionEntries[to.key] == nil {
                    functionEntries[to.key] = RecoveredFunction(
                        address: to,
                        name: String(format: "sub_%08X", targetValue),
                        size: 0,
                        nodeID: destNode
                    )
                }
            }
        }

        let uniqueSymbols = uniquedSymbols(symbols)
        var labeledXrefs: [XRef] = []
        var symbolIndex: [String: ProgramSymbol] = [:]
        for symbol in uniqueSymbols {
            if symbolIndex[symbol.address.key] == nil {
                symbolIndex[symbol.address.key] = symbol
            }
        }
        var seenXRefs = Set<String>()
        for var xref in xrefs {
            if let fromSym = symbolIndex[xref.from.key] { xref.fromSymbol = fromSym.name }
            if let toSym = symbolIndex[xref.to.key] { xref.toSymbol = toSym.name }
            if seenXRefs.insert(xref.id).inserted {
                labeledXrefs.append(xref)
            }
        }

        return AnalysisDatabase(
            blocks: blocks,
            symbols: uniqueSymbols,
            functions: functionEntries.values.sorted { $0.address.address < $1.address.address },
            xrefs: labeledXrefs
        )
    }

    public static func annotatedListing(
        _ instructions: [DisassembledInstruction],
        space: AddressSpace,
        database: AnalysisDatabase
    ) -> String {
        var lines: [String] = []
        for instruction in instructions {
            let address = ProgramAddress(space: space, address: instruction.address)
            var line = instruction.formatted
            if let symbol = database.symbol(at: address) {
                line += "  ; \(symbol.name)"
            }
            let outgoing = database.xrefs(from: address)
            if !outgoing.isEmpty {
                let notes = outgoing.map { xref in
                    let dest = xref.toSymbol ?? xref.to.display
                    return "\(xref.kind.displayName)->\(dest)"
                }
                line += "  ; " + notes.joined(separator: ", ")
            }
            let incoming = database.xrefs(to: address).count
            if incoming > 0 {
                line += "  ; xref in=\(incoming)"
            }
            lines.append(line)
        }
        return lines.joined(separator: "\n")
    }

    public static func space(for node: ROMNode, isa: DisassemblyISA) -> AddressSpace {
        if let raw = node.metadata["addressSpace"], let space = AddressSpace(rawValue: raw) {
            return space
        }
        let path = node.id.lowercased()
        switch isa {
        case .m68k:
            return .m68kToolbox
        case .powerPC:
            if path.contains("macos.elf") || path.contains(".elf") { return .elf }
            if path.contains(".pef") || path.contains("pef") || node.metadata["regionKind"] == "0" {
                return .pef
            }
            if path.contains("macrom") || path.contains("nanokern") || path.contains("hwinit")
                || path.contains("emulator") || path.contains("exception") {
                return .ppcMacROM
            }
            if node.runtimeAddress != nil, node.metadata["regionKind"] != nil {
                return .pef
            }
            return .ppcMacROM
        }
    }

    public static func baseAddress(for node: ROMNode, space: AddressSpace) -> UInt64 {
        if let runtime = node.runtimeAddress { return runtime }
        if space == .ppcMacROM, let macrom = node.metadata["macromOffset"], let value = UInt64(macrom) {
            return AddressSpaces.ppcMacROMBase + value
        }
        return 0
    }

    public static func byteOffset(for address: ProgramAddress, in node: ROMNode) -> Int? {
        guard let isa = node.kind.isa else { return nil }
        let space = space(for: node, isa: isa)
        guard address.space == space else { return nil }
        let base = baseAddress(for: node, space: space)
        guard address.address >= base else { return nil }
        let offset = Int(address.address - base)
        guard offset >= 0, offset < node.data.count else { return nil }
        return offset
    }

    private static func byteLimit(for isa: DisassemblyISA) -> Int {
        switch isa {
        case .powerPC: return ppcByteLimit
        case .m68k: return m68kByteLimit
        }
    }

    private static func flowKind(isa: DisassemblyISA, mnemonic: String) -> XRefKind? {
        let name = mnemonic.lowercased()
        switch isa {
        case .powerPC:
            if ["blr", "blrl", "bctr", "bctrl"].contains(name) { return nil }
            if name == "bl" || name == "bla" { return .call }
            if name.hasPrefix("b") { return .jump }
            return nil
        case .m68k:
            if name.hasPrefix("bsr") || name.hasPrefix("jsr") { return .call }
            if name.hasPrefix("jmp") || name.hasPrefix("bra") || name.hasPrefix("bcc")
                || name.hasPrefix("bcs") || name.hasPrefix("beq") || name.hasPrefix("bne")
                || name.hasPrefix("bge") || name.hasPrefix("blt") || name.hasPrefix("bgt")
                || name.hasPrefix("ble") || name.hasPrefix("bpl") || name.hasPrefix("bmi")
                || name.hasPrefix("bhi") || name.hasPrefix("bls") || name.hasPrefix("bvc")
                || name.hasPrefix("bvs") || name.hasPrefix("bhs") || name.hasPrefix("blo")
                || name.hasPrefix("db") {
                return .jump
            }
            return nil
        }
    }

    private static func branchTarget(in operands: String) -> UInt64? {
        guard let regex = try? NSRegularExpression(pattern: #"0x([0-9A-Fa-f]+)"#) else { return nil }
        let range = NSRange(operands.startIndex..., in: operands)
        let matches = regex.matches(in: operands, range: range)
        var best: UInt64?
        for match in matches {
            guard let hexRange = Range(match.range(at: 1), in: operands),
                  let value = UInt64(operands[hexRange], radix: 16)
            else { continue }
            if value > 0xFF {
                best = value
            } else if best == nil {
                best = value
            }
        }
        return best
    }

    private static func aTrap(in bytes: Data) -> UInt16? {
        guard bytes.count >= 2 else { return nil }
        let word = UInt16(bytes[bytes.startIndex]) << 8 | UInt16(bytes[bytes.startIndex + 1])
        guard word & 0xF000 == 0xA000 else { return nil }
        return word
    }

    private static func resolveSpace(
        for address: UInt64,
        preferred: AddressSpace,
        occupied: [AddressSpace: [(UInt64, UInt64, String)]]
    ) -> AddressSpace {
        if let ranges = occupied[preferred], ranges.contains(where: { address >= $0.0 && address < $0.1 }) {
            return preferred
        }
        for (space, ranges) in occupied {
            if ranges.contains(where: { address >= $0.0 && address < $0.1 }) {
                return space
            }
        }
        if address >= AddressSpaces.ppcMacROMBase && address < AddressSpaces.ppcMacROMBase + AddressSpaces.ppcMacROMSize {
            return .ppcMacROM
        }
        return preferred
    }

    private static func nodeID(
        containing address: ProgramAddress,
        occupied: [AddressSpace: [(UInt64, UInt64, String)]]
    ) -> String? {
        guard let ranges = occupied[address.space] else { return nil }
        return ranges.first { address.address >= $0.0 && address.address < $0.1 }?.2
    }

    private static func seedKnownSymbols(
        into symbols: inout [ProgramSymbol],
        functionEntries: inout [String: RecoveredFunction],
        blocks: [MemoryBlock]
    ) {
        let m68kNode = blocks.first { $0.address.space == .m68kToolbox }?.nodeID ?? ""
        let dialogStub = ProgramAddress(space: .m68kToolbox, address: AddressSpaces.getNewDialogStub)
        symbols.append(
            ProgramSymbol(address: dialogStub, name: "GetNewDialog_stub", kind: .function, nodeID: m68kNode)
        )
        functionEntries[dialogStub.key] = RecoveredFunction(
            address: dialogStub,
            name: "GetNewDialog_stub",
            size: Int(AddressSpaces.getNewDialogStubEnd - AddressSpaces.getNewDialogStub),
            nodeID: m68kNode
        )

        for (trap, _) in ATrapTable.names {
            let address = ProgramAddress(space: .toolboxTrap, address: UInt64(trap))
            symbols.append(
                ProgramSymbol(address: address, name: ATrapTable.name(for: trap), kind: .trap, nodeID: m68kNode)
            )
        }
    }

    /// SuperMario ROM dispatch table: one `_TrapName` function symbol per trap
    /// implemented in the 68k ROM (68k PC -> trap name for `r24` heat maps).
    public static func trapTable(in parsed: ParsedROM) -> SuperMarioTrapTable.Table? {
        for node in parsed.root.descendants where SuperMarioParser.isSuperMario(node.data) {
            if let table = SuperMarioTrapTable.decode(node.data) {
                return table
            }
        }
        return nil
    }

    private static func seedTrapImplementations(
        _ parsed: ParsedROM,
        into symbols: inout [ProgramSymbol],
        functionEntries: inout [String: RecoveredFunction],
        blocks: [MemoryBlock]
    ) {
        guard let table = trapTable(in: parsed) else { return }
        let m68kNode = blocks.first { $0.address.space == .m68kToolbox }?.nodeID ?? ""
        var seen = Set<UInt32>()
        for entry in table.implemented {
            guard let offset = entry.romOffset, seen.insert(offset).inserted else { continue }
            let address = ProgramAddress(space: .m68kToolbox, address: UInt64(offset))
            let name = ATrapTable.name(for: entry.trap)
            symbols.append(ProgramSymbol(address: address, name: name, kind: .function, nodeID: m68kNode))
            if functionEntries[address.key] == nil {
                functionEntries[address.key] = RecoveredFunction(
                    address: address,
                    name: name,
                    size: 0,
                    nodeID: m68kNode
                )
            }
        }
    }

    private static func uniquedSymbols(_ symbols: [ProgramSymbol]) -> [ProgramSymbol] {
        var seen = Set<String>()
        var result: [ProgramSymbol] = []
        for symbol in symbols {
            let key = symbol.address.key + "|" + symbol.name
            if seen.insert(key).inserted {
                result.append(symbol)
            }
        }
        return result.sorted { $0.address.address < $1.address.address }
    }

    private static func sanitize(_ name: String) -> String {
        let mapped = name.map { character -> Character in
            character.isLetter || character.isNumber || character == "_" ? character : "_"
        }
        let joined = String(mapped)
        return joined.isEmpty ? "loc" : joined
    }
}
