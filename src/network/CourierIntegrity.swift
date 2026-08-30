import Foundation
import Soa_Courier

extension Courier
{
    func startIntegrityCheck(installPath: String) -> UInt64
    {
        run { [self] operationID in
            let installRoot = try canonicalInstallRoot(installPath)
            try recoverInterruptedUpdate(installRoot: installRoot)

            reportProgress(operationID, courier_phase_preparing,
                           "Requesting game version...", 0, 0, 0, 0, 0, 0)
            let version = try await fetchRemoteVersion()
            reportProgress(operationID, courier_phase_preparing,
                           "Requesting manifest...", 0, 0, 0, 0, 0, 0)
            let manifest = try await fetchManifest(version: version)

            var bad = 0
            for (index, entry) in manifest.enumerated() {
                try Task.checkCancellation()
                let relative = try actualManagedRelativePath(installRoot: installRoot, entry: entry)
                let local = try safeDestination(root: installRoot, relativePath: relative)
                let percent = Int(Double(index + 1) / Double(manifest.count) * 100)
                reportProgress(operationID, courier_phase_verifying,
                               "Verifying (\(index + 1)/\(manifest.count))", percent,
                               0, 0, 0, index + 1, manifest.count)
                let hash = try manifestHashOfFile(
                    at: local.path,
                    expectedHash: entry.manifest.hash) { _ in
                    self.reportProgress(operationID, courier_phase_verifying,
                                        "Verifying (\(index + 1)/\(manifest.count))", percent,
                                        0, 0, 0, index + 1, manifest.count)
                }
                if hash == nil || hash?.caseInsensitiveCompare(entry.manifest.hash) != .orderedSame {
                    bad += 1
                }
            }
            reportDone(operationID, courier_result_completed, "\(bad)")
        }
    }
}
