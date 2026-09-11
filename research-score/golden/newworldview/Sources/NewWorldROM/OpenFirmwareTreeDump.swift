import Foundation

/// Parser for the OpenFirmware device-tree dump produced by
/// `research-score/golden/ofwalk.py` (macemu): a `### show-devs` section
/// followed by one `### <path>` section per node containing the output of
/// `.properties` (one `name<spaces>value` line per property).
///
/// Only the properties the driver inventory needs are decoded as values
/// (`name`, `device_type`, `compatible`, `model`); every other property is
/// retained by name so callers can ask e.g. "does this node carry a
/// `driver,AAPL,MacOS,PowerPC` property?".
public struct OpenFirmwareTreeDump: Sendable, Equatable {
    public struct Node: Sendable, Equatable, Identifiable {
        public var id: String { path }
        public let path: String
        /// Node name without the unit address (`via-pmu@16000` -> `via-pmu`).
        public let name: String
        public let deviceType: String?
        public let compatible: [String]
        public let model: String?
        public let properties: [String]

        public var parentPath: String? {
            guard path != "/" else { return nil }
            guard let slash = path.lastIndex(of: "/") else { return nil }
            let parent = String(path[..<slash])
            return parent.isEmpty ? "/" : parent
        }

        public var parentName: String? {
            guard let parentPath, parentPath != "/" else { return nil }
            return OpenFirmwareTreeDump.strippedName(of: parentPath)
        }

        /// Property the Trampoline uses to pick a Mac OS driver directly from
        /// the firmware node (OpenBIOS provides it for `QEMU,VGA`).
        public var hasFirmwareMacOSDriver: Bool {
            properties.contains("driver,AAPL,MacOS,PowerPC")
        }

        /// Every string a `prop` parcel's `a` field may be compared against.
        public var matchKeys: [String] {
            var keys = [name]
            keys.append(contentsOf: compatible)
            if let model { keys.append(model) }
            return keys
        }
    }

    public let nodes: [Node]

    public func node(at path: String) -> Node? {
        nodes.first { $0.path == path }
    }

    public static func strippedName(of path: String) -> String {
        let last = path.split(separator: "/").last.map(String.init) ?? path
        if let at = last.firstIndex(of: "@") { return String(last[..<at]) }
        return last
    }

    public static func parse(_ text: String) -> OpenFirmwareTreeDump {
        var nodes: [Node] = []
        var currentPath: String?
        var props: [String: String] = [:]
        var order: [String] = []

        func flush() {
            guard let path = currentPath else { return }
            let name = props["name"].flatMap(firstString) ?? strippedName(of: path)
            let node = Node(
                path: path,
                name: name,
                deviceType: props["device_type"].flatMap(firstString),
                compatible: props["compatible"].map(stringList) ?? [],
                model: props["model"].flatMap(firstString),
                properties: order
            )
            nodes.append(node)
            props = [:]
            order = []
        }

        // The serial capture uses CRLF; Swift treats "\r\n" as one Character,
        // so normalise before splitting.
        let normalized = text.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")
        for rawLine in normalized.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = String(rawLine)
            if line.hasPrefix("### ") {
                flush()
                let title = String(line.dropFirst(4)).trimmingCharacters(in: .whitespaces)
                currentPath = title.hasPrefix("/") ? title : nil
                continue
            }
            guard currentPath != nil else { continue }
            if line.isEmpty || line.hasPrefix(".properties") || line == " ok" || line.hasPrefix("0 >") {
                continue
            }
            // `name<spaces>value`; property names never contain spaces.
            guard let space = line.firstIndex(of: " ") else {
                // A property with no value at all.
                if !line.isEmpty && !order.contains(line) {
                    order.append(line)
                    props[line] = ""
                }
                continue
            }
            let key = String(line[..<space])
            let value = line[space...].trimmingCharacters(in: .whitespaces)
            guard !key.isEmpty, !key.hasPrefix("0 "), !order.contains(key) else { continue }
            order.append(key)
            props[key] = value
        }
        flush()
        return OpenFirmwareTreeDump(nodes: nodes)
    }

    // MARK: - Value decoding

    /// `"abc"` -> `abc`; `{"a", "b"}` -> `a`.
    private static func firstString(_ value: String) -> String? {
        stringList(value).first
    }

    /// Decodes a quoted string or a `{"a", "b"}` list. Non-string values
    /// (hex dumps, numbers) yield an empty list.
    private static func stringList(_ value: String) -> [String] {
        var result: [String] = []
        var inQuote = false
        var current = ""
        for ch in value {
            if ch == "\"" {
                if inQuote {
                    result.append(current)
                    current = ""
                }
                inQuote.toggle()
            } else if inQuote {
                current.append(ch)
            }
        }
        return result
    }
}
