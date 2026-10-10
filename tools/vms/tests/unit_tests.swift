// Unit tests for the control channel's pure code: GuestPixels, GuestInput, VMControlProtocol. Built by unit.sh.
import Foundation
import ImageIO

var failures = 0
var checks = 0
func check(_ ok: Bool, _ what: String, line: Int = #line) {
    checks += 1
    if !ok { failures += 1; print("FAIL (line \(line)): \(what)") }
}

// MARK: pixels

func palette(_ colors: [(UInt8, UInt8, UInt8)]) -> [UInt8] {
    var p = [UInt8](repeating: 0, count: 256 * 3)
    for (i, c) in colors.enumerated() { p[i * 3] = c.0; p[i * 3 + 1] = c.1; p[i * 3 + 2] = c.2 }
    return p
}

do {
    // 32-bit: pad, R, G, B
    let f = GuestFrame(width: 2, height: 1, rowBytes: 8, depth: 32, pixels: [0, 10, 20, 30, 0, 200, 100, 50], palette: [])
    check(f.rgba() == [10, 20, 30, 255, 200, 100, 50, 255], "32-bit xRGB")
    // 16-bit 5-5-5 big-endian: 0x7C00 = red, 0x03E0 = green, 0x001F = blue, 0x7FFF = white
    let g = GuestFrame(width: 4, height: 1, rowBytes: 8, depth: 16, pixels: [0x7C, 0x00, 0x03, 0xE0, 0x00, 0x1F, 0x7F, 0xFF], palette: [])
    check(g.rgba() == [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255], "16-bit 5-5-5 expands to full range")
    // 8-bit indexed
    let pal8 = palette([(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)])
    let h = GuestFrame(width: 3, height: 1, rowBytes: 3, depth: 8, pixels: [1, 2, 3], palette: pal8)
    check(h.rgba() == [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255], "8-bit indexed")
    // 4-bit: high nibble is the first pixel
    let i4 = GuestFrame(width: 3, height: 1, rowBytes: 2, depth: 4, pixels: [0x12, 0x30], palette: pal8)
    check(i4.rgba() == [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255], "4-bit: high nibble first, odd width")
    // 2-bit
    let i2 = GuestFrame(width: 4, height: 1, rowBytes: 1, depth: 2, pixels: [0b00_01_10_11], palette: pal8)
    check(i2.rgba() == [0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255], "2-bit: first pixel in the top bits")
    // 1-bit: set bit = index 1
    let i1 = GuestFrame(width: 9, height: 1, rowBytes: 2, depth: 1, pixels: [0b1010_0000, 0b1000_0000], palette: pal8)
    let r1 = i1.rgba()!
    check(r1[0] == 255 && r1[4] == 0 && r1[8] == 255 && r1[12] == 0 && r1[32] == 255, "1-bit: MSB first, second row byte")
    // row padding is skipped
    let padded = GuestFrame(width: 1, height: 2, rowBytes: 8, depth: 32, pixels: [0, 1, 2, 3, 9, 9, 9, 9, 0, 4, 5, 6, 9, 9, 9, 9], palette: [])
    check(padded.rgba() == [1, 2, 3, 255, 4, 5, 6, 255], "rowBytes larger than the row is honoured")
    // invalid frames never read out of bounds
    check(GuestFrame(width: 2, height: 2, rowBytes: 8, depth: 32, pixels: [UInt8](repeating: 0, count: 15), palette: []).rgba() == nil, "short pixel data rejected")
    check(GuestFrame(width: 2, height: 1, rowBytes: 4, depth: 32, pixels: [UInt8](repeating: 0, count: 4), palette: []).rgba() == nil, "rowBytes too small rejected")
    check(GuestFrame(width: 2, height: 1, rowBytes: 2, depth: 8, pixels: [0, 0], palette: [0]).rgba() == nil, "indexed frame without a full palette rejected")
    check(GuestFrame(width: 2, height: 1, rowBytes: 2, depth: 24, pixels: [0, 0], palette: []).rgba() == nil, "unknown depth rejected")
    check(GuestFrame(width: 0, height: 1, rowBytes: 0, depth: 32, pixels: [], palette: []).rgba() == nil, "empty frame rejected")
    // scaling
    let wide = GuestFrame(width: 4, height: 2, rowBytes: 16, depth: 32,
                          pixels: (0..<8).flatMap { [UInt8(0), UInt8($0 * 10), 0, 0] }, palette: [])
    let s = wide.scaledRGBA(maxWidth: 2)!
    check(s.width == 2 && s.height == 1 && s.rgba.count == 8, "scaled to the requested width, aspect kept")
    check(wide.scaledRGBA(maxWidth: 100)!.width == 4, "maxWidth larger than the picture changes nothing")
    check(wide.scaledRGBA(maxWidth: nil)!.width == 4, "no maxWidth changes nothing")
    // PNG
    let png = wide.pngData()!
    check(png.prefix(8) == Data([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]), "PNG signature")
    let source = CGImageSourceCreateWithData(png as CFData, nil)!
    let image = CGImageSourceCreateImageAtIndex(source, 0, nil)!
    check(image.width == 4 && image.height == 2, "PNG decodes to the same size")
}

// MARK: keys

