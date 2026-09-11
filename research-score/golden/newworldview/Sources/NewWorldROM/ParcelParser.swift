import Foundation

enum ParcelParser {
    static let nodeSize = 88
    static let childSize = 60

    static func parse(
        _ data: Data,
        name: String,
        id: String,
        containerOffset: Int?
    ) throws -> ROMNode {
        guard data.starts(with: Data("prcl".utf8)) else {
            throw ROMParseError.unrecognizedFormat
        }

        let tree = try walkTree(data)
        var children: [ROMNode] = []
        var usedNames = Set<String>()
        var listing = """
        # Toolbox parcels (prcl)
        # 'prop': match and edit an existing DT node
        # 'node': create a new DT node
        # 'rom ': Power Macintosh ROM image
        # 'psum': DT checksum black/whitelists

        """

        for (nodeIndex, (node, childStructs)) in tree.enumerated() {
            listing += "\(node.ostype) flags=\(String(format: "0x%05x", node.flags))"
            if !node.a.isEmpty { listing += " a=\(quote(node.a))" }
            if !node.b.isEmpty { listing += " b=\(quote(node.b))" }
            listing += "\n"

            var adjacentName: String?
            var unpackedChildren: [(PrclChild, Data)] = []
            for child in childStructs {
                let packed = try BinaryCursor.slice(
                    data,
                    offset: child.ptr,
                    count: child.packedLength,
                    context: "parcel child \(child.name)"
                )
                let payload: Data
                if child.compress == "lzss" {
                    payload = LZSSDecoder.decompress(packed)
                } else {
                    payload = packed
                }
                unpackedChildren.append((child, payload))
                if child.name == "code,AAPL,MacOS,name" {
                    adjacentName = BinaryCursor.macRoman(payload).trimmingCharacters(in: CharacterSet(charactersIn: "\0"))
                }
            }

            let parcelFolderName = ROMDispatcher.uniquedName(
                node.a.isEmpty ? "\(node.ostype)-\(nodeIndex)" : node.a,
                used: &usedNames
            )
            var parcelChildren: [ROMNode] = []
            var childUsed = Set<String>()

            for (child, payload) in unpackedChildren {
                listing += "\t\(child.ostype) flags=\(String(format: "0x%05x", child.flags))"
                if !child.name.isEmpty { listing += " name=\(quote(child.name))" }
                if child.compress == "lzss" { listing += " compress=lzss" }
                listing += "\n"

                if child.ostype == "cstr" || child.ostype == "csta" {
                    let strings = payload.split(separator: 0, omittingEmptySubsequences: true)
                        .map { BinaryCursor.macRoman(Data($0)) }
                    for line in strings {
                        listing += "\t\t\(quote(line))\n"
                    }
                    let childName = ROMDispatcher.uniquedName(
                        child.name.isEmpty ? child.ostype : child.name,
                        used: &childUsed
                    )
                    parcelChildren.append(
                        ROMNode(
                            id: id + "/" + parcelFolderName + "/" + childName,
                            name: childName,
                            kind: .text,
                            data: payload,
                            text: strings.joined(separator: "\n"),
                            containerOffset: (containerOffset ?? 0) + child.ptr,
                            metadata: [
                                "ostype": child.ostype,
                                "flags": String(format: "0x%05x", child.flags)
                            ]
                        )
                    )
                    continue
                }

                let guessed = guessName(parent: node, child: child, adjacentName: adjacentName, data: payload)
                var base = guessed.isEmpty ? (child.name.isEmpty ? child.ostype : child.name) : guessed
                if payload.starts(withASCII: "Joy!peff"), !base.hasSuffix(".pef") {
                    base += ".pef"
                }
                let childName = ROMDispatcher.uniquedName(base, used: &childUsed)
                listing += "\t\tsrc=\(quote(childName)) packed=\(child.packedLength) unpacked=\(payload.count)\n"

                let parsedChild = ROMDispatcher.parse(
                    payload,
                    name: childName,
                    id: id + "/" + parcelFolderName + "/" + childName,
                    containerOffset: (containerOffset ?? 0) + child.ptr,
                    resourceFork: nil
                )
                var childMetadata = parsedChild.metadata
                childMetadata["ostype"] = child.ostype
                childMetadata["propertyName"] = child.name
                childMetadata["flags"] = String(format: "0x%05x", child.flags)
                childMetadata["packedLength"] = String(child.packedLength)
                if child.compress == "lzss" { childMetadata["compress"] = "lzss" }
                parcelChildren.append(
                    ROMNode(
                        id: parsedChild.id,
                        name: parsedChild.name,
                        kind: parsedChild.kind,
                        data: parsedChild.data,
                        text: parsedChild.text,
                        containerOffset: parsedChild.containerOffset,
                        runtimeAddress: parsedChild.runtimeAddress,
                        children: parsedChild.children,
                        metadata: childMetadata
                    )
                )
            }

            listing += "\n"
            children.append(
                ROMNode(
                    id: id + "/" + parcelFolderName,
                    name: parcelFolderName,
                    kind: .folder,
                    data: Data(),
                    text: "Parcel \(node.ostype) — \(childStructs.count) children",
                    children: parcelChildren,
                    metadata: [
                        "ostype": node.ostype,
                        "flags": String(format: "0x%05x", node.flags),
                        "a": node.a,
                        "b": node.b
                    ]
                )
            )
        }

        children.insert(
            ROMNode(
                id: id + "/Parcelfile",
                name: "Parcelfile",
                kind: .text,
                data: Data(listing.utf8),
                text: listing,
                containerOffset: containerOffset
            ),
            at: 0
        )

        return ROMNode(
            id: id,
            name: name,
            kind: .folder,
            data: data,
            text: "\(tree.count) parcels, \(data.count) bytes",
            containerOffset: containerOffset,
            children: children
        )
    }

