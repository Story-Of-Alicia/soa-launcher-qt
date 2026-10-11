import Foundation
import Soa_Courier

extension Courier
{
    func mismatchedManifestIndices(
        operationID: UInt64,
        installRoot: URL,
        manifest: [ValidatedManifestEntry],
        relativePaths: [String]? = nil,
        phase: courier_phase,
        action: String,
        maximumConcurrency: Int = 4) async throws -> Set<Int>
    {
        guard !manifest.isEmpty else { return [] }
        if let relativePaths, relativePaths.count != manifest.count {
            throw Err("Internal manifest verification path count mismatch")
        }

        log(2, "Checking \(manifest.count) game files against the manifest")

        let mismatched = try await withThrowingTaskGroup(of: (Int, Bool).self) { group in
            var nextIndex = 0
            var completed = 0
            var mismatched = Set<Int>()
            let limit = max(1, min(maximumConcurrency, manifest.count))

            func addTask(_ index: Int)
            {
                let entry = manifest[index]
                let suppliedRelative = relativePaths?[index]
                group.addTask {
                    try Task.checkCancellation()
                    let relative = try suppliedRelative
                        ?? actualManagedRelativePath(installRoot: installRoot, entry: entry)
                    let local = try safeDestination(
                        root: installRoot, relativePath: relative, includeLeafSymlinkCheck: false)
                    let matches = try manifestFileMatches(at: local, entry: entry)
                    return (index, !matches)
                }
            }

            while nextIndex < limit {
                addTask(nextIndex)
                nextIndex += 1
            }

            while let result = try await group.next() {
                try Task.checkCancellation()
                completed += 1
                if result.1 {
                    mismatched.insert(result.0)
                }

                let percent = Int(Double(completed) / Double(manifest.count) * 100.0)
                reportProgress(
                    operationID,
                    phase,
                    "\(action) (\(completed)/\(manifest.count))",
                    percent,
                    0,
                    0,
                    0,
                    completed,
                    manifest.count)

                if nextIndex < manifest.count {
                    addTask(nextIndex)
                    nextIndex += 1
                }
            }

            return mismatched
        }

        log(2, "\(action) complete: \(manifest.count - mismatched.count) valid, \(mismatched.count) need attention")
        return mismatched
    }

    func startIntegrityCheck(installPath: String) -> UInt64
    {
        run { [self] operationID in
            let installRoot = try canonicalInstallRoot(installPath)
            try recoverInterruptedUpdate(installRoot: installRoot)

            guard let version = readLocalVersion(installPath: installPath),
                  !version.isEmpty else {
                throw Err("The installed game version could not be determined for verification")
            }

            reportProgress(operationID, courier_phase_preparing,
                           "Requesting manifest...", 0, 0, 0, 0, 0, 0)
            let manifest = try await fetchManifest(version: version)

            let relativePaths = try managedManifestPaths(installRoot: installRoot, manifest: manifest)

            let mismatched = try await mismatchedManifestIndices(
                operationID: operationID,
                installRoot: installRoot,
                manifest: manifest,
                relativePaths: relativePaths,
                phase: courier_phase_checking,
                action: "Checking")
            let unexpected = try unexpectedInstallFiles(
                installRoot: installRoot,
                expectedRelativePaths: relativePaths)

            var changes: [String] = []
            changes.reserveCapacity(mismatched.count + unexpected.count)
            for index in mismatched.sorted() {
                let relative = relativePaths[index]
                let local = try safeDestination(
                    root: installRoot,
                    relativePath: relative,
                    includeLeafSymlinkCheck: false)
                if isSymbolicLink(local.path) || FileManager.default.fileExists(atPath: local.path) {
                    changes.append("modified:\(relative)")
                    log(3, "Modified game file: \(logSafe(relative))")
                } else {
                    changes.append("missing:\(relative)")
                    log(3, "Missing game file: \(logSafe(relative))")
                }
            }
            for relative in unexpected {
                changes.append("unexpected:\(relative)")
                log(3, "Unexpected game file: \(logSafe(relative))")
            }

            let clean = changes.isEmpty
            reportDone(
                operationID,
                clean ? courier_result_up_to_date : courier_result_integrity_repair_required,
                clean ? "up-to-date" : changes.joined(separator: "\n"))
        }
    }
}
