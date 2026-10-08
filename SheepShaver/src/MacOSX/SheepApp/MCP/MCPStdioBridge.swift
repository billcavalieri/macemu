/*
 *  MCPStdioBridge.swift - `SheepShaver --mcp-stdio`: lets clients that start MCP servers as programs and talk to them
 *  over stdin and stdout (Claude Desktop and others) use the app's MCP server. It reads one JSON-RPC message per line on
 *  stdin, posts it to http://127.0.0.1:<port>/mcp with the bearer token from the app's settings, and writes the reply as
 *  one line on stdout. The token is read from the Keychain here, so the client's configuration holds no secret.
 *
 *  Called from main() in main_unix.cpp before anything else starts: no window, no emulator, no Dock icon.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation

@_cdecl("SheepMCPStdioMain")
public func SheepMCPStdioMain() -> Int32 {
    let output = FileHandle.standardOutput
    func write(_ data: Data) {
        output.write(data)
        output.write(Data("\n".utf8))
    }
    func error(id: Any?, _ message: String) -> Data {
        let object: [String: Any] = ["jsonrpc": "2.0", "id": id ?? NSNull(), "error": ["code": -32000, "message": message]]
        return (try? JSONSerialization.data(withJSONObject: object)) ?? Data()
    }
    guard let token = MCPSettings.existingToken else {
        FileHandle.standardError.write(Data("SheepShaver: no MCP token yet. Open SheepShaver, then Settings, and turn the MCP server on.\n".utf8))
        // keep answering, so the client shows a reason instead of a dead server
        while let line = readLine() {
            if let id = idOf(line) { write(error(id: id, "SheepShaver's MCP server has not been set up: open SheepShaver > Settings and turn it on")) }
        }
        return 1
    }
    let url = URL(string: "http://127.0.0.1:\(MCPSettings.port)\(MCPHTTP.endpoint)")!
    let session = URLSession(configuration: .ephemeral)
    while let line = readLine(strippingNewline: true) {
        let trimmed = line.trimmingCharacters(in: .whitespaces)
        if trimmed.isEmpty { continue }
        var request = URLRequest(url: url, timeoutInterval: 900)
        request.httpMethod = "POST"
        request.httpBody = Data(trimmed.utf8)
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        let done = DispatchSemaphore(value: 0)
        nonisolated(unsafe) var reply: Data?
        nonisolated(unsafe) var failure: String?
        session.dataTask(with: request) { data, response, err in
            if let err {
                failure = "SheepShaver's MCP server is not reachable (is SheepShaver open, with the MCP server turned on in Settings?): \(err.localizedDescription)"
            } else if let http = response as? HTTPURLResponse {
                if http.statusCode == 200 { reply = data }
                else if http.statusCode == 202 { reply = nil }
                else if http.statusCode == 401 { failure = "SheepShaver's MCP token changed: restart this client" }
                else { failure = "SheepShaver's MCP server answered \(http.statusCode)" }
            }
            done.signal()
        }.resume()
        done.wait()
        if let failure {
            if let id = idOf(trimmed) { write(error(id: id, failure)) }
        } else if let reply, !reply.isEmpty {
            // one reply per line: JSON has no raw newlines in what the server sends, but be sure
            write(Data(String(decoding: reply, as: UTF8.self).replacingOccurrences(of: "\n", with: " ").utf8))
        }
    }
    return 0
}

/// The id of a JSON-RPC request line, or nil for a notification or something unreadable.
private func idOf(_ line: String) -> Any? {
    guard let object = try? JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
          object["method"] != nil, let id = object["id"], !(id is NSNull) else { return nil }
    return id
}