    private struct PrclNode {
        var link: UInt32
        var ostype: String
        var hdrSize: Int
        var flags: UInt32
        var childSize: Int
        var a: String
        var b: String
    }

    private struct PrclChild {
        var ostype: String
        var flags: UInt32
        var compress: String
        var packedLength: Int
        var ptr: Int
        var name: String
    }

    private static func walkTree(_ data: Data) throws -> [(PrclNode, [PrclChild])] {
        var offset = Int(try BinaryCursor.u32be(data, 12, context: "prcl first link"))
        var result: [(PrclNode, [PrclChild])] = []
        var seen = Set<Int>()

        while offset != 0 {
            if seen.contains(offset) {
                throw ROMParseError.truncated("parcel list contains a cycle at \(offset)")
            }
            seen.insert(offset)

            let node = try unpackNode(data, offset: offset)
            var children: [PrclChild] = []
            var childOffset = offset + nodeSize
            let childEnd = offset + node.hdrSize
            let stride = max(node.childSize, childSize)
            while childOffset + childSize <= childEnd {
                children.append(try unpackChild(data, offset: childOffset))
                childOffset += stride
            }
            result.append((node, children))
            offset = Int(node.link)
        }
        return result
    }

    private static func unpackNode(_ data: Data, offset: Int) throws -> PrclNode {
        PrclNode(
            link: try BinaryCursor.u32be(data, offset, context: "prcl.link"),
            ostype: try BinaryCursor.fourCC(data, offset + 4),
            hdrSize: Int(try BinaryCursor.u32be(data, offset + 8)),
            flags: try BinaryCursor.u32be(data, offset + 12),
            childSize: Int(try BinaryCursor.u32be(data, offset + 20)),
            a: try BinaryCursor.cString(data, offset: offset + 24, maxLength: 32),
            b: try BinaryCursor.cString(data, offset: offset + 56, maxLength: 32)
        )
    }

    private static func unpackChild(_ data: Data, offset: Int) throws -> PrclChild {
        let compress = try BinaryCursor.fourCC(data, offset + 8).trimmingCharacters(in: .whitespacesAndNewlines)
        return PrclChild(
            ostype: try BinaryCursor.fourCC(data, offset),
            flags: try BinaryCursor.u32be(data, offset + 4),
            compress: compress.replacingOccurrences(of: "\0", with: ""),
            packedLength: Int(try BinaryCursor.u32be(data, offset + 20)),
            ptr: Int(try BinaryCursor.u32be(data, offset + 24)),
            name: try BinaryCursor.cString(data, offset: offset + 28, maxLength: 32)
        )
    }

    private static func guessName(parent: PrclNode, child: PrclChild, adjacentName: String?, data: Data) -> String {
        if parent.ostype == "rom ", child.ostype == "rom " {
            return "MacROM"
        }
        if let ndrv = PEFParser.suggestName(data) {
            return ndrv
        }
        if parent.flags & 0xF0000 != 0 || child.flags & 0x80 != 0 {
            return child.name
        }
        if child.name.contains("AAPL,MacOS,PowerPC"), let adjacentName {
            return adjacentName
        }
        if child.name == "lanLib,AAPL,MacOS,PowerPC" {
            return parent.a + "_lanLib"
        }
        return ""
    }

    private static func quote(_ string: String) -> String {
        if string.contains(where: { $0.isWhitespace || $0 == "'" || $0 == "\"" }) {
            return "'\(string.replacingOccurrences(of: "'", with: "\\'"))'"
        }
        return string
    }
}
