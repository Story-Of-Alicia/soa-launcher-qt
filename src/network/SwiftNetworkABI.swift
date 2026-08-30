import Foundation
#if canImport(CryptoKit)
import CryptoKit
#endif
import Soa_Courier

#if !os(Linux)
private let soaSealKeyIDDomain = Data("SOA-SEAL-KEY-ID-V1\0".utf8)
private let soaSealPrefix = Data("SOA-SEAL-V1\0".utf8)

private func soaSealKeyID(_ publicKey: Data) -> String
{
    #if canImport(CryptoKit)
    var input = soaSealKeyIDDomain
    input.append(publicKey)
    let digest = SHA256.hash(data: input)
    let bytes = Array(digest.prefix(16))
    let hex = bytes.map { String(format: "%02X", $0) }.joined()
    let groups = stride(from: 0, to: hex.count, by: 4).map { offset -> String in
        let start = hex.index(hex.startIndex, offsetBy: offset)
        let end = hex.index(start, offsetBy: 4)
        return String(hex[start..<end])
    }
    return "SOA1-" + groups.joined(separator: "-")
    #else
    return ""
    #endif
}

@_cdecl("soa_verify_soa_seal_v1")
public func soaVerifySOASealV1(_ publicKey: UnsafePointer<UInt8>?,
                               _ publicKeySize: UInt64,
                               _ documentKind: UInt8,
                               _ message: UnsafePointer<UInt8>?,
                               _ messageSize: UInt64,
                               _ keyID: UnsafePointer<CChar>?,
                               _ keyIDSize: UInt64,
                               _ signature: UnsafePointer<UInt8>?,
                               _ signatureSize: UInt64) -> Bool
{
    #if canImport(CryptoKit)
    guard let publicKey, publicKeySize == 32,
          let keyID, keyIDSize == 44,
          let signature, signatureSize == 64,
          messageSize == 0 || message != nil,
          publicKeySize <= UInt64(Int.max),
          messageSize <= UInt64(Int.max),
          keyIDSize <= UInt64(Int.max),
          signatureSize <= UInt64(Int.max) else {
        return false
    }

    let kind: String
    switch documentKind {
    case 1: kind = "manifest"
    case 2: kind = "history"
    default: return false
    }

    do {
        let publicKeyData = Data(bytes: publicKey, count: Int(publicKeySize))
        let keyIDData = Data(bytes: keyID, count: Int(keyIDSize))
        guard let suppliedKeyID = String(data: keyIDData, encoding: .utf8) else { return false }
        let expectedKeyID = soaSealKeyID(publicKeyData)
        guard suppliedKeyID == expectedKeyID else { return false }

        var payload = soaSealPrefix
        payload.append(Data("kind=\(kind)\0".utf8))
        payload.append(Data("key=\(expectedKeyID)\0".utf8))
        if let message, messageSize != 0 {
            payload.append(Data(bytes: message, count: Int(messageSize)))
        }

        let signatureData = Data(bytes: signature, count: Int(signatureSize))
        let key = try Curve25519.Signing.PublicKey(rawRepresentation: publicKeyData)
        return key.isValidSignature(signatureData, for: payload)
    } catch {
        return false
    }
    #else
    return false
    #endif
}
#endif

@_cdecl("soa_http_client_create")
public func soa_http_client_create(_ httpDone: soa_http_done_cb?,
                                   _ dnsDone: soa_dns_done_cb?,
                                   _ context: UnsafeMutableRawPointer?) -> UnsafeMutableRawPointer?
{
    guard let httpDone, let dnsDone else { return nil }
    return Unmanaged.passRetained(SwiftHttpClient(
        httpDone: httpDone,
        dnsDone: dnsDone,
        context: context)).toOpaque()
}

@_cdecl("soa_http_client_shutdown")
public func soa_http_client_shutdown(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeUnretainedValue().shutdown()
}

@_cdecl("soa_http_client_destroy")
public func soa_http_client_destroy(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    let client = Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeRetainedValue()
    client.shutdown()
}

@_cdecl("soa_http_client_request")
public func soa_http_client_request(_ pointer: UnsafeMutableRawPointer?,
                                    _ url: UnsafePointer<CChar>?,
                                    _ method: soa_http_method,
                                    _ timeoutMilliseconds: UInt32,
                                    _ maximumBytes: UInt64,
                                    _ accept: UnsafePointer<CChar>?,
                                    _ userAgent: UnsafePointer<CChar>?,
                                    _ allowInsecureHTTP: Bool) -> UInt64
{
    guard let pointer, let url else { return 0 }
    let client = Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeUnretainedValue()
    return client.request(
        urlText: String(cString: url),
        method: method,
        timeoutMilliseconds: timeoutMilliseconds,
        maximumBytes: maximumBytes,
        accept: cStringValue(accept),
        userAgent: cStringValue(userAgent),
        allowInsecureHTTP: allowInsecureHTTP)
}

@_cdecl("soa_http_client_resolve")
public func soa_http_client_resolve(_ pointer: UnsafeMutableRawPointer?,
                                    _ hostname: UnsafePointer<CChar>?,
                                    _ timeoutMilliseconds: UInt32) -> UInt64
{
    guard let pointer, let hostname else { return 0 }
    let client = Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeUnretainedValue()
    return client.resolve(
        hostname: String(cString: hostname),
        timeoutMilliseconds: timeoutMilliseconds)
}

