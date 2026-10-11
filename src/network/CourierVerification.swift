import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

private func digestOfFile<H: StreamingFileHasher>(
    at path: String,
    hasher: inout H,
    progress: ((UInt64) -> Void)? = nil) throws -> String?
{
    guard let handle = FileHandle(forReadingAtPath: path) else { return nil }
    defer { try? handle.close() }

    var processed: UInt64 = 0
    var nextReport: UInt64 = 8 * 1024 * 1024
    while true {
        try Task.checkCancellation()
        let chunk = try handle.read(upToCount: 1 << 20) ?? Data()
        if chunk.isEmpty { break }
        hasher.update(chunk)
        processed += UInt64(chunk.count)
        if processed >= nextReport {
            progress?(processed)
            nextReport = processed + 8 * 1024 * 1024
        }
    }
    progress?(processed)
    return hasher.finalizeHex()
}

func manifestHashOfFile(at path: String, expectedHash: String,
                        progress: ((UInt64) -> Void)? = nil) throws -> String?
{
    if expectedHash.count == 64 {
        var hasher = SHA256()
        return try digestOfFile(
            at: path, hasher: &hasher, progress: progress)
    }
    var hasher = MD5()
    return try digestOfFile(
        at: path, hasher: &hasher, progress: progress)
}

struct VerifiedFileStamp: Codable, Equatable
{
    let device: UInt64
    let inode: UInt64
    let size: Int64
    let modifiedSeconds: Int64
    let modifiedNanoseconds: Int64
    let changedSeconds: Int64
    let changedNanoseconds: Int64
}

func fileStamp(_ path: String) -> VerifiedFileStamp?
{
    var info = stat()
    guard path.withCString({ lstat($0, &info) }) == 0,
          (info.st_mode & mode_t(S_IFMT)) == mode_t(S_IFREG) else { return nil }
#if canImport(Darwin)
    let modified = info.st_mtimespec
    let changed = info.st_ctimespec
#else
    let modified = info.st_mtim
    let changed = info.st_ctim
#endif
    return VerifiedFileStamp(
        device: UInt64(info.st_dev), inode: UInt64(info.st_ino), size: Int64(info.st_size),
        modifiedSeconds: Int64(modified.tv_sec), modifiedNanoseconds: Int64(modified.tv_nsec),
        changedSeconds: Int64(changed.tv_sec), changedNanoseconds: Int64(changed.tv_nsec))
}

func manifestFileMatches(at local: URL, entry: ValidatedManifestEntry) throws -> Bool
{
    guard let before = fileStamp(local.path), before.size == Int64(entry.manifest.size) else {
        return false
    }
    let hash = try manifestHashOfFile(at: local.path, expectedHash: entry.manifest.hash)
    return hash?.caseInsensitiveCompare(entry.manifest.hash) == .orderedSame
        && fileStamp(local.path) == before
}

func managedManifestPaths(installRoot: URL, manifest: [ValidatedManifestEntry]) throws -> [String]
{
    var keys = Set<String>()
    return try manifest.map { entry in
        let relative = try actualManagedRelativePath(installRoot: installRoot, entry: entry)
        guard keys.insert(installPathKey(relative)).inserted else {
            throw Err("Manifest entries resolve to the same destination: \(relative)")
        }
        return relative
    }
}
