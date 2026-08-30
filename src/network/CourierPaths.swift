import Foundation

private let internalTopLevelNames: Set<String> = [
    ".soa-update-staging",
    ".soa-update-backup",
    ".soa-update-journal.json",
    ".soa-managed-manifest.json",
    "version.json",
]
private let maximumManifestFiles = 50_000
private let maximumManifestTotalBytes: UInt64 = 100 * 1024 * 1024 * 1024
private let maximumManifestFileBytes: UInt64 = 32 * 1024 * 1024 * 1024
private let maximumManifestPathBytes = 240
private let maximumManifestComponentBytes = 120
private let windowsReservedNames: Set<String> = [
    "con", "prn", "aux", "nul",
    "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
    "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
]
private let windowsInvalidCharacters = CharacterSet(charactersIn: "<>:\"|?*")


func normalizedManifestPath(_ raw: String) throws -> String
{
    if raw.isEmpty || raw.contains("\0") {
        throw Err("Manifest contains an empty or invalid path")
    }

    let slashPath = raw.replacingOccurrences(of: "\\", with: "/")
    if slashPath.hasPrefix("/") || slashPath.hasPrefix("~") || slashPath.hasPrefix("//") {
        throw Err("Manifest contains an absolute path: \(raw)")
    }

    if slashPath.range(of: #"^[A-Za-z]:"#, options: .regularExpression) != nil {
        throw Err("Manifest contains a drive-qualified path: \(raw)")
    }

    let parts = slashPath.split(separator: "/", omittingEmptySubsequences: false)
    if parts.isEmpty {
        throw Err("Manifest contains an empty path")
    }

    var normalized: [String] = []
    normalized.reserveCapacity(parts.count)
    for partSub in parts {
        let part = String(partSub)
        if part.isEmpty || part == "." || part == ".." {
            throw Err("Manifest contains an unsafe path component: \(raw)")
        }
        if part.contains("\0") || part.utf8.count > maximumManifestComponentBytes
            || part.unicodeScalars.contains(where: { $0.value < 32 }) {
            throw Err("Manifest contains an invalid or oversized path component: \(raw)")
        }
        if part.hasSuffix(" ") || part.hasSuffix(".")
            || part.unicodeScalars.contains(where: { windowsInvalidCharacters.contains($0) }) {
            throw Err("Manifest contains a Windows-incompatible path component: \(raw)")
        }
        let reservedStem = part.split(separator: ".", omittingEmptySubsequences: false)
            .first.map(String.init)?.lowercased() ?? ""
        if windowsReservedNames.contains(reservedStem) {
            throw Err("Manifest contains a reserved Windows path component: \(raw)")
        }
        normalized.append(part)
    }

    if let first = normalized.first,
       internalTopLevelNames.contains(first.lowercased())
        || first.lowercased().hasPrefix(".soa-") {
        throw Err("Manifest path conflicts with launcher update metadata: \(raw)")
    }

    let joined = normalized.joined(separator: "/")
    if joined.utf8.count > maximumManifestPathBytes {
        throw Err("Manifest path is too long: \(raw)")
    }
    return joined
}

func validateManifest(_ manifest: Manifest) throws -> [ValidatedManifestEntry]
{
    guard !manifest.files.isEmpty else { throw Err("Manifest has no files") }
    guard manifest.files.count <= maximumManifestFiles else {
        throw Err("Manifest contains too many files")
    }

    var seen: Set<String> = []
    var totalBytes: UInt64 = 0
    var result: [ValidatedManifestEntry] = []
    result.reserveCapacity(manifest.files.count)

    for entry in manifest.files {
        guard entry.size >= 0 else { throw Err("Manifest contains a negative file size: \(entry.path)") }
        let fileBytes = UInt64(entry.size)
        guard fileBytes <= maximumManifestFileBytes else {
            throw Err("Manifest file is too large: \(entry.path)")
        }
        let (newTotal, overflow) = totalBytes.addingReportingOverflow(fileBytes)
        guard !overflow && newTotal <= maximumManifestTotalBytes else {
            throw Err("Manifest total size is too large")
        }
        totalBytes = newTotal
        let hash = entry.hash.trimmingCharacters(in: .whitespacesAndNewlines)
        guard (hash.count == 32 || hash.count == 64)
                && hash.allSatisfy({ $0.isHexDigit }) else {
            throw Err(
                "Manifest contains an invalid MD5 or SHA-256 value for \(entry.path)")
        }

        let relative = try normalizedManifestPath(entry.path)
        let key = relative.folding(
            options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive],
            locale: Locale(identifier: "en_US_POSIX"))

        guard seen.insert(key).inserted else {
            throw Err("Manifest contains duplicate or case-colliding paths: \(entry.path)")
        }

        result.append(ValidatedManifestEntry(
            manifest: ManifestEntry(path: entry.path, hash: hash.lowercased(), size: entry.size),
            relativePath: relative,
            collisionKey: key))
    }

    for entry in result {
        let components = entry.collisionKey.split(separator: "/").map(String.init)
        if components.count < 2 { continue }
        var prefix = components[0]
        for component in components.dropFirst().dropLast() {
            if seen.contains(prefix) {
                throw Err("Manifest contains a file-versus-directory path collision: \(entry.relativePath)")
            }
            prefix += "/" + component
        }
        if seen.contains(prefix) {
            throw Err("Manifest contains a file-versus-directory path collision: \(entry.relativePath)")
        }
    }

    return result
}

