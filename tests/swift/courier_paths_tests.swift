import Foundation

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

let dxvkBackedUpDlls: Set<String> = ["d3dx9_31.dll", "d3dx9_42.dll"]

@main
struct CourierPathTests
{
    static let hash = "0123456789abcdef0123456789abcdef"

    static func entry(_ path: String, size: Int = 1) -> ManifestEntry
    {
        ManifestEntry(path: path, hash: hash, size: size)
    }

    static func expectThrows(_ name: String, _ body: () throws -> Void)
    {
        do {
            try body()
            fatalError("Expected rejection: \(name)")
        } catch {
        }
    }

    static func main() throws
    {
        let valid = try validateManifest(Manifest(files: [entry("data/game.bin")]))
        guard valid.count == 1 && valid[0].relativePath == "data/game.bin" else {
            fatalError("Valid manifest was not normalized correctly")
        }

        let sha256 = String(repeating: "ab", count: 32)
        let shaValid = try validateManifest(Manifest(files: [
            ManifestEntry(path: "data/sha.bin", hash: sha256, size: 1),
        ]))
        guard shaValid.first?.manifest.hash == sha256 else {
            fatalError("Valid SHA-256 manifest hash was rejected")
        }

        for path in [
            "../escape.bin",
            "/absolute.bin",
            "C:/drive.bin",
            "data//file.bin",
            "data/./file.bin",
            "data/../file.bin",
            "CON.txt",
            "data/NUL",
            "data/trailing. ",
            "data/bad:name",
            ".soa-update-journal.json",
            "version.json",
        ] {
            expectThrows(path) {
                _ = try validateManifest(Manifest(files: [entry(path)]))
            }
        }

        expectThrows("case collision") {
            _ = try validateManifest(Manifest(files: [entry("Data/File.bin"), entry("data/file.bin")]))
        }

        expectThrows("width collision") {
            _ = try validateManifest(Manifest(files: [entry("data/Ａ.bin"), entry("data/A.bin")]))
        }

        expectThrows("file directory collision") {
            _ = try validateManifest(Manifest(files: [entry("data"), entry("data/file.bin")]))
        }

        expectThrows("negative size") {
            _ = try validateManifest(Manifest(files: [entry("data/file.bin", size: -1)]))
        }

        expectThrows("invalid hash") {
            _ = try validateManifest(Manifest(files: [
                ManifestEntry(path: "data/file.bin", hash: "invalid", size: 1),
            ]))
        }

        let temporary = FileManager.default.temporaryDirectory
            .appendingPathComponent("soa-courier-path-tests-\(UUID().uuidString)", isDirectory: true)
        let root = temporary.appendingPathComponent("root", isDirectory: true)
        let outside = temporary.appendingPathComponent("outside", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: outside, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporary) }

        try FileManager.default.createSymbolicLink(
            atPath: root.appendingPathComponent("linked").path,
            withDestinationPath: outside.path)
        expectThrows("symlink escape") {
            _ = try safeDestination(root: root, relativePath: "linked/file.bin")
        }
    }
}