do {
    check(GuestKeys.stroke(for: "a") == GuestKeys.Stroke(code: 0x00, shift: false), "a")
    check(GuestKeys.stroke(for: "A") == GuestKeys.Stroke(code: 0x00, shift: true), "A is shift+a")
    check(GuestKeys.stroke(for: "1") == GuestKeys.Stroke(code: 0x12, shift: false), "1")
    check(GuestKeys.stroke(for: "!") == GuestKeys.Stroke(code: 0x12, shift: true), "! is shift+1")
    check(GuestKeys.stroke(for: " ") == GuestKeys.Stroke(code: 0x31, shift: false), "space")
    check(GuestKeys.stroke(for: "\n") == GuestKeys.Stroke(code: 0x24, shift: false), "line break is return")
    check(GuestKeys.stroke(for: "\t") == GuestKeys.Stroke(code: 0x30, shift: false), "tab")
    check(GuestKeys.stroke(for: "é") == nil, "non-ASCII cannot be typed")
    check(GuestKeys.stroke(for: "\u{07}") == nil, "control characters cannot be typed")
    // every printable ASCII character is typeable and distinct strokes give distinct characters
    var seen = Set<[Int]>()
    var allTypeable = true
    for b in 0x20...0x7e {
        guard let s = GuestKeys.stroke(for: Character(UnicodeScalar(UInt8(b)))) else { allTypeable = false; print("  not typeable: \(b)"); continue }
        if !seen.insert([s.code, s.shift ? 1 : 0]).inserted { allTypeable = false; print("  duplicate stroke for \(b)") }
    }
    check(allTypeable, "all 95 printable ASCII characters have a distinct stroke")
    check(GuestKeys.code(forKey: "return") == 0x24 && GuestKeys.code(forKey: "LEFT") == 0x3b && GuestKeys.code(forKey: "f5") == 0x60, "named keys")
    check(GuestKeys.code(forKey: "up") == 0x3e && GuestKeys.code(forKey: "down") == 0x3d && GuestKeys.code(forKey: "right") == 0x3c, "arrow keys use ADB codes")
    check(GuestKeys.code(forKey: "q") == 0x0c && GuestKeys.code(forKey: "Q") == 0x0c, "a letter, either case")
    check(GuestKeys.code(forKey: "nonsense") == nil, "unknown key name")
    check(GuestKeys.modifierCode("command") == 0x37 && GuestKeys.modifierCode("Shift") == 0x38 && GuestKeys.modifierCode("option") == 0x3a && GuestKeys.modifierCode("ctrl") == 0x36 && GuestKeys.modifierCode("control") == 0x36 && GuestKeys.modifierCode("cmd") == 0x37 && GuestKeys.modifierCode("alt") == 0x3a, "modifiers (control is the ADB control key, not left arrow)")
    check(GuestKeys.modifierCode("hyper") == nil, "unknown modifier")
}

// MARK: pointer planner

do {
    check(MouseStepPlanner.next(from: (10, 10), to: (10, 10)) == nil, "already there")
    check(MouseStepPlanner.next(from: (10, 10), to: (11, 9)) == nil, "within tolerance")
    let far = MouseStepPlanner.next(from: (0, 0), to: (500, 300))!
    check(far.dx == MouseStepPlanner.farStep && far.dy == MouseStepPlanner.farStep, "far: capped steps")
    let near = MouseStepPlanner.next(from: (100, 100), to: (105, 98))!
    check(abs(near.dx) == 1 && abs(near.dy) == 1, "near: single pixels")
    check(MouseStepPlanner.next(from: (100, 100), to: (100, 90))!.dx == 0, "an axis that is there is not moved")
    let back = MouseStepPlanner.next(from: (300, 300), to: (0, 0))!
    check(back.dx < 0 && back.dy < 0, "direction follows the sign of the error")
    // simulate a guest that accelerates larger reports (x1.5 above 8) and never overshoots by more than a step
    func simulate(from start: (Int, Int), to target: (Int, Int)) -> (steps: Int, end: (Int, Int)) {
        var p = start
        var steps = 0
        while let m = MouseStepPlanner.next(from: p, to: target), steps < 2000 {
            func apply(_ d: Int) -> Int { abs(d) > 8 ? d * 3 / 2 : d }
            p = (p.0 + apply(m.dx), p.1 + apply(m.dy))
            steps += 1
        }
        return (steps, p)
    }
    for (a, b) in [((0, 0), (1023, 767)), ((1023, 767), (0, 0)), ((500, 400), (503, 401)), ((10, 700), (900, 5)), ((300, 300), (300, 310))] {
        let r = simulate(from: a, to: b)
        check(r.steps < 200 && abs(r.end.0 - b.0) <= 1 && abs(r.end.1 - b.1) <= 1, "converges from \(a) to \(b) in \(r.steps) steps")
    }
}

// MARK: protocol

func parse(_ s: String) -> Result<ControlRequest, ControlError> { ControlProtocol.parse(Data(s.utf8)) }
func op(_ s: String) -> ControlOp? { if case .success(let r) = parse(s) { return r.op }; return nil }
func error(_ s: String) -> String? { if case .failure(let e) = parse(s) { return e.message }; return nil }

