import Foundation
import Soa_Courier

private struct PlannedReplacement: Sendable
{
    let entry: ValidatedManifestEntry
    let stagedRelativePath: String
    let targetRelativePath: String
}

private struct TransferProgressSnapshot
{
    let received: UInt64
    let throughput: UInt64
    let completedFiles: Int
    let shouldReport: Bool
}

private final class TransferProgressTracker: @unchecked Sendable
{
    private let lock = NSLock()
    private var receivedByIndex: [Int: UInt64]
    private var completed = Set<Int>()
    private var windowStart = Date()
    private var windowBytes: UInt64 = 0
    private var throughput: UInt64 = 0
    private var lastProgressReport = Date.distantPast

    init(initialReceived: [Int: UInt64])
    {
        self.receivedByIndex = initialReceived
    }

    func update(index: Int, received: UInt64, transferred: Int) -> TransferProgressSnapshot
    {
        lock.lock()
        defer { lock.unlock() }

        receivedByIndex[index] = received
        if transferred > 0 {
            windowBytes &+= UInt64(transferred)
        }
        let now = Date()
        let elapsed = now.timeIntervalSince(windowStart)
        if elapsed >= 1.0 && windowBytes > 0 {
            throughput = UInt64(Double(windowBytes) / elapsed)
            windowBytes = 0
            windowStart = now
        }
        let shouldReport = now.timeIntervalSince(lastProgressReport) >= 0.1
        if shouldReport {
            lastProgressReport = now
        }
        return snapshot(shouldReport: shouldReport)
    }

    func reset(index: Int) -> TransferProgressSnapshot
    {
        lock.lock()
        defer { lock.unlock() }
        receivedByIndex[index] = 0
        completed.remove(index)
        return snapshot(shouldReport: true)
    }

    func complete(index: Int, expectedSize: UInt64) -> TransferProgressSnapshot
    {
        lock.lock()
        defer { lock.unlock() }
        receivedByIndex[index] = expectedSize
        completed.insert(index)
        return snapshot(shouldReport: true)
    }

    private func snapshot(shouldReport: Bool) -> TransferProgressSnapshot
    {
        TransferProgressSnapshot(
            received: receivedByIndex.values.reduce(UInt64(0), +),
            throughput: throughput,
            completedFiles: completed.count,
            shouldReport: shouldReport)
    }
}

extension Courier
{
    private var stagingDirectoryName: String { ".soa-update-staging" }
    private var backupDirectoryName: String { ".soa-update-backup" }
    private var journalFileName: String { ".soa-update-journal.json" }
    private var stagingManifestFileName: String { ".soa-update-staging.json" }
    private var managedManifestFileName: String { ".soa-managed-manifest.json" }

    private func removeIfPresent(_ url: URL) throws
    {
        if FileManager.default.fileExists(atPath: url.path) || isSymbolicLink(url.path) {
            try FileManager.default.removeItem(at: url)
        }
    }

    private func createParent(of url: URL) throws
    {
        try FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    }

    private func metadataBackupURL(backupRoot: URL, name: String) -> URL
    {
        backupRoot.appendingPathComponent("__metadata", isDirectory: true)
            .appendingPathComponent(name)
    }

    private func restoreMetadata(installRoot: URL, backupRoot: URL,
                                 name: String, existedBefore: Bool) throws
    {
        let destination = installRoot.appendingPathComponent(name)
        let backup = metadataBackupURL(backupRoot: backupRoot, name: name)
        if FileManager.default.fileExists(atPath: backup.path) {
            try removeIfPresent(destination)
            try createParent(of: destination)
            try FileManager.default.moveItem(at: backup, to: destination)
        } else if !existedBefore {
            try removeIfPresent(destination)
        }
    }

