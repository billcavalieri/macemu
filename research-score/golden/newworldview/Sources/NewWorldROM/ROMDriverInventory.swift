import Foundation

/// Inventory of the drivers a New World ROM can supply, cross-referenced
/// (optionally) with an OpenFirmware device tree.
///
/// Two driver populations live in `Mac OS ROM`:
///  * **Parcels** (`prcl` blob, walked by the Trampoline): `prop` parcels
///    attach native drivers (`ndrv`, `shlb`, `nlib` children) to OF nodes
///    whose `name`/`compatible` matches field `a` and whose `device_type`
///    matches field `b`; `node` parcels (CodePrepare / CodeRegister) inject
///    shared libraries unconditionally.
///  * **68k `DRVR` resources** in the SuperMario ROM (`.Sony`, `.AppleCD`,
///    …), reached through the classic Device Manager once the 68k emulator
///    is running.
public struct ROMDriverInventory: Encodable, Sendable {
    public struct CodeItem: Encodable, Sendable, Equatable {
        public let ostype: String
        public let property: String
        public let name: String
        public let size: Int
        public let compressed: Bool
        public let nodeID: String

        public var isDriver: Bool {
            ostype == "ndrv" || property.hasPrefix("driver,") || property.hasPrefix("lanLib,")
        }
    }

    public struct ParcelDriver: Encodable, Sendable, Equatable {
        /// OF `name`/`compatible` value the parcel matches (`a`).
        public let match: String
        /// OF `device_type` the parcel requires (`b`, may be empty).
        public let deviceType: String
        public let flags: String
        public let code: [CodeItem]
        public let nodeID: String

        public init(match: String, deviceType: String, flags: String, code: [CodeItem], nodeID: String) {
            self.match = match
            self.deviceType = deviceType
            self.flags = flags
            self.code = code
            self.nodeID = nodeID
        }

        public var key: String { deviceType.isEmpty ? match : "\(match)/\(deviceType)" }

        public func applies(to node: OpenFirmwareTreeDump.Node) -> String? {
            guard deviceType.isEmpty || node.deviceType == deviceType else { return nil }
            if node.name == match { return "name" }
            if node.compatible.contains(match) { return "compatible" }
            if node.model == match { return "model" }
            if node.parentName == match { return "parent" }
            return nil
        }
    }

    public struct LibraryParcel: Encodable, Sendable, Equatable {
        public let name: String
        public let flags: String
        public let libraries: [CodeItem]
    }

    public struct DRVRResource: Encodable, Sendable, Equatable {
        public let id: Int
        public let name: String
        public let size: Int
        public let combo: String
        public let nodeID: String
    }

    public enum Verdict: String, Encodable, Sendable {
        /// A ROM `prop` parcel matches this node.
        case romParcel
        /// The firmware node itself carries `driver,AAPL,MacOS,PowerPC`.
        case firmwareDriver
        /// Both a parcel and a firmware property apply.
        case romParcelAndFirmware
        /// Device with a `device_type` but no Mac OS driver from either side.
        case none
    }

    public struct NodeMatch: Encodable, Sendable, Equatable {
        public let path: String
        public let name: String
        public let deviceType: String?
        public let compatible: [String]
        public let firmwareDriver: Bool
        /// `parcel key` -> how it matched (`name`, `compatible`, `model`, `parent`).
        public let parcels: [String: String]
        public let verdict: Verdict
    }

    public let parcelDrivers: [ParcelDriver]
    public let libraryParcels: [LibraryParcel]
    public let drvrResources: [DRVRResource]
    public private(set) var nodeMatches: [NodeMatch] = []
    public private(set) var devtreeName: String? = nil

    // MARK: - Building

    public static func build(_ parsed: ParsedROM) -> ROMDriverInventory {
        var drivers: [ParcelDriver] = []
        var libraries: [LibraryParcel] = []
        var drvrs: [DRVRResource] = []

        func codeItems(_ node: ROMNode) -> [CodeItem] {
            node.children.compactMap { child in
                guard let ostype = child.metadata["ostype"] else { return nil }
                guard ostype != "cstr", ostype != "csta" else { return nil }
                return CodeItem(
                    ostype: ostype,
                    property: child.metadata["propertyName"] ?? "",
                    name: child.name,
                    size: child.size,
                    compressed: child.metadata["compress"] == "lzss",
                    nodeID: child.id
                )
            }
        }

        func walk(_ node: ROMNode) {
            switch node.metadata["ostype"] {
            case "prop":
                drivers.append(
                    ParcelDriver(
                        match: node.metadata["a"] ?? "",
                        deviceType: node.metadata["b"] ?? "",
                        flags: node.metadata["flags"] ?? "",
                        code: codeItems(node),
                        nodeID: node.id
                    )
                )
            case "node":
                libraries.append(
                    LibraryParcel(
                        name: node.metadata["a"] ?? node.name,
                        flags: node.metadata["flags"] ?? "",
                        libraries: codeItems(node)
                    )
                )
            default:
                break
            }
            if node.metadata["rsrcType"] == "DRVR" {
                drvrs.append(
                    DRVRResource(
                        id: Int(node.metadata["rsrcID"] ?? "") ?? 0,
                        name: node.metadata["rsrcName"] ?? node.name,
                        size: node.size,
                        combo: node.metadata["combo"] ?? "",
                        nodeID: node.id
                    )
                )
            }
            for child in node.children { walk(child) }
        }
        walk(parsed.root)

        drivers.sort { $0.key < $1.key }
        drvrs.sort { $0.id < $1.id }
        return ROMDriverInventory(parcelDrivers: drivers, libraryParcels: libraries, drvrResources: drvrs)
    }

