import Foundation

enum SuperMarioParser {
    private static let padPattern: Data = {
        var data = Data()
        for _ in 0..<100 { data.append(contentsOf: [0x6B, 0x63]) }
        return data
    }()

    private static let comboNames: [UInt64: String] = [
        0x40 << 56: "AppleTalk1",
        0x20 << 56: "AppleTalk2",
        0x30 << 56: "AppleTalk2_NetBoot_FPU",
        0x08 << 56: "AppleTalk2_NetBoot_NoFPU",
        0x10 << 56: "NetBoot",
        0x78 << 56: "AllCombos"
    ]

    static func parse(
        _ data: Data,
        name: String,
        id: String,
        containerOffset: Int?
    ) throws -> ROMNode {
        guard isSuperMario(data) else { throw ROMParseError.unrecognizedFormat }

        let romRsrc = Int(try BinaryCursor.u32be(data, 26, context: "RomRsrc"))
        let romSize = Int(try BinaryCursor.u32be(data, 64, context: "RomSize"))
        var children: [ROMNode] = []

        let mainEnd = min(max(romRsrc, 0), data.count)
        if mainEnd > 0 {
            let mainCode = data.prefix(mainEnd)
            children.append(
                ROMNode.leaf(
                    id: id + "/MainCode",
                    name: "MainCode",
                    data: Data(mainCode),
                    kind: .disassemblable(.m68k),
                    containerOffset: containerOffset,
                    runtimeAddress: 0,
                    metadata: [
                        "RomRsrc": BinaryCursor.hex(romRsrc),
                        "addressSpace": AddressSpace.m68kToolbox.rawValue
                    ]
                )
            )
        }

        if let trapTable = SuperMarioTrapTable.decode(data) {
            let listing = SuperMarioTrapTable.listing(trapTable)
            children.append(
                ROMNode(
                    id: id + "/TrapDispatch",
                    name: "TrapDispatch",
                    kind: .text,
                    data: Data(listing.utf8),
                    text: listing,
                    containerOffset: (containerOffset ?? 0) + trapTable.tableOffset,
                    metadata: [
                        "encoding": trapTable.encoding,
                        "implemented": String(trapTable.implemented.count)
                    ]
                )
            )
        }

        if let padRange = data.lastRange(of: padPattern) {
            let declStart = data.startIndex.distance(to: padRange.upperBound)
            if declStart < data.count {
                let decl = data.suffix(from: data.startIndex + declStart)
                if decl.contains(where: { $0 != 0 }) {
                    children.append(
                        ROMNode.leaf(
                            id: id + "/DeclData",
                            name: "DeclData",
                            data: Data(decl),
                            kind: .binary,
                            containerOffset: (containerOffset ?? 0) + declStart
                        )
                    )
                }
            }
        }

        var listing = """
        # Macintosh ROM resources (SuperMario)
        rom_size=\(BinaryCursor.hex(data.count))
        header_rom_size=\(BinaryCursor.hex(romSize))

        """

        let offsets = extractResourceOffsets(data)
        var rsrcChildren: [ROMNode] = []
        var usedNames = Set<String>()
        var typesNeedingMain = Set<String>()

        for (headerOffset, dataOffset, length) in offsets {
            guard let payload = try? BinaryCursor.slice(data, offset: dataOffset, count: length, context: "resource") else {
                continue
            }
            let combo = (try? BinaryCursor.u64be(data, headerOffset)) ?? 0
            let rsrcType = (try? BinaryCursor.fourCC(data, headerOffset + 16)) ?? "????"
            let rsrcID = Int(Int16(bitPattern: (try? BinaryCursor.u16be(data, headerOffset + 20)) ?? 0))
            let nameData = (try? BinaryCursor.pascalString(data, offset: headerOffset + 23)) ?? Data()
            let rsrcName = BinaryCursor.macRoman(nameData)
            let comboName = comboNames[combo] ?? "0b" + String((combo >> 56), radix: 2)

            if rsrcName == "%A5Init" {
                typesNeedingMain.insert(rsrcType)
            }

            var filename = "\(sanitize(rsrcType))_\(rsrcID)"
            if rsrcName != "Main" || typesNeedingMain.contains(rsrcType) {
                filename += "_" + sanitize(rsrcName)
            }
            if comboName != "AllCombos" {
                filename += "_" + comboName.replacingOccurrences(of: "AppleTalk", with: "AT")
            }
            while filename.contains("__") {
                filename = filename.replacingOccurrences(of: "__", with: "_")
            }
            filename = filename.trimmingCharacters(in: CharacterSet(charactersIn: "_"))
            if payload.starts(withASCII: "Joy!peff") { filename += ".pef" }
            if rsrcType == "PICT" { filename += ".pict" }
            filename = ROMDispatcher.uniquedName(filename, used: &usedNames)

            listing += "type=\(rsrcType) id=\(rsrcID) name=\(rsrcName.isEmpty ? "''" : rsrcName) src=Rsrc/\(filename)"
            if comboName != "AllCombos" { listing += " combo=\(comboName)" }
            listing += "\n"

            let parsedResource = ROMDispatcher.parse(
                payload,
                name: filename,
                id: id + "/Rsrc/" + filename,
                containerOffset: (containerOffset ?? 0) + dataOffset,
                resourceFork: nil
            )
            var resourceMetadata = parsedResource.metadata
            resourceMetadata["rsrcType"] = rsrcType
            resourceMetadata["rsrcID"] = String(rsrcID)
            resourceMetadata["rsrcName"] = rsrcName
            resourceMetadata["combo"] = comboName
            rsrcChildren.append(
                ROMNode(
                    id: parsedResource.id,
                    name: parsedResource.name,
                    kind: parsedResource.kind,
                    data: parsedResource.data,
                    text: parsedResource.text,
                    containerOffset: parsedResource.containerOffset,
                    runtimeAddress: parsedResource.runtimeAddress,
                    children: parsedResource.children,
                    metadata: resourceMetadata
                )
            )
        }

        children.insert(
            ROMNode(
                id: id + "/Romfile",
                name: "Romfile",
                kind: .text,
                data: Data(listing.utf8),
                text: listing,
                containerOffset: containerOffset
            ),
            at: 0
        )

        if !rsrcChildren.isEmpty {
            children.append(
                ROMNode(
                    id: id + "/Rsrc",
                    name: "Rsrc",
                    kind: .folder,
                    data: Data(),
                    text: "\(rsrcChildren.count) ROM resources",
                    children: rsrcChildren
                )
            )
        }

        let summary = """
        SuperMario 68k ROM
        Size: \(data.count) bytes
        Resources: \(rsrcChildren.count)
        """

        return ROMNode(
            id: id,
            name: name,
            kind: .folder,
            data: data,
            text: summary,
            containerOffset: containerOffset,
            children: children
        )
    }