    private func prepareInternalDirectory(installRoot: URL, name: String, preserve: Bool) throws -> URL
    {
        let raw = installRoot.appendingPathComponent(name, isDirectory: true)
        let manager = FileManager.default
        if manager.fileExists(atPath: raw.path) {
            let attributes = try manager.attributesOfItem(atPath: raw.path)
            guard attributes[.type] as? FileAttributeType == .typeDirectory else {
                throw Err("Launcher update workspace is not a directory: \(name)")
            }
            if !preserve {
                try manager.removeItem(at: raw)
                try manager.createDirectory(at: raw, withIntermediateDirectories: true)
            }
        } else {
            try manager.createDirectory(at: raw, withIntermediateDirectories: true)
        }

        let resolved = raw.resolvingSymlinksInPath().standardizedFileURL
        let rootPrefix = installRoot.path.hasSuffix("/") ? installRoot.path : installRoot.path + "/"
        guard resolved.path.hasPrefix(rootPrefix) else {
            throw Err("Launcher update workspace escapes the game installation directory")
        }
        return resolved
    }

    private func readStagingManifest(installRoot: URL) -> StagingManifest?
    {
        let url = installRoot.appendingPathComponent(stagingManifestFileName)
        guard let data = try? Data(contentsOf: url), data.count <= 16 * 1024 * 1024 else {
            return nil
        }
        return try? JSONDecoder().decode(StagingManifest.self, from: data)
    }

    private func quarantineRecoveryState(installRoot: URL) throws -> URL
    {
        let manager = FileManager.default
        let formatter = ISO8601DateFormatter()
        let suffix = formatter.string(from: Date())
            .replacingOccurrences(of: ":", with: "-")
        let quarantine = installRoot.appendingPathComponent(
            ".soa-recovery-quarantine-\(suffix)-\(UUID().uuidString)", isDirectory: true)
        try manager.createDirectory(at: quarantine, withIntermediateDirectories: true)
        for name in [journalFileName, backupDirectoryName, stagingDirectoryName, stagingManifestFileName] {
            let source = installRoot.appendingPathComponent(name)
            guard manager.fileExists(atPath: source.path) else { continue }
            let destination = quarantine.appendingPathComponent(name)
            try manager.moveItem(at: source, to: destination)
        }
        return quarantine
    }