func canonicalInstallRoot(_ installPath: String, create: Bool = false) throws -> URL
{
    let fm = FileManager.default
    if create {
        try fm.createDirectory(atPath: installPath, withIntermediateDirectories: true)
    }

    let root = URL(fileURLWithPath: installPath, isDirectory: true)
        .standardizedFileURL
        .resolvingSymlinksInPath()

    guard root.path != "/" && !root.path.isEmpty else {
        throw Err("Refusing to use the filesystem root as a game installation directory")
    }
    return root
}

private func isSymbolicLink(_ path: String) -> Bool
{
    guard let attributes = try? FileManager.default.attributesOfItem(atPath: path),
          let type = attributes[.type] as? FileAttributeType else { return false }
    return type == .typeSymbolicLink
}

func ensureNoSymlinkEscape(root: URL, relativePath: String, includeLeaf: Bool) throws
{
    let parts = relativePath.split(separator: "/").map(String.init)
    let count = includeLeaf ? parts.count : max(0, parts.count - 1)
    var current = root

    for part in parts.prefix(count) {
        current.appendPathComponent(part, isDirectory: true)
        if FileManager.default.fileExists(atPath: current.path) && isSymbolicLink(current.path) {
            throw Err("Refusing manifest destination through a symbolic link: \(relativePath)")
        }
    }
}

func safeDestination(root: URL, relativePath: String, includeLeafSymlinkCheck: Bool = true) throws -> URL
{
    let normalized = try normalizedManifestPath(relativePath)
    try ensureNoSymlinkEscape(root: root, relativePath: normalized, includeLeaf: includeLeafSymlinkCheck)

    var destination = root
    for part in normalized.split(separator: "/") {
        destination.appendPathComponent(String(part), isDirectory: false)
    }
    destination = destination.standardizedFileURL

    let rootPrefix = root.path.hasSuffix("/") ? root.path : root.path + "/"
    guard destination.path.hasPrefix(rootPrefix) else {
        throw Err("Manifest destination escapes the installation directory: \(relativePath)")
    }
    return destination
}

func urlForContent(base: String, version: String, relativePath: String) throws -> URL
{
    guard var url = URL(string: base) else { throw Err("Invalid CDN base URL") }
    url.appendPathComponent(version)
    for part in relativePath.split(separator: "/") {
        url.appendPathComponent(String(part))
    }
    return url
}

func actualManagedRelativePath(installRoot: URL, entry: ValidatedManifestEntry) throws -> String
{
    let fileName = URL(fileURLWithPath: entry.relativePath).lastPathComponent
    guard dxvkBackedUpDlls.contains(where: { fileName.caseInsensitiveCompare($0) == .orderedSame }),
          !entry.relativePath.contains("/") else {
        return entry.relativePath
    }

    let direct = try safeDestination(root: installRoot, relativePath: entry.relativePath)
    let backupRelative = entry.relativePath + ".bak"
    let backup = try safeDestination(root: installRoot, relativePath: backupRelative)
    let dxvk = installRoot.appendingPathComponent("d3d9.dll")
    if FileManager.default.fileExists(atPath: backup.path)
        || FileManager.default.fileExists(atPath: dxvk.path)
    {
        _ = direct
        return backupRelative
    }
    return entry.relativePath
}