    static func isSuperMario(_ data: Data) -> Bool {
        (data.count == 0x200000 || data.count == 0x300000) && data.firstRange(of: padPattern) != nil
    }

    private static func extractResourceOffsets(_ data: Data) -> [(Int, Int, Int)] {
        guard let romRsrc = try? Int(BinaryCursor.u32be(data, 26)),
              romRsrc + 16 <= data.count,
              let link0 = try? Int(BinaryCursor.u32be(data, romRsrc))
        else { return [] }

        var offsets: [(Int, Int, Int)] = []
        var link = link0
        var seen = Set<Int>()
        while link != 0, !seen.contains(link), link + 24 <= data.count {
            seen.insert(link)
            guard let dataOffset = try? Int(BinaryCursor.u32be(data, link + 12)),
                  dataOffset >= 16,
                  let sizePlus12 = try? Int(BinaryCursor.u32be(data, dataOffset - 8))
            else { break }
            let dataSize = sizePlus12 - 12
            if dataSize > 0, dataOffset + dataSize <= data.count {
                offsets.append((link, dataOffset, dataSize))
            }
            guard let next = try? Int(BinaryCursor.u32be(data, link + 8)) else { break }
            link = next
        }
        return offsets.reversed()
    }

    private static func sanitize(_ string: String) -> String {
        string.map { $0.isLetter || $0.isNumber ? $0 : "_" }.reduce(into: "") { $0.append($1) }
    }
}