    private func pruneRecoveryQuarantines(installRoot: URL, keep: Int = 2)
    {
        let manager = FileManager.default
        guard let entries = try? manager.contentsOfDirectory(
            at: installRoot,
            includingPropertiesForKeys: [.contentModificationDateKey],
            options: []) else { return }
        let quarantines = entries.filter {
            $0.lastPathComponent.hasPrefix(".soa-recovery-quarantine-")
        }.sorted {
            let left = (try? $0.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
            let right = (try? $1.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
            return left > right
        }
        for stale in quarantines.dropFirst(max(0, keep)) {
            try? manager.removeItem(at: stale)
        }
    }

    func recoverInterruptedUpdate(installRoot: URL) throws
    {
        let manager = FileManager.default
        let journalURL = installRoot.appendingPathComponent(journalFileName)
        let backupRootRaw = installRoot.appendingPathComponent(backupDirectoryName, isDirectory: true)
        guard manager.fileExists(atPath: journalURL.path) else {


            try? removeIfPresent(backupRootRaw)
            return
        }

        let journal: UpdateJournal
        do {
            let data = try Data(contentsOf: journalURL)
            guard data.count <= 4 * 1024 * 1024 else {
                throw Err("Update recovery journal is unexpectedly large")
            }
            journal = try JSONDecoder().decode(UpdateJournal.self, from: data)
            guard (1...3).contains(journal.schemaVersion),
                  journal.replacementPaths.count == journal.replacementHadOriginal.count else {
                throw Err("Update recovery journal is invalid")
            }
            if let paths = journal.obsoleteBackupPaths,
               paths.count != journal.obsoletePaths.count {
                throw Err("Update recovery journal has invalid cleanup metadata")
            }
            if journal.schemaVersion == 3,
               journal.obsoleteBackupPaths != journal.obsoletePaths.indices.map({ ".soa-obsolete/\($0)" }) {
                throw Err("Update recovery journal has invalid cleanup backup paths")
            }
            if let staged = journal.replacementStagedPaths,
               staged.count != journal.replacementPaths.count {
                throw Err("Update recovery journal has invalid staging metadata")
            }
        } catch {
            let quarantine = try? quarantineRecoveryState(installRoot: installRoot)
            let detail = quarantine?.lastPathComponent ?? "a recovery quarantine"
            throw Err("The interrupted update journal was corrupt and was moved to \(detail). Run repair again.")
        }

        let backupRoot = try prepareInternalDirectory(
            installRoot: installRoot, name: backupDirectoryName, preserve: true)
        var stagingRoot: URL?
        if journal.replacementStagedPaths != nil {
            stagingRoot = try prepareInternalDirectory(
                installRoot: installRoot, name: stagingDirectoryName, preserve: true)
        }
        log(3, "Recovering an interrupted game update")

        for index in journal.replacementPaths.indices.reversed() {
            let relative = try normalizedManifestPath(journal.replacementPaths[index])
            let destination = try safeDestination(
                root: installRoot, relativePath: relative, includeLeafSymlinkCheck: false)
            let backup = try safeDestination(
                root: backupRoot, relativePath: relative, includeLeafSymlinkCheck: false)
            let backupExists = manager.fileExists(atPath: backup.path) || isSymbolicLink(backup.path)
            let replacementReachedDestination = backupExists || !journal.replacementHadOriginal[index]

            if replacementReachedDestination,
               manager.fileExists(atPath: destination.path) || isSymbolicLink(destination.path),
               let stagedPaths = journal.replacementStagedPaths, let stagingRoot {
                let stagedRelative = try normalizedManifestPath(stagedPaths[index])
                let staged = try safeDestination(root: stagingRoot, relativePath: stagedRelative)
                try createParent(of: staged)
                if manager.fileExists(atPath: staged.path) {
                    try removeIfPresent(destination)
                } else {
                    try manager.moveItem(at: destination, to: staged)
                }
            }

            if backupExists {
                try removeIfPresent(destination)
                try createParent(of: destination)
                try manager.moveItem(at: backup, to: destination)
            } else if !journal.replacementHadOriginal[index] {
                try removeIfPresent(destination)
            }
        }

        for index in journal.obsoletePaths.indices.reversed() {
            let relative = journal.obsoletePaths[index]
            let destination = try safeExistingDestination(root: installRoot, relativePath: relative)
            let backupRelative = journal.obsoleteBackupPaths?[index] ?? relative
            let backup = try safeExistingDestination(root: backupRoot, relativePath: backupRelative)
            if manager.fileExists(atPath: backup.path) || isSymbolicLink(backup.path) {
                try removeIfPresent(destination)
                try createParent(of: destination)
                try manager.moveItem(at: backup, to: destination)
            }
        }

        try restoreMetadata(
            installRoot: installRoot, backupRoot: backupRoot,
            name: "version.json", existedBefore: journal.versionMetadataExisted)
        try restoreMetadata(
            installRoot: installRoot, backupRoot: backupRoot,
            name: managedManifestFileName, existedBefore: journal.managedMetadataExisted)

        try? removeIfPresent(backupRoot)
        try removeIfPresent(journalURL)
    }

    private func backupMetadataIfPresent(installRoot: URL, backupRoot: URL, name: String) throws
    {
        let source = installRoot.appendingPathComponent(name)
        guard FileManager.default.fileExists(atPath: source.path) else { return }
        let backup = metadataBackupURL(backupRoot: backupRoot, name: name)
        try createParent(of: backup)
        try FileManager.default.moveItem(at: source, to: backup)
    }

    private func availableDiskSpace(at root: URL) -> UInt64?
    {
        guard let attributes = try? FileManager.default.attributesOfFileSystem(forPath: root.path),
              let value = attributes[.systemFreeSize] as? NSNumber else { return nil }
        return value.uint64Value
    }

    private func prepareReplacement(
        operationID: UInt64,
        tracker: TransferProgressTracker,
        index: Int,
        count: Int,
        planned: PlannedReplacement,
        stagingRoot: URL,
        version: String,
        totalBytes: UInt64) async throws
    {
        let fileManager = FileManager.default
        let staged = try safeDestination(root: stagingRoot, relativePath: planned.stagedRelativePath)
        try createParent(of: staged)
        let url = try urlForContent(
            base: cdnBaseURL, version: version, relativePath: planned.entry.relativePath)
        let expectedSize = UInt64(planned.entry.manifest.size)
        var performedCleanRedownload = false
        var downloadedThisRun = false

        while true {
            try Task.checkCancellation()
            var storedSize = (try? fileManager.attributesOfItem(atPath: staged.path)[.size] as? NSNumber)?.uint64Value ?? 0
            if storedSize > expectedSize {
                try removeIfPresent(staged)
                storedSize = 0
                _ = tracker.reset(index: index)
            }

            if storedSize < expectedSize {
                downloadedThisRun = true
                let start = tracker.update(index: index, received: storedSize, transferred: 0)
                let startOrdinal = min(count, start.completedFiles + 1)
                reportProgress(
                    operationID, courier_phase_downloading,
                    storedSize > 0
                        ? "Resuming (\(startOrdinal)/\(count))"
                        : "Downloading (\(startOrdinal)/\(count))",
                    totalBytes > 0
                        ? Int(Double(start.received) / Double(totalBytes) * 100.0)
                        : 0,
                    start.received, totalBytes, start.throughput, startOrdinal, count)

                let resumedThisRequest = storedSize > 0
                try await streamingDownload(
                    from: url,
                    to: staged,
                    expectedSize: planned.entry.manifest.size)
                { byteCount, fileReceived in
                    let snapshot = tracker.update(
                        index: index, received: fileReceived, transferred: byteCount)
                    guard snapshot.shouldReport else { return }
                    let ordinal = min(count, snapshot.completedFiles + 1)
                    self.reportProgress(
                        operationID, courier_phase_downloading,
                        resumedThisRequest
                            ? "Resuming (\(ordinal)/\(count))"
                            : "Downloading (\(ordinal)/\(count))",
                        totalBytes > 0
                            ? Int(Double(snapshot.received) / Double(totalBytes) * 100.0)
                            : 0,
                        snapshot.received, totalBytes, snapshot.throughput, ordinal, count)
                }
            }

            let beforeVerify = tracker.update(
                index: index, received: expectedSize, transferred: 0)
            let verifyOrdinal = min(count, beforeVerify.completedFiles + 1)
            reportProgress(
                operationID, courier_phase_verifying,
                "Verifying (\(verifyOrdinal)/\(count))",
                totalBytes > 0
                    ? Int(Double(beforeVerify.received) / Double(totalBytes) * 100.0)
                    : 0,
                beforeVerify.received, totalBytes, beforeVerify.throughput, verifyOrdinal, count)

            let actualHash = try manifestHashOfFile(
                at: staged.path,
                expectedHash: planned.entry.manifest.hash)
            if actualHash?.caseInsensitiveCompare(planned.entry.manifest.hash) == .orderedSame {
                let completed = tracker.complete(index: index, expectedSize: expectedSize)
                let completedOrdinal = completed.completedFiles
                reportProgress(
                    operationID, courier_phase_verifying,
                    "Verified (\(completedOrdinal)/\(count))",
                    totalBytes > 0
                        ? Int(Double(completed.received) / Double(totalBytes) * 100.0)
                        : 100,
                    completed.received, totalBytes, completed.throughput, completedOrdinal, count)

                let safePath = logSafe(planned.entry.relativePath)
                if downloadedThisRun {
                    log(1, "Downloaded \(expectedSize) bytes of \(safePath)")
                } else {
                    log(1, "Reused \(expectedSize) verified bytes of \(safePath)")
                }
                return
            }

            try removeIfPresent(staged)
            _ = tracker.reset(index: index)
            if performedCleanRedownload {
                throw Err("Hash mismatch for \(planned.entry.relativePath)")
            }
            performedCleanRedownload = true
            downloadedThisRun = true
            log(3, "Saved partial data for \(planned.entry.relativePath) was invalid; retrying that file from the beginning")
        }
    }

    func startUpdateCheck(installPath: String) -> UInt64
    {
        run { [self] operationID in
            let installRoot = try canonicalInstallRoot(installPath)
            try recoverInterruptedUpdate(installRoot: installRoot)

            reportProgress(operationID, courier_phase_preparing,
                           "Requesting game version...", 0, 0, 0, 0, 0, 0)
            let remote = try await fetchRemoteVersion()
            let local = readLocalVersion(installPath: installPath)
            reportDone(
                operationID,
                local == remote ? courier_result_up_to_date : courier_result_update_available,
                local == remote ? "up-to-date" : "update-available")
        }
    }

    func startUpdate(installPath: String) -> UInt64
    {
        startSync(installPath: installPath, useInstalledVersion: false)
    }

    func startRepair(installPath: String) -> UInt64
    {
        startSync(installPath: installPath, useInstalledVersion: true)
    }

    private func startSync(installPath: String, useInstalledVersion: Bool) -> UInt64
    {
        run { [self] operationID in
            let fileManager = FileManager.default
            let installRoot = try canonicalInstallRoot(installPath, create: true)
            try recoverInterruptedUpdate(installRoot: installRoot)

            let version: String
            if useInstalledVersion {
                guard let installed = readLocalVersion(installPath: installPath),
                      !installed.isEmpty else {
                    throw Err("The installed game version could not be determined for repair")
                }
                version = installed
            } else {
                reportProgress(operationID, courier_phase_preparing,
                               "Requesting game version...", 0, 0, 0, 0, 0, 0)
                version = try await fetchRemoteVersion()
            }
            reportProgress(operationID, courier_phase_preparing,
                           "Requesting manifest...", 0, 0, 0, 0, 0, 0)
            let manifest = try await fetchManifest(version: version)

            let targetPaths = try managedManifestPaths(installRoot: installRoot, manifest: manifest)

            let mismatched = try await mismatchedManifestIndices(
                operationID: operationID,
                installRoot: installRoot,
                manifest: manifest,
                relativePaths: targetPaths,
                phase: courier_phase_checking,
                action: "Checking")
            var needed: [PlannedReplacement] = []
            needed.reserveCapacity(mismatched.count)
            for index in manifest.indices where mismatched.contains(index) {
                let entry = manifest[index]
                needed.append(PlannedReplacement(
                    entry: entry,
                    stagedRelativePath: entry.relativePath,
                    targetRelativePath: targetPaths[index]))
            }

            let priorManaged = readManagedManifest(installRoot: installRoot)
            let targetKeys = Set(targetPaths.map {
                $0.folding(options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive],
                           locale: Locale(identifier: "en_US_POSIX"))
            })
            var obsolete: [String] = []
            for raw in priorManaged?.files ?? [] {
                do {
                    let normalized = try normalizedManifestPath(raw)
                    let key = normalized.folding(
                        options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive],
                        locale: Locale(identifier: "en_US_POSIX"))
                    if !targetKeys.contains(key) {
                        obsolete.append(normalized)
                    }
                } catch {
                    log(3, "Ignoring unsafe path in local managed manifest: \(raw)")
                }
            }

            let unexpected = try unexpectedInstallFiles(
                installRoot: installRoot,
                expectedRelativePaths: targetPaths)
            if !unexpected.isEmpty {
                for path in unexpected.prefix(20) {
                    log(3, "Removing unexpected game file: \(logSafe(path))")
                }
                if unexpected.count > 20 {
                    log(3, "\(unexpected.count - 20) additional unexpected game files were omitted from the log")
                }
                obsolete.append(contentsOf: unexpected)
            }
            obsolete = Array(Set(obsolete)).sorted {
                $0.localizedStandardCompare($1) == .orderedAscending
            }

            let totalBytes = try needed.reduce(UInt64(0)) { partial, planned in
                let size = UInt64(planned.entry.manifest.size)
                let (sum, overflow) = partial.addingReportingOverflow(size)
                if overflow { throw Err("Manifest total download size is too large") }
                return sum
            }

            let stagingRootRaw = installRoot.appendingPathComponent(stagingDirectoryName, isDirectory: true)
            let stagingManifestURL = installRoot.appendingPathComponent(stagingManifestFileName)
            let expectedStagingManifest = StagingManifest(
                schemaVersion: 1,
                releaseVersion: version,
                files: manifest.map { entry in
                    StagingManifestEntry(
                        path: entry.relativePath,
                        hash: entry.manifest.hash.lowercased(),
                        size: entry.manifest.size)
                })

            let canResume = readStagingManifest(installRoot: installRoot) == expectedStagingManifest
            if !canResume {
                try removeIfPresent(stagingRootRaw)
                try removeIfPresent(stagingManifestURL)
            } else if !needed.isEmpty {
                log(2, "Resuming verified or partial files from the previous download attempt")
            }

            let stagingRoot = try prepareInternalDirectory(
                installRoot: installRoot, name: stagingDirectoryName, preserve: true)
            let backupRoot = try prepareInternalDirectory(
                installRoot: installRoot, name: backupDirectoryName, preserve: false)
            try atomicWriteJSON(expectedStagingManifest, to: stagingManifestURL)

            var resumableBytes: UInt64 = 0
            var initialReceived: [Int: UInt64] = [:]
            initialReceived.reserveCapacity(needed.count)
            for (index, planned) in needed.enumerated() {
                let staged = try safeDestination(root: stagingRoot, relativePath: planned.stagedRelativePath)
                let storedSize = (try? fileManager.attributesOfItem(atPath: staged.path)[.size] as? NSNumber)?.uint64Value ?? 0
                let reusable = min(storedSize, UInt64(planned.entry.manifest.size))
                initialReceived[index] = reusable
                resumableBytes += reusable
            }
            let remainingBytes = totalBytes > resumableBytes ? totalBytes - resumableBytes : 0
            var backupBytes: UInt64 = 0
            for relative in needed.map(\.targetRelativePath) {
                let destination = try safeDestination(
                    root: installRoot, relativePath: relative, includeLeafSymlinkCheck: false)
                if let number = try? fileManager.attributesOfItem(atPath: destination.path)[.size] as? NSNumber {
                    let (sum, overflow) = backupBytes.addingReportingOverflow(number.uint64Value)
                    if overflow { throw Err("Update backup size is too large") }
                    backupBytes = sum
                }
            }
            for relative in obsolete {
                let destination = try safeExistingDestination(root: installRoot, relativePath: relative)
                if let number = try? fileManager.attributesOfItem(atPath: destination.path)[.size] as? NSNumber {
                    let (sum, overflow) = backupBytes.addingReportingOverflow(number.uint64Value)
                    if overflow { throw Err("Update backup size is too large") }
                    backupBytes = sum
                }
            }
            let reserve: UInt64 = 256 * 1024 * 1024
            let (downloadAndBackup, firstOverflow) = remainingBytes.addingReportingOverflow(backupBytes)
            let (requiredBytes, secondOverflow) = downloadAndBackup.addingReportingOverflow(reserve)
            if firstOverflow || secondOverflow {
                throw Err("Update disk-space requirement is too large")
            }
            if requiredBytes > 0, let free = availableDiskSpace(at: installRoot), free < requiredBytes {
                throw Err("Not enough free disk space to stage, back up, and finish this update")
            }

            let tracker = TransferProgressTracker(initialReceived: initialReceived)
            if !needed.isEmpty {
                let concurrency = min(4, needed.count)
                try await withThrowingTaskGroup(of: Void.self) { group in
                    var nextIndex = 0

                    while nextIndex < concurrency {
                        let index = nextIndex
                        let planned = needed[index]
                        group.addTask { [self] in
                            try await prepareReplacement(
                                operationID: operationID,
                                tracker: tracker,
                                index: index,
                                count: needed.count,
                                planned: planned,
                                stagingRoot: stagingRoot,
                                version: version,
                                totalBytes: totalBytes)
                        }
                        nextIndex += 1
                    }

                    while try await group.next() != nil {
                        try Task.checkCancellation()
                        if nextIndex < needed.count {
                            let index = nextIndex
                            let planned = needed[index]
                            group.addTask { [self] in
                                try await prepareReplacement(
                                    operationID: operationID,
                                    tracker: tracker,
                                    index: index,
                                    count: needed.count,
                                    planned: planned,
                                    stagingRoot: stagingRoot,
                                    version: version,
                                    totalBytes: totalBytes)
                            }
                            nextIndex += 1
                        }
                    }
                }
            }

            let replacementPaths = needed.map(\.targetRelativePath)
            let replacementHadOriginal = try replacementPaths.map {
                let destination = try safeDestination(
                    root: installRoot, relativePath: $0, includeLeafSymlinkCheck: false)
                return fileManager.fileExists(atPath: destination.path)
                    || isSymbolicLink(destination.path)
            }
            let obsoleteBackupPaths = obsolete.indices.map { ".soa-obsolete/\($0)" }
            let journal = UpdateJournal(
                schemaVersion: 3,
                replacementPaths: replacementPaths,
                replacementHadOriginal: replacementHadOriginal,
                replacementStagedPaths: needed.map(\.stagedRelativePath),
                obsoletePaths: obsolete,
                obsoleteBackupPaths: obsoleteBackupPaths,
                versionMetadataExisted: fileManager.fileExists(atPath: installRoot.appendingPathComponent("version.json").path),
                managedMetadataExisted: fileManager.fileExists(atPath: installRoot.appendingPathComponent(managedManifestFileName).path))
            let journalURL = installRoot.appendingPathComponent(journalFileName)
            try atomicWriteJSON(journal, to: journalURL)

            do {
                try backupMetadataIfPresent(installRoot: installRoot, backupRoot: backupRoot, name: "version.json")
                try backupMetadataIfPresent(installRoot: installRoot, backupRoot: backupRoot, name: managedManifestFileName)

                for planned in needed {
                    try Task.checkCancellation()
                    let staged = try safeDestination(root: stagingRoot, relativePath: planned.stagedRelativePath)
                    let destination = try safeDestination(
                        root: installRoot, relativePath: planned.targetRelativePath,
                        includeLeafSymlinkCheck: false)
                    let backup = try safeDestination(root: backupRoot, relativePath: planned.targetRelativePath)
                    try createParent(of: destination)

                    if fileManager.fileExists(atPath: destination.path)
                        || isSymbolicLink(destination.path) {
                        try createParent(of: backup)
                        try fileManager.moveItem(at: destination, to: backup)
                    }
                    try fileManager.moveItem(at: staged, to: destination)
                }

                for (index, relative) in obsolete.enumerated() {
                    try Task.checkCancellation()
                    let destination = try safeExistingDestination(root: installRoot, relativePath: relative)
                    guard fileManager.fileExists(atPath: destination.path)
                            || isSymbolicLink(destination.path) else { continue }
                    let backup = try safeExistingDestination(root: backupRoot, relativePath: obsoleteBackupPaths[index])
                    try createParent(of: backup)
                    try fileManager.moveItem(at: destination, to: backup)
                }

                try writeVersionJSON(installRoot: installRoot, version: version)
                try atomicWriteJSON(
                    ManagedManifest(schemaVersion: 1, releaseVersion: version, files: targetPaths.sorted()),
                    to: installRoot.appendingPathComponent(managedManifestFileName))

                try removeIfPresent(journalURL)
                try? removeIfPresent(stagingRoot)
                try? removeIfPresent(stagingManifestURL)
                try? removeIfPresent(backupRoot)
                pruneRecoveryQuarantines(installRoot: installRoot)
            } catch {
                do {
                    try recoverInterruptedUpdate(installRoot: installRoot)
                } catch let recoveryError {
                    log(4, "Update failed and rollback also failed: \(recoveryError)")
                }
                throw error
            }

            let message: String
            if needed.isEmpty && obsolete.isEmpty {
                message = "Already up to date."
            } else {
                message = "Updated \(needed.count) file(s) and removed \(obsolete.count) obsolete file(s)."
            }
            reportProgress(operationID, courier_phase_downloading, message, 100,
                           totalBytes, totalBytes, 0, needed.count, needed.count)
            reportDone(operationID, courier_result_completed, message)
        }
    }
}
