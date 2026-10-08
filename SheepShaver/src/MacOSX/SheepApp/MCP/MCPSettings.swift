/*
 *  MCPSettings.swift - The MCP server's settings: on/off (default off), the port, which VMs it may use, and the
 *  bearer token. App-wide, kept in UserDefaults; the token is in the Keychain (UserDefaults only if the Keychain
 *  refuses). Read by the library window and by `SheepShaver --mcp-stdio`, which is the same app bundle.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import Security

enum MCPSettings {
    static let enabledKey = "SheepMCPEnabled"
    static let portKey = "SheepMCPPort"
    static let allowedKey = "SheepMCPAllowedVMs"
    private static let tokenFallbackKey = "SheepMCPTokenFallback"
    private static let keychainService = "com.billcavalieri.sheepshaver.mcp"
    static let defaultPort: UInt16 = 8765

    /// Test runs: NW_MCP_TEST=<port>:<token>:<id>,<id> turns the server on with these values and leaves the user's settings
    /// alone. Honoured only with diagnostics on (NW_VERBOSE=1).
    private static let testOverride: (port: UInt16, token: String, allowed: Set<String>)? = {
        guard nwDiagnosticsOn, let raw = ProcessInfo.processInfo.environment["NW_MCP_TEST"] else { return nil }
        let parts = raw.split(separator: ":", maxSplits: 2, omittingEmptySubsequences: false).map(String.init)
        guard parts.count == 3, let port = UInt16(parts[0]) else { return nil }
        return (port, parts[1], Set(parts[2].split(separator: ",").map(String.init)))
    }()

    static var enabled: Bool {
        get { testOverride != nil || UserDefaults.standard.bool(forKey: enabledKey) }
        set { UserDefaults.standard.set(newValue, forKey: enabledKey) }
    }

    static var port: UInt16 {
        get {
            if let testOverride { return testOverride.port }
            let v = UserDefaults.standard.integer(forKey: portKey)
            return (1024...65535).contains(v) ? UInt16(v) : defaultPort
        }
        set { UserDefaults.standard.set(Int(newValue), forKey: portKey) }
    }

    /// Ids of the VMs the server may list, start, stop, watch and control. Empty until the user picks some.
    static var allowedIDs: Set<String> {
        get { testOverride?.allowed ?? Set(UserDefaults.standard.stringArray(forKey: allowedKey) ?? []) }
        set { UserDefaults.standard.set(newValue.sorted(), forKey: allowedKey) }
    }

    // MARK: token

    private static func keychainQuery() -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: keychainService, kSecAttrAccount as String: "token"]
    }

    /// The stored token, or nil when there is none yet.
    static var existingToken: String? {
        if let testOverride { return testOverride.token }
        var query = keychainQuery()
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        if SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess, let data = item as? Data,
           let token = String(data: data, encoding: .utf8), !token.isEmpty {
            return token
        }
        return UserDefaults.standard.string(forKey: tokenFallbackKey).flatMap { $0.isEmpty ? nil : $0 }
    }

    private static func store(_ token: String) {
        var query = keychainQuery()
        SecItemDelete(query as CFDictionary)
        query[kSecValueData as String] = Data(token.utf8)
        query[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        if SecItemAdd(query as CFDictionary, nil) == errSecSuccess {
            UserDefaults.standard.removeObject(forKey: tokenFallbackKey)
        } else {
            UserDefaults.standard.set(token, forKey: tokenFallbackKey)
        }
    }

    static func makeToken() -> String {
        var bytes = [UInt8](repeating: 0, count: 32)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        precondition(status == errSecSuccess, "no random bytes")
        return Data(bytes).base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }

    /// The token, made the first time it is asked for.
    static var token: String {
        if let existing = existingToken { return existing }
        let fresh = makeToken()
        store(fresh)
        return fresh
    }

    @discardableResult
    static func regenerateToken() -> String {
        let fresh = makeToken()
        store(fresh)
        return fresh
    }
}
