import Foundation
import Soa_Courier

final class CourierLogger: @unchecked Sendable
{
    static let shared = CourierLogger()

    private let lock = NSLock()
    private var callback: courier_log_cb?
    private var context: UnsafeMutableRawPointer?

    func set(_ callback: courier_log_cb?, context: UnsafeMutableRawPointer?)
    {
        lock.lock()
        self.callback = callback
        self.context = context
        lock.unlock()
    }

    func write(_ level: Int32, _ message: String)
    {
        lock.lock()
        let currentCallback = callback
        let currentContext = context
        lock.unlock()

        guard let currentCallback else { return }
        message.withCString { currentCallback(level, $0, currentContext) }
    }
}

func log(_ level: Int32, _ message: String)
{
    CourierLogger.shared.write(level, message)
}

func logSafe(_ value: String) -> String
{
    value.unicodeScalars.map { scalar in
        CharacterSet.controlCharacters.contains(scalar) ? "?" : String(scalar)
    }.joined()
}

struct ManifestEntry: Codable
{
    let path: String
    let hash: String
    let size: Int
}

struct Manifest: Codable
{
    let files: [ManifestEntry]
}

struct ValidatedManifestEntry
{
    let manifest: ManifestEntry
    let relativePath: String
    let collisionKey: String
}

struct ManagedManifest: Codable
{
    let schemaVersion: Int
    let releaseVersion: String
    let files: [String]
}

struct StagingManifestEntry: Codable, Equatable
{
    let path: String
    let hash: String
    let size: Int
}

struct StagingManifest: Codable, Equatable
{
    let schemaVersion: Int
    let releaseVersion: String
    let files: [StagingManifestEntry]
}

struct UpdateJournal: Codable
{
    let schemaVersion: Int
    let replacementPaths: [String]
    let replacementHadOriginal: [Bool]
    let replacementStagedPaths: [String]?
    let obsoletePaths: [String]
    let versionMetadataExisted: Bool
    let managedMetadataExisted: Bool
}

let dxvkBackedUpDlls: Set<String> = ["d3dx9_31.dll", "d3dx9_42.dll"]

struct Err: Error
{
    let message: String
    let retryable: Bool

    init(_ message: String, retryable: Bool = false)
    {
        self.message = message
        self.retryable = retryable
    }
}
