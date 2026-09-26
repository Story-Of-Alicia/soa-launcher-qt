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

        let expectedFile = root.appendingPathComponent("data/game.bin")
        try FileManager.default.createDirectory(
            at: expectedFile.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data([1, 2, 3]).write(to: expectedFile)
        try Data().write(to: root.appendingPathComponent("version.json"))
        try Data().write(to: root.appendingPathComponent("alice.cfg"))
        let internalDirectory = root.appendingPathComponent(".soa-update-staging", isDirectory: true)
        try FileManager.default.createDirectory(at: internalDirectory, withIntermediateDirectories: true)
        try Data().write(to: internalDirectory.appendingPathComponent("temporary.bin"))
        try Data().write(to: root.appendingPathComponent("mod.dll"))
        let fakeInternal = root.appendingPathComponent(".soa-cheat", isDirectory: true)
        try FileManager.default.createDirectory(at: fakeInternal, withIntermediateDirectories: true)
        try Data().write(to: fakeInternal.appendingPathComponent("mod.dll"))

        let unexpected = try unexpectedInstallFiles(
            installRoot: root, expectedRelativePaths: ["data/game.bin"])
        guard unexpected.contains("mod.dll") && unexpected.contains("linked")
                && unexpected.contains(".soa-cheat/mod.dll")
                && !unexpected.contains("version.json")
                && !unexpected.contains("alice.cfg")
                && !unexpected.contains("data/game.bin")
                && !unexpected.contains(".soa-update-staging/temporary.bin") else {
            fatalError("Unexpected-file scan did not enforce the install allowlist: \(unexpected)")
        }

        let dxvkRoot = temporary.appendingPathComponent("dxvk", isDirectory: true)
        try FileManager.default.createDirectory(at: dxvkRoot, withIntermediateDirectories: true)
        for name in ["d3d9.dll", "d3dx9_31.dll", "d3dx9_31.dll.bak"] {
            try Data().write(to: dxvkRoot.appendingPathComponent(name))
        }
        let dxvkUnexpected = try unexpectedInstallFiles(
            installRoot: dxvkRoot, expectedRelativePaths: ["d3dx9_31.dll.bak"])
        guard dxvkUnexpected.isEmpty else {
            fatalError("DXVK compatibility files were incorrectly rejected: \(dxvkUnexpected)")
        }
    }
}