    // MARK: - Device-tree cross reference

    private static let firmwareOnlyPrefixes = ["/packages", "/openprom", "/aliases", "/options", "/chosen", "/builtin"]

    public mutating func match(against tree: OpenFirmwareTreeDump, name: String? = nil) {
        devtreeName = name
        var matches: [NodeMatch] = []
        for node in tree.nodes {
            if Self.firmwareOnlyPrefixes.contains(where: { node.path == $0 || node.path.hasPrefix($0 + "/") }) {
                continue
            }
            var parcels: [String: String] = [:]
            for driver in parcelDrivers {
                if let how = driver.applies(to: node) { parcels[driver.key] = how }
            }
            let firmware = node.hasFirmwareMacOSDriver
            guard node.deviceType != nil || !parcels.isEmpty || firmware else { continue }
            let verdict: Verdict
            switch (parcels.isEmpty, firmware) {
            case (false, false): verdict = .romParcel
            case (true, true): verdict = .firmwareDriver
            case (false, true): verdict = .romParcelAndFirmware
            case (true, false): verdict = .none
            }
            matches.append(
                NodeMatch(
                    path: node.path,
                    name: node.name,
                    deviceType: node.deviceType,
                    compatible: node.compatible,
                    firmwareDriver: firmware,
                    parcels: parcels,
                    verdict: verdict
                )
            )
        }
        nodeMatches = matches
    }

    /// Parcel drivers that matched no node in the cross-referenced tree.
    public var unusedParcelDrivers: [ParcelDriver] {
        guard !nodeMatches.isEmpty else { return [] }
        let used = Set(nodeMatches.flatMap { $0.parcels.keys })
        return parcelDrivers.filter { !used.contains($0.key) }
    }

    // MARK: - Rendering

    private static func pad(_ s: String, _ width: Int) -> String {
        s.count >= width ? s : s + String(repeating: " ", count: width - s.count)
    }

    public static func render(_ inventory: ROMDriverInventory) -> String {
        var out = ""

        out += "== prop parcels (\(inventory.parcelDrivers.count)) ==\n"
        out += "match                device_type  flags    code\n"
        for driver in inventory.parcelDrivers {
            let code = driver.code.map { item in
                var s = "\(item.ostype):\(item.property.isEmpty ? item.name : item.property)"
                if item.property.hasPrefix("driver,") || item.property.hasPrefix("pef,") || item.property.hasPrefix("lanLib,") {
                    s += " (\(item.name))"
                }
                s += " \(item.size)B"
                return s
            }.joined(separator: "; ")
            out += pad(driver.match, 20) + " " + pad(driver.deviceType.isEmpty ? "-" : driver.deviceType, 12)
                + " " + pad(driver.flags, 8) + " " + code + "\n"
        }

        out += "\n== node parcels (\(inventory.libraryParcels.count)) ==\n"
        for parcel in inventory.libraryParcels {
            let libs = parcel.libraries.map { "\($0.ostype):\($0.name) \($0.size)B" }.joined(separator: ", ")
            out += "\(parcel.name) [\(parcel.flags)]: \(libs)\n"
        }

        out += "\n== 68k DRVR resources (\(inventory.drvrResources.count)) ==\n"
        for drvr in inventory.drvrResources {
            var line = "DRVR " + String(format: "%6d", drvr.id) + "  " + pad(drvr.name, 16) + String(format: "%7dB", drvr.size)
            if drvr.combo != "AllCombos", !drvr.combo.isEmpty { line += "  combo=\(drvr.combo)" }
            out += line + "\n"
        }

        if !inventory.nodeMatches.isEmpty {
            out += "\n== OpenFirmware nodes"
            if let name = inventory.devtreeName { out += " (\(name))" }
            out += " ==\n"
            for match in inventory.nodeMatches {
                let type = match.deviceType ?? "-"
                var detail: String
                switch match.verdict {
                case .romParcel:
                    detail = "ROM: " + match.parcels.sorted { $0.key < $1.key }.map { "\($0.key) via \($0.value)" }.joined(separator: ", ")
                case .firmwareDriver:
                    detail = "firmware driver,AAPL,MacOS,PowerPC"
                case .romParcelAndFirmware:
                    detail = "firmware driver + ROM: " + match.parcels.keys.sorted().joined(separator: ", ")
                case .none:
                    detail = "no Mac OS driver"
                }
                if !match.compatible.isEmpty {
                    detail += "  [compatible: \(match.compatible.joined(separator: ", "))]"
                }
                out += pad(match.path, 56) + " " + pad(type, 18) + " " + detail + "\n"
            }
            let unused = inventory.unusedParcelDrivers
            if !unused.isEmpty {
                out += "\nprop parcels with no matching node: " + unused.map(\.key).joined(separator: ", ") + "\n"
            }
        }
        return out
    }
}