do {
    check(op(#"{"id":3,"op":"ping"}"#) == .ping, "ping")
    check(parse(#"{"id":3,"op":"status"}"#) == .success(ControlRequest(id: 3, op: .status)), "id is carried")
    check(op(#"{"op":"screenshot"}"#) == .screenshot(maxWidth: nil), "screenshot without options")
    check(op(#"{"op":"screenshot","max_width":800}"#) == .screenshot(maxWidth: 800), "screenshot max_width")
    check(error(#"{"op":"screenshot","max_width":5}"#) != nil && error(#"{"op":"screenshot","max_width":"big"}"#) != nil, "bad max_width")
    check(op(#"{"op":"mouse_move","x":10,"y":20}"#) == .mouseMove(x: 10, y: 20), "mouse_move")
    check(op(#"{"op":"mouse_move","x":10.0,"y":20.0}"#) == .mouseMove(x: 10, y: 20), "whole floats are accepted as integers")
    check(error(#"{"op":"mouse_move","x":10.5,"y":20}"#) != nil, "fractions rejected")
    check(error(#"{"op":"mouse_move","x":-1,"y":20}"#) != nil && error(#"{"op":"mouse_move","x":99999,"y":0}"#) != nil, "out-of-range coordinates rejected")
    check(error(#"{"op":"mouse_move","x":true,"y":1}"#) != nil && error(#"{"op":"mouse_move","x":"1","y":1}"#) != nil, "booleans and strings are not numbers")
    check(error(#"{"op":"mouse_move","x":1}"#) != nil, "missing y")
    check(op(#"{"op":"mouse_click","x":1,"y":2}"#) == .mouseClick(x: 1, y: 2, button: .left, count: 1), "click defaults")
    check(op(#"{"op":"mouse_click","x":1,"y":2,"button":"RIGHT","count":2}"#) == .mouseClick(x: 1, y: 2, button: .right, count: 2), "right double click")
    check(op(#"{"op":"mouse_click"}"#) == .mouseClick(x: nil, y: nil, button: .left, count: 1), "click in place")
    check(error(#"{"op":"mouse_click","x":1}"#) != nil, "x without y")
    check(error(#"{"op":"mouse_click","x":1,"y":2,"count":9}"#) != nil && error(#"{"op":"mouse_click","x":1,"y":2,"button":"middle"}"#) != nil, "bad count and button")
    check(op(#"{"op":"mouse_down"}"#) == .mouseDown(button: .left) && op(#"{"op":"mouse_up","button":"right"}"#) == .mouseUp(button: .right), "down and up")
    check(op(#"{"op":"mouse_drag","from_x":1,"from_y":2,"to_x":3,"to_y":4}"#) == .mouseDrag(fromX: 1, fromY: 2, toX: 3, toY: 4, button: .left), "drag")
    check(error(#"{"op":"mouse_drag","from_x":1,"from_y":2,"to_x":3}"#) != nil, "drag needs all four")
    check(op(#"{"op":"type_text","text":"Hello, World!\n"}"#) == .typeText("Hello, World!\n"), "type_text")
    check(error(#"{"op":"type_text","text":""}"#) != nil && error(#"{"op":"type_text"}"#) != nil, "empty or missing text")
    check(error(#"{"op":"type_text","text":"café"}"#)?.contains("cannot type") == true, "untypeable character named in the error")
    check(error("{\"op\":\"type_text\",\"text\":\"\(String(repeating: "a", count: 4001))\"}") != nil, "text length limit")
    check(op(#"{"op":"press_key","key":"return"}"#) == .pressKey(key: "return", modifiers: []), "press_key")
    check(op(#"{"op":"press_key","key":"q","modifiers":["Command"]}"#) == .pressKey(key: "q", modifiers: ["command"]), "modifiers are normalised")
    check(error(#"{"op":"press_key","key":"zzz"}"#) != nil && error(#"{"op":"press_key","key":"a","modifiers":["hyper"]}"#) != nil && error(#"{"op":"press_key","key":"a","modifiers":"command"}"#) != nil, "bad key and modifiers")
    check(op(#"{"op":"shutdown"}"#) == .shutdown(force: false) && op(#"{"op":"shutdown","force":true}"#) == .shutdown(force: true), "shutdown")
    check(error(#"{"op":"shutdown","force":"yes"}"#) != nil, "force must be a boolean")
    check(op(#"{"op":"features"}"#) == .features(edgeRelease: nil, clipboard: nil), "features with no arguments reads the switches")
    check(op(#"{"op":"features","edge_release":false,"clipboard":true}"#) == .features(edgeRelease: false, clipboard: true), "features sets the switches it is given")
    check(op(#"{"op":"display"}"#) == .display(mode: nil) && op(#"{"op":"display","mode":"window"}"#) == .display(mode: .window) && op(#"{"op":"display","mode":"embedded"}"#) == .display(mode: .embedded), "display reads or sets where the picture is")
    check(error(#"{"op":"display","mode":"fullscreen"}"#) != nil && error(#"{"op":"display","mode":3}"#) != nil, "display modes are checked")
    check(error(#"{"op":"features","clipboard":"on"}"#) != nil && error(#"{"op":"features","edge_release":"false"}"#) != nil, "feature switches must be booleans")
    check(error(#"{"op":"format_disk"}"#)?.contains("unknown op") == true, "unknown op")
    check(error(#"{"id":1}"#) != nil && error("not json") != nil && error("[1,2]") != nil, "malformed requests")
    check(ControlProtocol.id(of: Data(#"{"id":7,"op":"zzz"}"#.utf8)) == 7 && ControlProtocol.id(of: Data("junk".utf8)) == 0, "id recovered for an error reply")
    check(ControlProtocol.parse(Data(count: ControlProtocol.maxLine + 1)) == .failure(ControlError("request too large")), "request size limit")

    let ok = String(data: ControlProtocol.success(id: 4, result: ["a": 1]), encoding: .utf8)!
    check(ok == "{\"id\":4,\"ok\":true,\"result\":{\"a\":1}}\n", "success reply is one line")
    let bad = String(data: ControlProtocol.failure(id: 4, "no \"quotes\" \n break"), encoding: .utf8)!
    check(bad.hasSuffix("\n") && bad.dropLast().firstIndex(of: "\n") == nil, "failure reply stays one line however the message looks")

    var buffer = Data("{\"op\":\"ping\"}\n{\"op\":\"sta".utf8)
    let lines = ControlProtocol.takeLines(from: &buffer)
    check(lines.count == 1 && String(data: buffer, encoding: .utf8) == "{\"op\":\"sta", "a partial line stays in the buffer")
    buffer.append(Data("tus\"}\n\n".utf8))
    check(ControlProtocol.takeLines(from: &buffer).count == 1 && buffer.isEmpty, "the line completes, blank lines are dropped")

    let path = ControlProtocol.socketPath(vmID: "0F8A2C1E-AAAA-BBBB-CCCC-123456789ABC", uid: 501)
    check(path == "/tmp/sheepshaver-501/0F8A2C1E-AAA.sock" && path.utf8.count < 104, "socket path is short and stable")
    check(ControlProtocol.socketPath(vmID: "../../etc", uid: 501) == "/tmp/sheepshaver-501/etc.sock", "no path tricks in an id")
    check(ControlProtocol.socketPath(vmID: "", uid: 501) == "/tmp/sheepshaver-501/vm.sock", "empty id")
}

// MARK: HTTP

do {
    func request(_ lines: [String], body: String = "") -> Data { Data((lines.joined(separator: "\r\n") + "\r\n\r\n" + body).utf8) }
    let good = request(["POST /mcp HTTP/1.1", "Host: 127.0.0.1:8765", "Authorization: Bearer tok", "Content-Length: 2"], body: "{}")
    if case .request(let r) = MCPHTTP.parse(good) {
        check(r.method == "POST" && r.path == "/mcp" && r.headers["host"] == "127.0.0.1:8765" && r.body == Data("{}".utf8), "a complete request parses")
    } else { check(false, "a complete request parses") }
    check(MCPHTTP.parse(Data("POST /mcp HTTP/1.1\r\nHost: x".utf8)) == .needMore, "partial headers need more")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Content-Length: 10"], body: "{}")) == .needMore, "partial body needs more")
    check(MCPHTTP.parse(Data(repeating: 65, count: MCPHTTP.maxHeaderBytes + 10)) == .failure(.error(431, "headers too large")), "endless headers refused")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Content-Length: 99999999"])) == .failure(.error(413, "body too large")), "huge body refused up front")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Transfer-Encoding: chunked"])) == .failure(.error(400, "chunked bodies are not supported")), "chunked refused")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Content-Length: 1", "Content-Length: 2"])) == .failure(.error(400, "duplicate header")), "duplicate Content-Length refused (request smuggling)")
    check(MCPHTTP.parse(request(["GARBAGE"])) == .failure(.error(400, "bad request line")), "bad request line")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "NoColonHere"])) == .failure(.error(400, "bad header")), "bad header line")
    check(MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Content-Length: -3"])) == .failure(.error(400, "bad Content-Length")), "negative length")
    if case .request(let r) = MCPHTTP.parse(request(["post /mcp?x=1 HTTP/1.1", "HOST: a", "Content-Length: 0"])) {
        check(r.headers["host"] == "a" && r.path == "/mcp?x=1", "header names are case-insensitive, query kept")
    } else { check(false, "case-insensitive headers") }
    // pipelined bytes after the body are not part of the request
    if case .request(let r) = MCPHTTP.parse(request(["POST /mcp HTTP/1.1", "Content-Length: 2"], body: "{}EXTRA")) { check(r.body == Data("{}".utf8), "the body is exactly Content-Length bytes") }

    func req(path: String = "/mcp", method: String = "POST", host: String? = "127.0.0.1:8765", auth: String? = "Bearer tok", origin: String? = nil) -> HTTPRequest {
        var h: [String: String] = [:]
        if let host { h["host"] = host }
        if let auth { h["authorization"] = auth }
        if let origin { h["origin"] = origin }
        return HTTPRequest(method: method, path: path, headers: h, body: Data())
    }
    check(MCPHTTP.authorize(req(), token: "tok", port: 8765) == nil, "a good request is allowed")
    check(MCPHTTP.authorize(req(path: "/mcp?a=b"), token: "tok", port: 8765) == nil, "a query string is ignored")
    check(MCPHTTP.authorize(req(host: "localhost:8765"), token: "tok", port: 8765) == nil && MCPHTTP.authorize(req(host: "[::1]:8765"), token: "tok", port: 8765) == nil, "localhost and ::1 hosts")
    check(MCPHTTP.authorize(req(auth: "bearer tok"), token: "tok", port: 8765) == nil, "the scheme name is case-insensitive")
    check(MCPHTTP.authorize(req(path: "/other"), token: "tok", port: 8765)?.status == 404, "other paths are 404")
    check(MCPHTTP.authorize(req(host: "evil.example:8765"), token: "tok", port: 8765)?.status == 403, "another host name is refused (DNS rebinding)")
    check(MCPHTTP.authorize(req(host: "127.0.0.1:9999"), token: "tok", port: 8765)?.status == 403, "another port in Host is refused")
    check(MCPHTTP.authorize(req(host: nil), token: "tok", port: 8765)?.status == 403, "no Host header is refused")
    check(MCPHTTP.authorize(req(origin: "https://evil.example"), token: "tok", port: 8765)?.status == 403, "a browser Origin is refused")
    check(MCPHTTP.authorize(req(auth: nil), token: "tok", port: 8765)?.status == 401, "no token")
    check(MCPHTTP.authorize(req(auth: "Bearer wrong"), token: "tok", port: 8765)?.status == 401, "wrong token")
    check(MCPHTTP.authorize(req(auth: "Basic dG9r"), token: "tok", port: 8765)?.status == 401, "other schemes")
    check(MCPHTTP.authorize(req(auth: "Bearer "), token: "", port: 8765)?.status == 401, "an empty token never matches an empty setting")
    check(MCPHTTP.authorize(req(auth: "Bearer toktok"), token: "tok", port: 8765)?.status == 401 && MCPHTTP.authorize(req(auth: "Bearer to"), token: "tok", port: 8765)?.status == 401, "prefixes and extensions of the token")
    check(MCPHTTP.authorize(req(method: "GET"), token: "tok", port: 8765)?.status == 405, "GET is not served")
    check(MCPHTTP.authorize(req(method: "GET", auth: nil), token: "tok", port: 8765)?.status == 401, "auth is checked before the method")
    check(MCPHTTP.constantTimeEquals("abc", "abc") && !MCPHTTP.constantTimeEquals("abc", "abd") && !MCPHTTP.constantTimeEquals("abc", "ab") && MCPHTTP.constantTimeEquals("", ""), "constant-time compare")
    let wire = String(decoding: HTTPResponse.json(Data("{}".utf8)).serialized(), as: UTF8.self)
    check(wire.hasPrefix("HTTP/1.1 200 OK\r\n") && wire.contains("Content-Length: 2\r\n") && wire.contains("Connection: close") && wire.hasSuffix("\r\n\r\n{}"), "response serialisation")
}

// MARK: MCP

final class FakeBackend: MCPBackend, @unchecked Sendable {
    var vms = [MCPVM(id: "id-a", name: "OS 9 Test", running: true), MCPVM(id: "id-b", name: "Stopped One", running: false)]
    var calls: [(String, String, [String: Any])] = []
    var starts: [(String, Bool, Int)] = []
    var failure: ControlError?
    func allowedVMs() -> [MCPVM] { vms }
    func start(vmID: String, waitForReady: Bool, timeoutSeconds: Int) throws -> [String: Any] {
        starts.append((vmID, waitForReady, timeoutSeconds)); return ["started": true]
    }
    func control(vmID: String, op: String, args: [String: Any]) throws -> [String: Any] {
        calls.append((vmID, op, args))
        if let failure { throw failure }
        if op == "screenshot" { return ["png_base64": "UE5H", "width": 10, "height": 5, "guest_width": 1024, "guest_height": 768] }
        return ["cursor": ["x": 1, "y": 2]]
    }
}

func rpc(_ backend: MCPBackend, _ json: String) -> [String: Any]? {
    guard let data = MCPProtocol.handle(Data(json.utf8), backend: backend) else { return nil }
    return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
}
func toolResult(_ backend: MCPBackend, _ name: String, _ args: String) -> (text: String, isError: Bool, content: [[String: Any]]) {
    let r = rpc(backend, #"{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"\#(name)","arguments":\#(args)}}"#)!
    let result = r["result"] as! [String: Any]
    let content = result["content"] as! [[String: Any]]
    return (content.compactMap { $0["text"] as? String }.joined(separator: "\n"), result["isError"] as? Bool ?? false, content)
}

do {
    let b = FakeBackend()
    let initialize = rpc(b, #"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}"#)!
    let ir = initialize["result"] as! [String: Any]
    check(ir["protocolVersion"] as? String == "2025-03-26" && (ir["capabilities"] as? [String: Any])?["tools"] != nil, "initialize echoes a supported protocol version and offers tools")
    check((rpc(b, #"{"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"1999-01-01"}}"#)!["result"] as! [String: Any])["protocolVersion"] as? String == "2025-06-18", "an unknown version gets the newest supported one")
    check(rpc(b, #"{"jsonrpc":"2.0","method":"notifications/initialized"}"#) == nil, "a notification gets no reply")
    check(rpc(b, #"{"jsonrpc":"2.0","id":3,"method":"ping"}"#)?["result"] != nil, "ping")
    check((rpc(b, #"{"jsonrpc":"2.0","id":4,"method":"nope"}"#)?["error"] as? [String: Any])?["code"] as? Int == -32601, "unknown method")
    check((rpc(b, "{not json")?["error"] as? [String: Any])?["code"] as? Int == -32700, "parse error")
    check((rpc(b, #"{"id":1,"method":"ping"}"#)?["error"] as? [String: Any])?["code"] as? Int == -32600, "missing jsonrpc version")
    check(rpc(b, #"{"jsonrpc":"2.0","id":7,"result":{}}"#) == nil, "a response from the client is ignored")
    let list = (rpc(b, #"{"jsonrpc":"2.0","id":5,"method":"tools/list"}"#)!["result"] as! [String: Any])["tools"] as! [[String: Any]]
    let names = Set(list.compactMap { $0["name"] as? String })
    check(names == ["list_vms", "start_vm", "shutdown_vm", "screenshot", "mouse_move", "mouse_click", "mouse_down", "mouse_up", "mouse_drag", "type_text", "press_key"], "all eleven tools are listed")
    check(list.allSatisfy { ($0["inputSchema"] as? [String: Any])?["type"] as? String == "object" && ($0["description"] as? String)?.isEmpty == false }, "every tool has a description and an object schema")
    check(list.filter { $0["name"] as? String != "list_vms" }.allSatisfy { (($0["inputSchema"] as! [String: Any])["required"] as? [String])?.contains("vm") == true }, "every tool but list_vms requires vm")
    // batch
    let batch = MCPProtocol.handle(Data(#"[{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"},{"jsonrpc":"2.0","id":2,"method":"ping"}]"#.utf8), backend: b)
    check(((try? JSONSerialization.jsonObject(with: batch ?? Data())) as? [[String: Any]])?.count == 2, "a batch answers its requests, not its notifications")
    check(MCPProtocol.handle(Data("[]".utf8), backend: b) != nil, "an empty batch is an error reply")
    check(rpc(b, #"{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"format_disk","arguments":{}}}"#)?["error"] != nil, "an unknown tool is a protocol error")

    // list_vms
    let listed = toolResult(b, "list_vms", "{}")
    check(!listed.isError && listed.text.contains("OS 9 Test") && listed.text.contains("id-b"), "list_vms shows allowed VMs")
    // vm resolution
    check(toolResult(b, "screenshot", "{}").isError, "vm is required")
    check(toolResult(b, "screenshot", #"{"vm":"nonexistent"}"#).text.contains("available: \"OS 9 Test\""), "an unknown VM lists the allowed ones")
    check(toolResult(b, "screenshot", #"{"vm":"os 9 TEST"}"#).isError == false && b.calls.last?.0 == "id-a", "names match case-insensitively")
    check(toolResult(b, "screenshot", #"{"vm":"id-a"}"#).isError == false, "ids match")
    check(b.calls.last?.2["vm"] == nil, "the backend is not given the vm argument")
    let empty = FakeBackend(); empty.vms = []
    check(toolResult(empty, "screenshot", #"{"vm":"anything"}"#).text.contains("no VMs are allowed"), "with nothing allowed the client is told how to enable some")
    let twins = FakeBackend(); twins.vms = [MCPVM(id: "1", name: "Same", running: true), MCPVM(id: "2", name: "same", running: true)]
    check(toolResult(twins, "screenshot", #"{"vm":"Same"}"#).text.contains("use its id"), "an ambiguous name asks for the id")
    check(toolResult(twins, "screenshot", #"{"vm":"2"}"#).isError == false, "...and the id works")
    // not running
    b.calls = []
    check(toolResult(b, "mouse_move", #"{"vm":"Stopped One","x":1,"y":1}"#).text.contains("not running") && b.calls.isEmpty, "controlling a stopped VM is refused without touching it")
    // argument checks happen before the backend
    check(toolResult(b, "mouse_move", #"{"vm":"id-a","x":-1,"y":1}"#).isError && b.calls.isEmpty, "bad coordinates never reach the VM")
    check(toolResult(b, "type_text", #"{"vm":"id-a","text":"café"}"#).isError && b.calls.isEmpty, "untypeable text never reaches the VM")
    check(toolResult(b, "press_key", #"{"vm":"id-a","key":"q","modifiers":["hyper"]}"#).isError && b.calls.isEmpty, "bad modifiers never reach the VM")
    // what reaches the backend
    _ = toolResult(b, "mouse_click", #"{"vm":"id-a","x":10,"y":20,"button":"right","count":2}"#)
    check(b.calls.last?.1 == "mouse_click" && (b.calls.last?.2["x"] as? Int) == 10 && (b.calls.last?.2["button"] as? String) == "right" && (b.calls.last?.2["count"] as? Int) == 2, "mouse_click arguments are passed on")
    _ = toolResult(b, "shutdown_vm", #"{"vm":"id-a","force":true}"#)
    check(b.calls.last?.1 == "shutdown" && (b.calls.last?.2["force"] as? Bool) == true, "shutdown_vm reaches the VM as the shutdown op, with force")
    // screenshot result
    let shot = toolResult(b, "screenshot", #"{"vm":"id-a"}"#)
    check(shot.content.first?["type"] as? String == "image" && shot.content.first?["mimeType"] as? String == "image/png" && shot.content.first?["data"] as? String == "UE5H", "a screenshot is an image content block")
    check(shot.text.contains("10x5") && shot.text.contains("1024x768"), "…with its size in words")
    // start_vm
    _ = toolResult(b, "start_vm", #"{"vm":"Stopped One","wait_for_ready":true,"timeout_seconds":30}"#)
    check(b.starts.last?.0 == "id-b" && b.starts.last?.1 == true && b.starts.last?.2 == 30, "start_vm passes its options")
    _ = toolResult(b, "start_vm", #"{"vm":"Stopped One"}"#)
    check(b.starts.last?.1 == false && b.starts.last?.2 == 120, "start_vm defaults")
    check(toolResult(b, "start_vm", #"{"vm":"id-b","timeout_seconds":0}"#).isError && toolResult(b, "start_vm", #"{"vm":"id-b","wait_for_ready":"yes"}"#).isError, "start_vm argument checks")
    let before = b.starts.count
    _ = toolResult(b, "start_vm", #"{"vm":"not allowed"}"#)
    check(b.starts.count == before, "a VM that is not allowed cannot be started")
    // backend errors
    b.failure = ControlError("the guest did not answer in time")
    let failed = toolResult(b, "screenshot", #"{"vm":"id-a"}"#)
    check(failed.isError && failed.text.contains("did not answer"), "a VM error becomes an isError tool result")
    // the allow-list is the only way in: a hidden VM is indistinguishable from a missing one
    let hidden = FakeBackend(); hidden.vms = [MCPVM(id: "id-a", name: "Visible", running: true)]
    check(toolResult(hidden, "screenshot", #"{"vm":"id-b"}"#).text == toolResult(hidden, "screenshot", #"{"vm":"never-existed"}"#).text.replacingOccurrences(of: "never-existed", with: "id-b"), "a VM that is not allowed looks like one that does not exist")
}

// ---- LibraryImport (adding an existing prefs file) ----
do {
    let dir = URL(fileURLWithPath: "/Users/me/Macs/os9", isDirectory: true)
    let text = "# comment\ndisk disk.img\ndisk /abs/other.img\ncdrom ../cd.iso\nrom \nextfs ~/share\nscreen win/1024/768\nramsize 536870912\n\nfloppy *\n"
    let out = LibraryImport.absolutized(text, relativeTo: dir).components(separatedBy: "\n")
    check(out.contains("disk /Users/me/Macs/os9/disk.img"), "a relative disk path becomes absolute")
    check(out.contains("disk /abs/other.img") && out.contains("extfs ~/share") && out.contains("floppy *"), "absolute, ~ and * values are left alone")
    check(out.contains("cdrom /Users/me/Macs/cd.iso"), "../ is resolved")
    check(out.contains("screen win/1024/768") && out.contains("ramsize 536870912") && out.contains("# comment"), "keys that are not paths are untouched")
    check(out.count == text.components(separatedBy: "\n").count, "no line is added or lost")
    check(LibraryImport.suggestedName(for: URL(fileURLWithPath: "/a/My Mac/prefs")) == "My Mac", "a file called prefs is named after its folder")
    check(LibraryImport.suggestedName(for: URL(fileURLWithPath: "/a/prefs-1024-qcow2")) == "prefs-1024-qcow2", "any other file is named after itself")
    check(LibraryImport.looksLikePrefs("disk /x\nrom /y\n") && !LibraryImport.looksLikePrefs("hello world\nfoo bar\n") && !LibraryImport.looksLikePrefs(""), "prefs detection")
}

// ---- PrefsDocument (editing a prefs file without losing anything) ----
do {
    let original = "# my Mac\ndisk /a/one.img\ndisk /a/two.qcow2\ncdrom /cd.iso\nrom \nscreen win/1024/768\nramsize 536870912\nfuture_key some value\njit true\n"
    var doc = PrefsDocument(text: original)
    check(doc.text == original, "a prefs file comes back byte for byte when nothing is changed")
    check(doc.values("disk") == ["/a/one.img", "/a/two.qcow2"], "repeated keys keep every value, in order")
    check(doc.string("screen") == "win/1024/768" && doc.int("ramsize") == 536870912 && doc.bool("jit"), "typed reads")
    check(doc.string("rom") == "" && doc.has("rom") && !doc.has("nogui") && doc.bool("nogui", true) && doc.string("zzz", "d") == "d", "empty values, missing keys and fallbacks")
    doc.set("ramsize", 268435456)
    check(doc.text == original.replacingOccurrences(of: "ramsize 536870912", with: "ramsize 268435456"), "changing one value changes only that line")
    doc.set("jit", false)
    check(doc.text.contains("jit false\n") && doc.text.contains("# my Mac\n") && doc.text.contains("future_key some value\n"), "comments and keys the app does not know are kept")
    doc.setValues("disk", ["/a/two.qcow2", "/a/three.img", "/a/four.img"])
    check(doc.values("disk") == ["/a/two.qcow2", "/a/three.img", "/a/four.img"], "a list is replaced as a whole")
    check(doc.text.hasPrefix("# my Mac\ndisk /a/two.qcow2\ndisk /a/three.img\ndisk /a/four.img\ncdrom"), "…at the place the first old line was")
    doc.setValues("disk", ["/only.img"])
    check(doc.values("disk") == ["/only.img"] && doc.text.components(separatedBy: "\n").filter { $0.hasPrefix("disk") }.count == 1, "a shorter list leaves no old lines behind")
    doc.set("nogui", true)
    check(doc.text.hasSuffix("jit false\nnogui true\n"), "a new key is added at the end")
    doc.remove("cdrom"); doc.remove("not_there")
    check(!doc.has("cdrom") && !doc.text.contains("cdrom"), "remove")
    doc.setValues("disk", [])
    check(!doc.has("disk"), "an empty list removes the key")
    let crlf = PrefsDocument(text: "disk /x\r\nrom /y\r\n")
    check(crlf.values("disk") == ["/x"] && crlf.string("rom") == "/y", "Windows line endings are read")
    check(PrefsDocument(text: "").text == "" && PrefsDocument(text: "\n").text == "\n", "empty files")
    var fresh = PrefsDocument()
    fresh.set("disk", "/d.img"); fresh.set("screen", "win/800/600")
    check(fresh.text == "disk /d.img\nscreen win/800/600\n", "a new file")
    check(PrefsDocument(text: "nogui\n").bool("nogui"), "a key with no value reads as true")
}

// ---- DisplayProtocol (input and events between the library window and a VM process) ----
do {
    func roundTrip(_ input: DisplayInput) -> DisplayInput? {
        var buffer = DisplayWire.encode(input); var out: DisplayInput?
        try? DisplayWire.take(&buffer) { kind, payload in out = DisplayWire.decodeInput(kind, payload) }
        return buffer.isEmpty ? out : nil
    }
    func roundTrip(_ event: DisplayEvent) -> DisplayEvent? {
        var buffer = DisplayWire.encode(event); var out: DisplayEvent?
        try? DisplayWire.take(&buffer) { kind, payload in out = DisplayWire.decodeEvent(kind, payload) }
        return buffer.isEmpty ? out : nil
    }
    let inputs: [DisplayInput] = [.key(code: 0x38, down: true), .key(code: 0, down: false), .mouseMove(dx: -3, dy: 7), .mouseMove(dx: 32767, dy: -32768),
                                  .mouseAbs(x: 1023, y: 767), .button(number: 1, down: true), .button(number: 0, down: false),
                                  .setRelativeMouse(true), .pointerWanted(false)]
    check(inputs.allSatisfy { roundTrip($0) == $0 }, "every input message survives encoding and decoding")
    let cursor = (0..<68).map { UInt8($0 * 3 & 255) }
    let events: [DisplayEvent] = [.guestMode(width: 1024, height: 768, depth: 32, generation: 70_000), .cursor(image: cursor), .cursorHidesHost(true),
                                  .arrow(x: -1, y: 300, visible: true), .shearsPointer(x: 5, y: -6, width: 1024, height: 768, buttons: 0x8000_0001),
                                  .shearsStatus(toolRunning: true, version: 1), .edgeThreshold(milli: 8_500), .features(host: 3, guest: 1), .displayMode(inOwnWindow: true), .displayMode(inOwnWindow: false), .problem(text: "Disk in use — “Mac OS”")]
    check(events.allSatisfy { roundTrip($0) == $0 }, "every event survives encoding and decoding (negative numbers, big generation, UTF-8)")
    check(DisplayWire.encode(DisplayInput.mouseMove(dx: 1, dy: 2)) == [5, 2, 1, 0, 2, 0], "a mouse move is 6 bytes on the wire (little-endian)")
    check(DisplayWire.encode(DisplayInput.key(code: 0x24, down: true)) == [3, 1, 0x24, 1], "a key is 4 bytes")
    check(inputs.allSatisfy { DisplayWire.encode($0).count <= 10 }, "input messages stay tiny")
    // framing
    var stream = DisplayWire.encode(DisplayInput.mouseMove(dx: 1, dy: 1)) + DisplayWire.encode(DisplayInput.button(number: 0, down: true))
    let whole = stream
    var seen = 0
    var partial = Array(whole.prefix(9))                  // one whole message and part of the next
    try? DisplayWire.take(&partial) { _, _ in seen += 1 }
    check(seen == 1 && partial.count == 3, "a partial message is kept until the rest arrives")
    partial += whole.suffix(from: 9)
    try? DisplayWire.take(&partial) { _, _ in seen += 1 }
    check(seen == 2 && partial.isEmpty, "…and then delivered")
    stream = [0, 1, 2]
    var threw = false
    do { try DisplayWire.take(&stream) { _, _ in } } catch { threw = true }
    check(threw, "a zero length byte is a protocol error")
    // hostile or damaged input is ignored, never trusted
    check(DisplayWire.decodeInput(1, [1, 2, 3]) == nil && DisplayWire.decodeInput(99, [1]) == nil && DisplayWire.decodeInput(4, [2, 1]) == nil, "wrong sizes, unknown kinds and a button other than 0/1 decode to nothing")
    check(DisplayWire.decodeInput(1, [0xff, 1]) == .key(code: 0x7f, down: true), "an ADB key code is limited to 7 bits")
    check(DisplayWire.decodeEvent(0x87, ArraySlice(Array(repeating: UInt8(65), count: 201))) == nil, "an over-long problem text is refused")
    let longText = String(repeating: "x", count: 500)
    if case .problem(let text)? = roundTrip(DisplayEvent.problem(text: longText)) { check(text.utf8.count == DisplayWire.maxProblemText, "a long problem text is cut to the limit") } else { check(false, "a long problem text round-trips") }
    check(DisplayWire.decodeEvent(0x82, ArraySlice([1, 2, 3])) == nil, "a cursor image of the wrong size is refused")
    // names
    check(DisplayPaths.sharedMemoryName(vmID: "ab/../cd ef", uid: 501) == "/sheep.501.ab..cdef".replacingOccurrences(of: "..", with: ""), "a shared memory name has no slashes, dots or spaces from the VM id")
    check(DisplayPaths.sharedMemoryName(vmID: "0123456789abcdefXYZ", uid: 501) == "/sheep.501.0123456789ab", "…and at most 12 characters of it")
    check(DisplayPaths.socketPath(vmID: "abc", uid: 501) == "/tmp/sheepshaver-501/abc.disp", "the display socket sits beside the control socket")
}

print("control tests: \(checks) checks, \(failures) failed")
exit(failures == 0 ? 0 : 1)
