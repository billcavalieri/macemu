/*
 *  MCPProtocol.swift - The Model Context Protocol side of the manager: JSON-RPC 2.0 requests in, tool results out.
 *
 *  Pure Foundation: no sockets, no AppKit, no emulator. The VMs and what happens to them are behind `MCPBackend`, so
 *  tools/vms/tests/unit.sh tests every tool, the allow-list and the argument checks with a fake backend.
 *
 *  Tools: list_vms, start_vm, shutdown_vm, screenshot, mouse_move, mouse_click, mouse_down, mouse_up, mouse_drag,
 *  type_text, press_key. Every tool but list_vms takes `vm` (a name or id); a VM that is not on the allow-list is
 *  reported as unknown, so a client cannot even tell it exists. Arguments are checked with ControlProtocol before
 *  anything is sent to a VM.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

struct MCPVM: Equatable {
    var id: String
    var name: String
    var running: Bool
}

protocol MCPBackend: AnyObject, Sendable {
    /// The VMs the user allowed, with whether each one runs.
    func allowedVMs() -> [MCPVM]
    func start(vmID: String, waitForReady: Bool, timeoutSeconds: Int) throws -> [String: Any]
    /// Sends one control operation (a ControlProtocol op name and its arguments) to a running VM.
    func control(vmID: String, op: String, args: [String: Any]) throws -> [String: Any]
}

enum MCPProtocol {
    static let supportedVersions = ["2025-06-18", "2025-03-26", "2024-11-05"]
    static let serverName = "sheepshaver"
    static let maxBatch = 16

    // MARK: tools

    private static func schema(_ properties: [String: Any], required: [String] = []) -> [String: Any] {
        var s: [String: Any] = ["type": "object", "properties": properties, "additionalProperties": false]
        if !required.isEmpty { s["required"] = required }
        return s
    }

    private static var vmProperty: [String: Any] { ["type": "string", "description": "The virtual machine: its name or id, as listed by list_vms."] }
    private static var buttonProperty: [String: Any] { ["type": "string", "enum": ["left", "right"], "description": "left (default) or right; a right click is a Control-click, the classic Mac OS way."] }
    private static func coord(_ what: String) -> [String: Any] { ["type": "integer", "minimum": 0, "description": what] }

    static var tools: [[String: Any]] {
        let guest = " Coordinates are guest screen pixels, origin at the top left; screenshot and list_vms tell you the size."
        return [
            ["name": "list_vms", "description": "List the virtual machines this server may control, with whether each is running.",
             "inputSchema": schema([:])],
            ["name": "start_vm", "description": "Start a virtual machine in its own window (without taking the keyboard focus). Returns when the VM process is up; with wait_for_ready it waits until the Sheep Shears tool in the guest is running, which means Mac OS has finished starting.",
             "inputSchema": schema(["vm": vmProperty,
                                    "wait_for_ready": ["type": "boolean", "description": "Wait for Mac OS to finish starting (needs the Sheep Shears tool installed in the guest). Default false."],
                                    "timeout_seconds": ["type": "integer", "minimum": 1, "maximum": 600, "description": "How long wait_for_ready waits. Default 120."]], required: ["vm"])],
            ["name": "shutdown_vm", "description": "Shut a running VM down. By default this is a clean Mac OS shutdown (applications are asked to quit and save), which needs the Sheep Shears tool in the guest. With force=true the VM is stopped at once, like pulling the plug, and unsaved work in the guest is lost.",
             "inputSchema": schema(["vm": vmProperty, "force": ["type": "boolean", "description": "Stop immediately without a clean shutdown. Default false."]], required: ["vm"])],
            ["name": "screenshot", "description": "Take a screenshot of the guest screen as a PNG. Works while the VM's window is hidden or covered.",
             "inputSchema": schema(["vm": vmProperty, "max_width": ["type": "integer", "minimum": 16, "maximum": 8192, "description": "Scale the picture down to at most this many pixels wide (smaller is faster to send). Default: full size."]], required: ["vm"])],
            ["name": "mouse_move", "description": "Move the guest pointer to a position." + guest,
             "inputSchema": schema(["vm": vmProperty, "x": coord("Pixels from the left."), "y": coord("Pixels from the top.")], required: ["vm", "x", "y"])],
            ["name": "mouse_click", "description": "Click, or double or triple click, at a position (or where the pointer is, when x and y are omitted)." + guest,
             "inputSchema": schema(["vm": vmProperty, "x": coord("Pixels from the left."), "y": coord("Pixels from the top."), "button": buttonProperty,
                                    "count": ["type": "integer", "minimum": 1, "maximum": 3, "description": "1 (default), 2 for a double click, 3 for a triple click."]], required: ["vm"])],
            ["name": "mouse_down", "description": "Press and hold the mouse button where the pointer is. Needed for classic pull-down menus (hold on the menu title, move to the item, then mouse_up). Always follow with mouse_up.",
             "inputSchema": schema(["vm": vmProperty, "button": buttonProperty], required: ["vm"])],
            ["name": "mouse_up", "description": "Release the mouse button.",
             "inputSchema": schema(["vm": vmProperty, "button": buttonProperty], required: ["vm"])],
            ["name": "mouse_drag", "description": "Press at one position, move to another, and release: drags windows, icons and selections." + guest,
             "inputSchema": schema(["vm": vmProperty, "from_x": coord("Start, pixels from the left."), "from_y": coord("Start, pixels from the top."),
                                    "to_x": coord("End, pixels from the left."), "to_y": coord("End, pixels from the top."), "button": buttonProperty],
                                   required: ["vm", "from_x", "from_y", "to_x", "to_y"])],
            ["name": "type_text", "description": "Type text on the guest keyboard (a US keyboard: ASCII letters, digits, punctuation, line breaks and tabs). It goes to whatever has the focus in the guest.",
             "inputSchema": schema(["vm": vmProperty, "text": ["type": "string", "description": "The text to type. A line break presses Return."]], required: ["vm", "text"])],
            ["name": "press_key", "description": "Press one key, optionally with modifiers (for menu shortcuts such as Command-Q).",
             "inputSchema": schema(["vm": vmProperty,
                                    "key": ["type": "string", "description": "A character, or a key name: return, enter, tab, space, delete, forwarddelete, escape, left, right, up, down, home, end, pageup, pagedown, f1 to f12."],
                                    "modifiers": ["type": "array", "items": ["type": "string", "enum": ["command", "option", "control", "shift"]], "description": "Modifier keys to hold while pressing it."]],
                                   required: ["vm", "key"])],
        ]
    }

    // MARK: JSON-RPC

    private static func rpcError(_ id: Any?, _ code: Int, _ message: String) -> [String: Any] {
        ["jsonrpc": "2.0", "id": id ?? NSNull(), "error": ["code": code, "message": message]]
    }

    private static func rpcResult(_ id: Any, _ result: [String: Any]) -> [String: Any] {
        ["jsonrpc": "2.0", "id": id, "result": result]
    }

    private static func isValidID(_ id: Any?) -> Bool {
        guard let id else { return false }
        if id is String { return true }
        if let n = id as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID() { return true }
        return false
    }

    /// The reply body for one HTTP POST, or nil when none is due (the body held only notifications and responses).
    static func handle(_ body: Data, backend: MCPBackend) -> Data? {
        guard let object = try? JSONSerialization.jsonObject(with: body) else {
            return encode(rpcError(nil, -32700, "Parse error"))
        }
        if let batch = object as? [Any] {
            guard !batch.isEmpty, batch.count <= maxBatch else { return encode(rpcError(nil, -32600, "Invalid batch")) }
            let replies = batch.compactMap { message($0, backend: backend) }
            return replies.isEmpty ? nil : encode(replies)
        }
        return message(object, backend: backend).map { encode($0) }
    }

    private static func encode(_ object: Any) -> Data {
        (try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys, .withoutEscapingSlashes])) ?? Data()
    }

    private static func message(_ object: Any, backend: MCPBackend) -> [String: Any]? {
        guard let request = object as? [String: Any], request["jsonrpc"] as? String == "2.0" else {
            return rpcError(nil, -32600, "Invalid Request")
        }
        guard let method = request["method"] as? String else {
            return nil                      // a response to something we never asked: ignore
        }
        let id = request["id"]
        if id == nil || id is NSNull {      // a notification: no reply
            return nil
        }
        guard isValidID(id) else { return rpcError(nil, -32600, "Invalid Request") }
        let params = request["params"] as? [String: Any] ?? [:]
        switch method {
        case "initialize":
            let wanted = params["protocolVersion"] as? String ?? ""
            let version = supportedVersions.contains(wanted) ? wanted : supportedVersions[0]
            return rpcResult(id!, ["protocolVersion": version, "capabilities": ["tools": ["listChanged": false]],
                                   "serverInfo": ["name": serverName, "version": "1"],
                                   "instructions": "Controls classic Mac OS virtual machines. Call list_vms first. Take a screenshot to see the screen before clicking; coordinates are guest pixels."])
        case "ping":
            return rpcResult(id!, [:])
        case "tools/list":
            return rpcResult(id!, ["tools": tools])
        case "tools/call":
            guard let name = params["name"] as? String, tools.contains(where: { $0["name"] as? String == name }) else {
                return rpcError(id, -32602, "Unknown tool")
            }
            let arguments = params["arguments"] as? [String: Any] ?? [:]
            return rpcResult(id!, call(name, arguments, backend: backend))
        default:
            return rpcError(id, -32601, "Method not found")
        }
    }

    // MARK: tool calls

    private static func text(_ s: String, isError: Bool = false) -> [String: Any] {
        ["content": [["type": "text", "text": s]], "isError": isError]
    }

    private static func json(_ object: Any) -> String {
        let data = (try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes])) ?? Data()
        return String(decoding: data, as: UTF8.self)
    }

    /// A VM by id, or by name (case-insensitively) when exactly one has it. Only VMs on the allow-list are visible.
    static func resolve(_ reference: String, in vms: [MCPVM]) -> Result<MCPVM, ControlError> {
        if let byID = vms.first(where: { $0.id == reference }) { return .success(byID) }
        let named = vms.filter { $0.name.caseInsensitiveCompare(reference) == .orderedSame }
        if named.count == 1 { return .success(named[0]) }
        if named.count > 1 { return .failure(ControlError("more than one VM is named \"\(reference)\"; use its id: " + named.map { $0.id }.joined(separator: ", "))) }
        let known = vms.map { "\"\($0.name)\"" }.joined(separator: ", ")
        return .failure(ControlError("no such virtual machine \"\(reference)\"" + (known.isEmpty ? " (no VMs are allowed: enable some in SheepShaver's Settings)" : "; available: " + known)))
    }

    static func call(_ name: String, _ arguments: [String: Any], backend: MCPBackend) -> [String: Any] {
        let vms = backend.allowedVMs()
        if name == "list_vms" {
            let list = vms.map { ["id": $0.id, "name": $0.name, "running": $0.running] as [String: Any] }
            return text(json(["virtual_machines": list]))
        }
        guard let reference = arguments["vm"] as? String, !reference.isEmpty else {
            return text("\"vm\" is required: the name or id of a virtual machine (see list_vms)", isError: true)
        }
        let vm: MCPVM
        switch resolve(reference, in: vms) {
        case .success(let found): vm = found
        case .failure(let error): return text(error.message, isError: true)
        }
        var args = arguments
        args["vm"] = nil
        do {
            switch name {
            case "start_vm":
                var wait = false
                if let raw = args["wait_for_ready"] { guard let b = raw as? Bool else { throw ControlError("\"wait_for_ready\" must be true or false") }; wait = b }
                var timeout = 120
                if args["timeout_seconds"] != nil {
                    guard let t = ControlProtocol.int(args["timeout_seconds"]), (1...600).contains(t) else { throw ControlError("\"timeout_seconds\" must be between 1 and 600") }
                    timeout = t
                }
                return text(json(try backend.start(vmID: vm.id, waitForReady: wait, timeoutSeconds: timeout)))
            default:
                guard vm.running else { throw ControlError("\"\(vm.name)\" is not running; start it with start_vm") }
                // Check the arguments here, so a bad request never reaches the VM
                let op = name == "shutdown_vm" ? "shutdown" : name
                _ = try ControlProtocol.op(op, args)
                let result = try backend.control(vmID: vm.id, op: op, args: args)
                if name == "screenshot", let png = result["png_base64"] as? String {
                    let w = result["width"] ?? 0, h = result["height"] ?? 0, gw = result["guest_width"] ?? 0, gh = result["guest_height"] ?? 0
                    return ["content": [["type": "image", "data": png, "mimeType": "image/png"],
                                        ["type": "text", "text": "Screenshot \(w)x\(h) pixels of a \(gw)x\(gh) guest screen. Click coordinates are guest pixels."]],
                            "isError": false]
                }
                return text(json(result))
            }
        } catch let error as ControlError {
            return text(error.message, isError: true)
        } catch {
            return text("failed: \(error.localizedDescription)", isError: true)
        }
    }
}
