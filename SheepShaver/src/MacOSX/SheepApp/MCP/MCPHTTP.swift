/*
 *  MCPHTTP.swift - The small slice of HTTP/1.1 the MCP endpoint needs, and the rules that decide who may talk to it.
 *
 *  Pure Foundation: compiled on its own by tools/vms/tests/unit.sh. The server listens on the loopback address only;
 *  on top of that a request must carry the bearer token, name the loopback host (so a web page cannot reach the
 *  server through DNS rebinding) and must not come from a browser page (no Origin header).
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

struct HTTPRequest: Equatable {
    var method: String
    var path: String
    var headers: [String: String]      // names lower-cased
    var body: Data
}

struct HTTPResponse: Equatable {
    var status: Int
    var headers: [String: String] = [:]
    var body: Data = Data()

    static let reasons = [200: "OK", 202: "Accepted", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden", 404: "Not Found",
                          405: "Method Not Allowed", 413: "Payload Too Large", 431: "Request Header Fields Too Large", 500: "Internal Server Error"]

    func serialized() -> Data {
        var head = "HTTP/1.1 \(status) \(Self.reasons[status] ?? "Status")\r\n"
        var all = headers
        all["Content-Length"] = "\(body.count)"
        all["Connection"] = "close"
        all["Cache-Control"] = "no-store"
        for key in all.keys.sorted() { head += "\(key): \(all[key]!)\r\n" }
        head += "\r\n"
        return Data(head.utf8) + body
    }

    static func json(_ body: Data) -> HTTPResponse {
        HTTPResponse(status: 200, headers: ["Content-Type": "application/json"], body: body)
    }

    static func error(_ status: Int, _ message: String, headers: [String: String] = [:]) -> HTTPResponse {
        var h = headers
        h["Content-Type"] = "text/plain; charset=utf-8"
        return HTTPResponse(status: status, headers: h, body: Data(message.utf8))
    }
}

enum HTTPParseResult: Equatable {
    case needMore
    case request(HTTPRequest)
    case failure(HTTPResponse)
}

enum MCPHTTP {
    static let maxHeaderBytes = 16 * 1024
    static let maxBodyBytes = 1 << 20
    static let endpoint = "/mcp"

    /// Parses one request from the bytes received so far. A request is complete when its Content-Length bytes of body have
    /// arrived; chunked bodies are not supported (the request is refused).
    static func parse(_ data: Data) -> HTTPParseResult {
        let separator = Data("\r\n\r\n".utf8)
        guard let end = data.range(of: separator) else {
            return data.count > maxHeaderBytes ? .failure(.error(431, "headers too large")) : .needMore
        }
        guard end.lowerBound <= maxHeaderBytes else { return .failure(.error(431, "headers too large")) }
        guard let head = String(data: data[data.startIndex..<end.lowerBound], encoding: .utf8) else {
            return .failure(.error(400, "bad request"))
        }
        let lines = head.components(separatedBy: "\r\n")
        let start = lines[0].split(separator: " ", omittingEmptySubsequences: true)
        guard start.count == 3, start[2].hasPrefix("HTTP/1.") else { return .failure(.error(400, "bad request line")) }
        var headers: [String: String] = [:]
        for line in lines.dropFirst() {
            guard let colon = line.firstIndex(of: ":") else { return .failure(.error(400, "bad header")) }
            let name = line[line.startIndex..<colon].trimmingCharacters(in: .whitespaces).lowercased()
            let value = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
            if headers[name] != nil && ["content-length", "host", "authorization"].contains(name) {
                return .failure(.error(400, "duplicate header"))
            }
            headers[name] = value
        }
        if headers["transfer-encoding"] != nil { return .failure(.error(400, "chunked bodies are not supported")) }
        var length = 0
        if let raw = headers["content-length"] {
            guard let n = Int(raw), n >= 0 else { return .failure(.error(400, "bad Content-Length")) }
            guard n <= maxBodyBytes else { return .failure(.error(413, "body too large")) }
            length = n
        }
        let bodyStart = end.upperBound
        guard data.count - bodyStart >= length else { return .needMore }
        return .request(HTTPRequest(method: String(start[0]), path: String(start[1]), headers: headers,
                                    body: Data(data[bodyStart..<(bodyStart + length)])))
    }

    /// Compares without stopping at the first difference, so timing does not reveal how much of a guess was right.
    static func constantTimeEquals(_ a: String, _ b: String) -> Bool {
        let x = Array(a.utf8), y = Array(b.utf8)
        var diff = x.count ^ y.count
        for i in 0..<max(x.count, y.count) {
            diff |= Int(i < x.count ? x[i] : 0) ^ Int(i < y.count ? y[i] : 0)
        }
        return diff == 0
    }

    /// nil when the request may proceed; otherwise the response that refuses it.
    static func authorize(_ request: HTTPRequest, token: String, port: UInt16) -> HTTPResponse? {
        let path = request.path.split(separator: "?", maxSplits: 1).first.map(String.init) ?? request.path
        guard path == endpoint else { return .error(404, "not found") }
        // DNS rebinding: a page on another site that resolves its name to 127.0.0.1 still sends its own name as Host
        let allowedHosts: Set<String> = ["127.0.0.1:\(port)", "localhost:\(port)", "[::1]:\(port)"]
        guard let host = request.headers["host"]?.lowercased(), allowedHosts.contains(host) else { return .error(403, "forbidden host") }
        // Browsers always send Origin on cross-site requests; MCP clients that are programs do not
        guard request.headers["origin"] == nil else { return .error(403, "browser requests are not accepted") }
        guard !token.isEmpty, let auth = request.headers["authorization"], auth.lowercased().hasPrefix("bearer "),
              constantTimeEquals(String(auth.dropFirst(7)).trimmingCharacters(in: .whitespaces), token) else {
            return .error(401, "missing or wrong bearer token", headers: ["WWW-Authenticate": "Bearer realm=\"sheepshaver\""])
        }
        guard request.method == "POST" else { return .error(405, "POST only", headers: ["Allow": "POST"]) }
        return nil
    }

    /// The whole answer to a request: refusals, the JSON-RPC reply, or 202 when the body held only notifications.
    static func respond(to request: HTTPRequest, token: String, port: UInt16, backend: MCPBackend) -> HTTPResponse {
        if let refusal = authorize(request, token: token, port: port) { return refusal }
        guard let reply = MCPProtocol.handle(request.body, backend: backend) else { return HTTPResponse(status: 202) }
        return .json(reply)
    }
}