@_cdecl("soa_http_client_cancel")
public func soa_http_client_cancel(_ pointer: UnsafeMutableRawPointer?, _ requestID: UInt64)
{
    guard let pointer else { return }
    Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeUnretainedValue().cancel(requestID)
}

@_cdecl("soa_http_client_cancel_all")
public func soa_http_client_cancel_all(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<SwiftHttpClient>.fromOpaque(pointer).takeUnretainedValue().cancelAll()
}

@_cdecl("soa_launcher_updater_create")
public func soa_launcher_updater_create(
    _ manifestURL: UnsafePointer<CChar>?,
    _ fallbackManifestURL: UnsafePointer<CChar>?,
    _ publicKeyHex: UnsafePointer<CChar>?,
    _ currentVersion: UnsafePointer<CChar>?,
    _ platform: UnsafePointer<CChar>?,
    _ downloadDirectory: UnsafePointer<CChar>?,
    _ userAgent: UnsafePointer<CChar>?,
    _ allowInsecureHTTP: Bool,
    _ checkDone: soa_launcher_check_cb?,
    _ progress: soa_launcher_progress_cb?,
    _ downloadDone: soa_launcher_download_cb?,
    _ context: UnsafeMutableRawPointer?) -> UnsafeMutableRawPointer?
{
    guard let manifestURL,
          let fallbackManifestURL,
          let publicKeyHex,
          let currentVersion,
          let platform,
          let downloadDirectory,
          let checkDone,
          let progress,
          let downloadDone else { return nil }
    let updater = LauncherUpdateService(
        manifestURL: String(cString: manifestURL),
        fallbackManifestURL: String(cString: fallbackManifestURL),
        publicKeyHex: String(cString: publicKeyHex),
        currentVersion: String(cString: currentVersion),
        platform: String(cString: platform),
        downloadDirectory: String(cString: downloadDirectory),
        userAgent: cStringValue(userAgent),
        allowInsecureHTTP: allowInsecureHTTP,
        checkDone: checkDone,
        progress: progress,
        downloadDone: downloadDone,
        context: context)
    return Unmanaged.passRetained(updater).toOpaque()
}

@_cdecl("soa_launcher_updater_shutdown")
public func soa_launcher_updater_shutdown(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeUnretainedValue().shutdown()
}

@_cdecl("soa_launcher_updater_destroy")
public func soa_launcher_updater_destroy(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    let updater = Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeRetainedValue()
    updater.shutdown()
}

@_cdecl("soa_launcher_updater_check")
public func soa_launcher_updater_check(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeUnretainedValue().check()
}

@_cdecl("soa_launcher_updater_download")
public func soa_launcher_updater_download(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeUnretainedValue().download()
}

@_cdecl("soa_launcher_updater_cancel")
public func soa_launcher_updater_cancel(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeUnretainedValue().cancel()
}

@_cdecl("soa_launcher_updater_select_version")
public func soa_launcher_updater_select_version(_ pointer: UnsafeMutableRawPointer?,
                                                _ version: UnsafePointer<CChar>?) -> Bool
{
    guard let pointer, let version else { return false }
    return Unmanaged<LauncherUpdateService>.fromOpaque(pointer).takeUnretainedValue()
        .selectVersion(String(cString: version))
}

@_cdecl("soa_discord_rpc_create")
public func soa_discord_rpc_create(_ applicationID: UnsafePointer<CChar>?,
                                   _ processID: Int64,
                                   _ proxyBaseURL: UnsafePointer<CChar>?) -> UnsafeMutableRawPointer?
{
    guard let applicationID else { return nil }
    let proxy = cStringValue(proxyBaseURL)
    let service = DiscordRpcService(
        applicationID: String(cString: applicationID),
        processID: processID,
        proxyBaseURL: proxy.isEmpty ? "http://127.0.0.1:18080" : proxy)
    return Unmanaged.passRetained(service).toOpaque()
}

@_cdecl("soa_discord_rpc_set_launcher_presence")
public func soa_discord_rpc_set_launcher_presence(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<DiscordRpcService>.fromOpaque(pointer).takeUnretainedValue().setLauncherPresence()
}

@_cdecl("soa_discord_rpc_set_game_presence")
public func soa_discord_rpc_set_game_presence(_ pointer: UnsafeMutableRawPointer?,
                                              _ username: UnsafePointer<CChar>?)
{
    guard let pointer else { return }
    Unmanaged<DiscordRpcService>.fromOpaque(pointer).takeUnretainedValue()
        .setGamePresence(username: cStringValue(username))
}

@_cdecl("soa_discord_rpc_shutdown")
public func soa_discord_rpc_shutdown(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    Unmanaged<DiscordRpcService>.fromOpaque(pointer).takeUnretainedValue().shutdown()
}

@_cdecl("soa_discord_rpc_destroy")
public func soa_discord_rpc_destroy(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    let service = Unmanaged<DiscordRpcService>.fromOpaque(pointer).takeRetainedValue()
    service.shutdown()
}
