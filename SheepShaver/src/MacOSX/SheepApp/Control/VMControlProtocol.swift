/*
 *  VMControlProtocol.swift - The messages the manager (and so the MCP server) sends to a running VM over its control
 *  socket: newline-delimited JSON, one request and one response per line.
 *
 *      -> {"id":1,"op":"mouse_click","x":120,"y":40,"button":"left","count":2}
 *      <- {"id":1,"ok":true,"result":{...}}      or      {"id":1,"ok":false,"error":"why"}
 *
 *  Pure Foundation, no sockets and no emulator: compiled on its own by tools/vms/tests/unit.sh. Everything in a request
 *  is untrusted, so every number and string is range-checked here before anything acts on it.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

enum ControlButton: String, Equatable {
    case left, right
}

enum ControlOp: Equatable {
    case ping
    case status
    case screenshot(maxWidth: Int?)
    case mouseMove(x: Int, y: Int)
    case mouseClick(x: Int?, y: Int?, button: ControlButton, count: Int)
    case mouseDown(button: ControlButton)
    case mouseUp(button: ControlButton)
    case mouseDrag(fromX: Int, fromY: Int, toX: Int, toY: Int, button: ControlButton)
    case typeText(String)
    case pressKey(key: String, modifiers: [String])
    case shutdown(force: Bool)
    /// The Sheep Shears switches on the host side: both nil reads them, a value sets that one.
    case features(edgeRelease: Bool?, clipboard: Bool?)
    /// Where the picture is: in the library window (embedded) or in the VM's own window. nil reads it.
    case display(mode: DisplayMode?)
}

enum DisplayMode: String, Equatable {
    case embedded      // no window of its own; the library window shows the picture
    case window        // the VM's own window
}

struct ControlRequest: Equatable {
    var id: Int
    var op: ControlOp
}

struct ControlError: Error, Equatable {
    var message: String
    init(_ message: String) { self.message = message }
}

enum ControlProtocol {
    static let maxLine = 1 << 20
    static let maxText = 4000
    static let maxCoordinate = 16384

    // MARK: arguments

    /// A JSON number that is a whole number (3 or 3.0), else nil. Booleans and strings are not numbers here.
    static func int(_ value: Any?) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let d = number.doubleValue
        guard d == d.rounded(), abs(d) < 1e9 else { return nil }
        return Int(d)
    }

    private static func coordinate(_ args: [String: Any], _ key: String) throws -> Int {
        guard let v = int(args[key]) else { throw ControlError("\"\(key)\" must be a whole number (guest pixels)") }
        guard (0...maxCoordinate).contains(v) else { throw ControlError("\"\(key)\" is out of range") }
        return v
    }

    private static func optionalCoordinate(_ args: [String: Any], _ key: String) throws -> Int? {
        args[key] == nil ? nil : try coordinate(args, key)
    }

    private static func button(_ args: [String: Any]) throws -> ControlButton {
        guard let raw = args["button"] else { return .left }
        guard let name = raw as? String, let b = ControlButton(rawValue: name.lowercased()) else {
            throw ControlError("\"button\" must be \"left\" or \"right\"")
        }
        return b
    }

    // MARK: requests

    static func parse(_ line: Data) -> Result<ControlRequest, ControlError> {
        guard line.count <= maxLine else { return .failure(ControlError("request too large")) }
        guard let object = try? JSONSerialization.jsonObject(with: line), let args = object as? [String: Any] else {
            return .failure(ControlError("a request is one JSON object per line"))
        }
        let id = int(args["id"]) ?? 0
        guard let name = args["op"] as? String else { return .failure(ControlError("missing \"op\"")) }
        do {
            return .success(ControlRequest(id: id, op: try op(name, args)))
        } catch let error as ControlError {
            return .failure(error)
        } catch {
            return .failure(ControlError("bad request"))
        }
    }

    /// The id of a request that failed to parse, so the error reply can still be matched to it (0 when unknown).
    static func id(of line: Data) -> Int {
        guard let object = try? JSONSerialization.jsonObject(with: line), let args = object as? [String: Any] else { return 0 }
        return int(args["id"]) ?? 0
    }

    static func op(_ name: String, _ args: [String: Any]) throws -> ControlOp {
        switch name {
        case "ping": return .ping
        case "status": return .status
        case "screenshot":
            var width: Int?
            if args["max_width"] != nil {
                guard let w = int(args["max_width"]), (16...8192).contains(w) else { throw ControlError("\"max_width\" must be between 16 and 8192") }
                width = w
            }
            return .screenshot(maxWidth: width)
        case "mouse_move":
            return .mouseMove(x: try coordinate(args, "x"), y: try coordinate(args, "y"))
        case "mouse_click":
            let x = try optionalCoordinate(args, "x"), y = try optionalCoordinate(args, "y")
            guard (x == nil) == (y == nil) else { throw ControlError("give both \"x\" and \"y\", or neither (click where the pointer is)") }
            var count = 1
            if args["count"] != nil {
                guard let c = int(args["count"]), (1...3).contains(c) else { throw ControlError("\"count\" must be 1, 2 or 3") }
                count = c
            }
            return .mouseClick(x: x, y: y, button: try button(args), count: count)
        case "mouse_down": return .mouseDown(button: try button(args))
        case "mouse_up": return .mouseUp(button: try button(args))
        case "mouse_drag":
            return .mouseDrag(fromX: try coordinate(args, "from_x"), fromY: try coordinate(args, "from_y"),
                              toX: try coordinate(args, "to_x"), toY: try coordinate(args, "to_y"), button: try button(args))
        case "type_text":
            guard let text = args["text"] as? String, !text.isEmpty else { throw ControlError("\"text\" must be a non-empty string") }
            guard text.count <= maxText else { throw ControlError("\"text\" is longer than \(maxText) characters") }
            for c in text where GuestKeys.stroke(for: c) == nil {
                throw ControlError("cannot type \(String(reflecting: String(c))): only US-keyboard characters, line breaks and tabs")
            }
            return .typeText(text)
        case "press_key":
            guard let key = args["key"] as? String, GuestKeys.code(forKey: key) != nil else {
                throw ControlError("\"key\" must be a character or a key name such as return, tab, escape, delete, left, f5")
            }
            var modifiers: [String] = []
            if let raw = args["modifiers"] {
                guard let list = raw as? [String], list.count <= 4, list.allSatisfy({ GuestKeys.modifierCode($0) != nil }) else {
                    throw ControlError("\"modifiers\" must be a list of command, option, control, shift")
                }
                modifiers = list.map { $0.lowercased() }
            }
            return .pressKey(key: key, modifiers: modifiers)
        case "shutdown":
            if let raw = args["force"], !(raw is Bool) { throw ControlError("\"force\" must be true or false") }
            return .shutdown(force: args["force"] as? Bool ?? false)
        case "display":
            guard let raw = args["mode"] else { return .display(mode: nil) }
            guard let name = raw as? String, let mode = DisplayMode(rawValue: name) else {
                throw ControlError("\"mode\" must be \"embedded\" (shown in the library window) or \"window\" (its own window)")
            }
            return .display(mode: mode)
        case "features":
            for key in ["edge_release", "clipboard"] where args[key] != nil && !(args[key] is Bool) {
                throw ControlError("\"\(key)\" must be true or false")
            }
            return .features(edgeRelease: args["edge_release"] as? Bool, clipboard: args["clipboard"] as? Bool)
        default:
            throw ControlError("unknown op \"\(name)\"")
        }
    }

    // MARK: responses

    static func success(id: Int, result: [String: Any] = [:]) -> Data {
        encode(["id": id, "ok": true, "result": result])
    }

    static func failure(id: Int, _ message: String) -> Data {
        encode(["id": id, "ok": false, "error": message])
    }

    private static func encode(_ object: [String: Any]) -> Data {
        var data = (try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])) ?? Data("{\"ok\":false,\"error\":\"encoding\"}".utf8)
        data.append(0x0a)
        return data
    }

    /// Splits whatever has arrived on a socket into complete lines, keeping a partial last line in `buffer`.
    static func takeLines(from buffer: inout Data) -> [Data] {
        var lines: [Data] = []
        while let newline = buffer.firstIndex(of: 0x0a) {
            let line = buffer[buffer.startIndex..<newline]
            if !line.isEmpty { lines.append(Data(line)) }
            buffer.removeSubrange(buffer.startIndex...newline)
        }
        return lines
    }

    /// Where a VM's control socket is: short, because a Unix socket path is limited to 104 bytes. One per VM id.
    static func socketPath(vmID: String, uid: uid_t = getuid()) -> String {
        let safe = String(vmID.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) || $0 == "-" }.prefix(12))
        return "/tmp/sheepshaver-\(uid)/\(safe.isEmpty ? "vm" : safe).sock"
    }
}
