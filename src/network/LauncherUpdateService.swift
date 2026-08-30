import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if canImport(CryptoKit)
import CryptoKit
#endif
import Soa_Courier

struct LauncherUpdateSelection
{
    let version: String
    let minimumVersion: String
    let message: String
    let packageKind: String
    let fileName: String
    let packageURLs: [URL]
    let sha256: String
    let expectedSize: UInt64
    let required: Bool

    var packageURL: URL { packageURLs[0] }
}

struct LauncherServiceFailure: Error
{
    let code: soa_launcher_error
    let status: Int
    let detail: String

    init(_ code: soa_launcher_error, _ detail: String, status: Int = 0)
    {
        self.code = code
        self.status = status
        self.detail = detail
    }
}

final class LauncherAssetDelegate: NSObject, URLSessionDataDelegate, URLSessionTaskDelegate, @unchecked Sendable
{
    private let destination: URL
    private let expectedSize: UInt64
    private let expectedDigest: String
    private let progress: @Sendable (UInt64, UInt64) -> Void
    private let queue: OperationQueue
    private let cancellation = URLSessionTaskCancellationGate()
    private var continuation: CheckedContinuation<Void, Error>?
    private var handle: FileHandle?
    private var terminalError: LauncherServiceFailure?
    private var received: UInt64 = 0
    private var hasher = SHA256()
    private var completed = false

    init(destination: URL,
         expectedSize: UInt64,
         expectedDigest: String,
         progress: @escaping @Sendable (UInt64, UInt64) -> Void)
    {
        self.destination = destination
        self.expectedSize = expectedSize
        self.expectedDigest = expectedDigest
        self.progress = progress
        self.queue = OperationQueue()
        self.queue.maxConcurrentOperationCount = 1
        self.queue.qualityOfService = .utility
    }

    func start(request: URLRequest, configuration: URLSessionConfiguration) async throws
    {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { continuation in
                self.continuation = continuation
                let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
                let task = session.dataTask(with: request)
                self.cancellation.start(task)
            }
        }, onCancel: {
            self.cancellation.cancel()
        })
    }

    func urlSession(_ session: URLSession,
                    task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void)
    {
        guard let source = response.url, let destination = request.url,
              source.scheme?.lowercased() == "https",
              destination.scheme?.lowercased() == "https",
              LauncherUpdateService.trustedReleaseHost(source.host?.lowercased() ?? ""),
              LauncherUpdateService.trustedReleaseHost(destination.host?.lowercased() ?? "") else {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_unsafe_url,
                "The launcher update redirected to an untrusted URL")
            completionHandler(nil)
            return
        }
        completionHandler(request)
    }

    func urlSession(_ session: URLSession,
                    dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void)
    {
        guard let http = response as? HTTPURLResponse else {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_network,
                "The launcher update returned a non-HTTP response")
            completionHandler(.cancel)
            return
        }
        guard (200...299).contains(http.statusCode) else {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_http,
                "HTTP \(http.statusCode)",
                status: http.statusCode)
            completionHandler(.cancel)
            return
        }
        if http.expectedContentLength >= 0
            && UInt64(http.expectedContentLength) != expectedSize {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_size_mismatch,
                "The server reported an unexpected launcher update size",
                status: http.statusCode)
            completionHandler(.cancel)
            return
        }

        do {
            try FileManager.default.createDirectory(
                at: destination.deletingLastPathComponent(),
                withIntermediateDirectories: true)
            try? FileManager.default.removeItem(at: destination)
            guard FileManager.default.createFile(atPath: destination.path, contents: nil) else {
                throw LauncherServiceFailure(
                    soa_launcher_error_destination,
                    "The launcher could not create the temporary update file")
            }
            handle = try FileHandle(forWritingTo: destination)
            completionHandler(.allow)
        } catch let failure as LauncherServiceFailure {
            terminalError = failure
            completionHandler(.cancel)
        } catch {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_destination,
                error.localizedDescription)
            completionHandler(.cancel)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data)
    {
        guard terminalError == nil else { return }
        let count = UInt64(data.count)
        if received > expectedSize || count > expectedSize - received {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_size_mismatch,
                "The launcher update exceeded its expected size")
            dataTask.cancel()
            return
        }
        do {
            try handle?.write(contentsOf: data)
            hasher.update(data)
            received += count
            progress(received, expectedSize)
        } catch {
            terminalError = LauncherServiceFailure(
                soa_launcher_error_write,
                error.localizedDescription)
            dataTask.cancel()
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?)
    {
        guard !completed else { return }
        completed = true
        try? handle?.synchronize()
        try? handle?.close()
        handle = nil

        defer {
            cancellation.clear()
            session.finishTasksAndInvalidate()
            continuation = nil
        }

        if let terminalError {
            continuation?.resume(throwing: terminalError)
            return
        }
        if let error {
            if (error as NSError).code == NSURLErrorCancelled {
                continuation?.resume(throwing: CancellationError())
            } else {
                continuation?.resume(throwing: LauncherServiceFailure(
                    soa_launcher_error_network,
                    error.localizedDescription))
            }
            return
        }
        guard received == expectedSize else {
            continuation?.resume(throwing: LauncherServiceFailure(
                soa_launcher_error_size_mismatch,
                "The downloaded launcher update has an unexpected size"))
            return
        }
        let digest = hasher.finalizeHex().lowercased()
        guard digest == expectedDigest else {
            continuation?.resume(throwing: LauncherServiceFailure(
                soa_launcher_error_digest_mismatch,
                "The launcher update failed SHA-256 verification"))
            return
        }
        continuation?.resume()
    }
}

final class LauncherUpdateService: @unchecked Sendable
{
    private let manifestURLText: String
    private let fallbackManifestURLText: String
    private let publicKeyHex: String
    private let currentVersion: String
    private let platform: String
    private let downloadDirectory: URL
    private let userAgent: String
    private let allowInsecureHTTP: Bool
    private let checkDone: soa_launcher_check_cb
    private let progress: soa_launcher_progress_cb
    private let downloadDone: soa_launcher_download_cb
    private let context: UnsafeMutableRawPointer?
    private let gate = NetworkCallbackGate()
    private let lock = NSLock()
    private var task: Task<Void, Never>?
    private var selection: LauncherUpdateSelection?
    private var catalogue: [String: LauncherUpdateSelection] = [:]
    private var catalogueJSON = "[]"
    private var stopped = false

    init(manifestURL: String,
         fallbackManifestURL: String,
         publicKeyHex: String,
         currentVersion: String,
         platform: String,
         downloadDirectory: String,
         userAgent: String,
         allowInsecureHTTP: Bool,
         checkDone: @escaping soa_launcher_check_cb,
         progress: @escaping soa_launcher_progress_cb,
         downloadDone: @escaping soa_launcher_download_cb,
         context: UnsafeMutableRawPointer?)
    {
        self.manifestURLText = manifestURL
        self.fallbackManifestURLText = fallbackManifestURL
        self.publicKeyHex = publicKeyHex.lowercased()
        self.currentVersion = currentVersion
        self.platform = platform
        self.downloadDirectory = URL(fileURLWithPath: downloadDirectory, isDirectory: true)
        self.userAgent = userAgent
        self.allowInsecureHTTP = allowInsecureHTTP
        self.checkDone = checkDone
        self.progress = progress
        self.downloadDone = downloadDone
        self.context = context
    }

    func shutdown()
    {
        lock.lock()
        stopped = true
        let active = task
        task = nil
        selection = nil
        catalogue.removeAll()
        catalogueJSON = "[]"
        lock.unlock()
        active?.cancel()
        gate.shutdown()
    }

    func cancel()
    {
        lock.lock()
        let active = task
        lock.unlock()
        active?.cancel()
    }

    func selectVersion(_ version: String) -> Bool
    {
        lock.lock()
        defer { lock.unlock() }
        guard !stopped, let selected = catalogue[normalizedVersion(version)] else {
            return false
        }
        selection = selected
        return true
    }

    func check()
    {
        lock.lock()
        if stopped {
            lock.unlock()
            return
        }
        if task != nil {
            lock.unlock()
            reportCheck(result: soa_launcher_check_failed,
                        failure: LauncherServiceFailure(
                            soa_launcher_error_busy,
                            "A launcher update operation is already running"),
                        selection: nil)
            return
        }
        selection = nil
        catalogue.removeAll()
        catalogueJSON = "[]"
        let operation = Task { [weak self] in
            await self?.performCheck()
            self?.clearTask()
        }
        task = operation
        lock.unlock()
    }

    func download()
    {
        lock.lock()
        if stopped {
            lock.unlock()
            return
        }
        guard task == nil else {
            lock.unlock()
            reportDownload(result: soa_launcher_download_failed,
                           failure: LauncherServiceFailure(
                               soa_launcher_error_busy,
                               "A launcher update operation is already running"),
                           finalPath: "")
            return
        }
        guard let selected = selection else {
            lock.unlock()
            reportDownload(result: soa_launcher_download_failed,
                           failure: LauncherServiceFailure(
                               soa_launcher_error_invalid_release,
                               "No launcher update is ready to download"),
                           finalPath: "")
            return
        }
        let operation = Task { [weak self] in
            await self?.performDownload(selected)
            self?.clearTask()
        }
        task = operation
        lock.unlock()
    }

    private func clearTask()
    {
        lock.lock()
        task = nil
        lock.unlock()
    }


    private func storeSelection(_ value: LauncherUpdateSelection)
    {
        lock.lock()
        selection = value
        lock.unlock()
    }

    private func storeCatalogue(_ values: [LauncherUpdateSelection], json: String)
    {
        lock.lock()
        catalogue = Dictionary(uniqueKeysWithValues: values.map { ($0.version, $0) })
        catalogueJSON = json
        lock.unlock()
    }

    private func performCheck() async
    {
        let supportedPlatform = platform == "linux-x86_64"
            || platform == "macos"

        guard !currentVersion.isEmpty,
              supportedPlatform,
              publicKeyHex.range(of: #"^[0-9a-f]{64}$"#, options: .regularExpression) != nil,
              let primaryURL = secureURL(manifestURLText, allowInsecureHTTP: allowInsecureHTTP),
              let fallbackURL = secureURL(fallbackManifestURLText,
                                          allowInsecureHTTP: allowInsecureHTTP),
              Self.trustedReleaseHost(primaryURL.host?.lowercased() ?? ""),
              Self.trustedReleaseHost(fallbackURL.host?.lowercased() ?? "") else {
            reportCheck(result: soa_launcher_check_failed,
                        failure: LauncherServiceFailure(
                            soa_launcher_error_invalid_configuration,
                            "The launcher update configuration is invalid"),
                        selection: nil)
            return
        }

        do {
            var lastError: Error?
            let endpoints = primaryURL == fallbackURL ? [primaryURL] : [primaryURL, fallbackURL]
            for endpoint in endpoints {
                do {
                    let selected = try await loadCatalogue(from: endpoint)
                    guard compareVersions(selected.version, currentVersion) > 0 else {
                        reportCheck(result: soa_launcher_check_no_update,
                                    failure: nil, selection: nil)
                        return
                    }
                    storeSelection(selected)
                    reportCheck(result: soa_launcher_check_update_available,
                                failure: nil, selection: selected)
                    return
                } catch is CancellationError {
                    throw CancellationError()
                } catch {
                    lastError = error
                }
            }
            throw lastError ?? LauncherServiceFailure(
                soa_launcher_error_network, "No launcher update source was available")
        } catch is CancellationError {
            reportCheck(result: soa_launcher_check_cancelled,
                        failure: LauncherServiceFailure(
                            soa_launcher_error_cancelled,
                            "Cancelled"),
                        selection: nil)
        } catch let failure as LauncherServiceFailure {
            reportCheck(result: soa_launcher_check_failed, failure: failure, selection: nil)
        } catch let failure as NetworkFailure {
            let code: soa_launcher_error = failure.message.contains("allowed size")
                ? soa_launcher_error_response_too_large
                : soa_launcher_error_network
            reportCheck(result: soa_launcher_check_failed,
                        failure: LauncherServiceFailure(code, failure.message, status: failure.status),
                        selection: nil)
        } catch {
            reportCheck(result: soa_launcher_check_failed,
                        failure: LauncherServiceFailure(
                            soa_launcher_error_network,
                            error.localizedDescription),
                        selection: nil)
        }
    }

    private func loadCatalogue(from manifestURL: URL) async throws -> LauncherUpdateSelection
    {
        let manifestData = try await fetchSignedJSON(manifestURL, maximumBytes: 64 * 1024)
        let selected = try parseManifest(manifestData)
        guard manifestURL.lastPathComponent == "manifest.json" else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_configuration,
                "The launcher update manifest name is invalid")
        }
        let historyURL = manifestURL.deletingLastPathComponent()
            .appendingPathComponent("versions.json")
        let historyData = try await fetchSignedJSON(historyURL, maximumBytes: 1024 * 1024)
        let parsedCatalogue = try parseCatalogue(historyData)
        guard parsedCatalogue.releases.contains(where: {
            $0.version == selected.version && $0.packageURLs.contains(selected.packageURL)
                && $0.sha256 == selected.sha256 && $0.expectedSize == selected.expectedSize
        }) else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The latest launcher release is missing from signed release history")
        }
        storeCatalogue(parsedCatalogue.releases, json: parsedCatalogue.json)
        return selected
    }

    private func fetchSignedJSON(_ url: URL, maximumBytes: Int) async throws -> Data
    {
        var request = URLRequest(url: url)
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("no-store", forHTTPHeaderField: "Cache-Control")
        let response = try await fetchBounded(
            request,
            timeoutMilliseconds: 12_000,
            maximumBytes: maximumBytes,
            allowInsecureHTTP: allowInsecureHTTP,
            redirectValidator: { source, destination in
                source.scheme?.lowercased() == destination.scheme?.lowercased()
                    && Self.trustedReleaseHost(source.host?.lowercased() ?? "")
                    && Self.trustedReleaseHost(destination.host?.lowercased() ?? "")
            })
        guard (200...299).contains(response.status) else {
            throw LauncherServiceFailure(soa_launcher_error_http,
                                         "HTTP \(response.status)", status: response.status)
        }
        var sealRequest = URLRequest(url: url.appendingPathExtension("seal"))
        sealRequest.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        sealRequest.setValue("text/plain", forHTTPHeaderField: "Accept")
        sealRequest.setValue("no-store", forHTTPHeaderField: "Cache-Control")
        let sealResponse = try await fetchBounded(
            sealRequest,
            timeoutMilliseconds: 12_000,
            maximumBytes: 256,
            allowInsecureHTTP: allowInsecureHTTP,
            redirectValidator: { source, destination in
                source.scheme?.lowercased() == destination.scheme?.lowercased()
                    && Self.trustedReleaseHost(source.host?.lowercased() ?? "")
                    && Self.trustedReleaseHost(destination.host?.lowercased() ?? "")
            })
        guard (200...299).contains(sealResponse.status) else {
            throw LauncherServiceFailure(soa_launcher_error_http,
                                         "HTTP \(sealResponse.status)",
                                         status: sealResponse.status)
        }
        let sealKind = try soaSealDocumentKind(for: url)
        try verifySOASeal(response.data,
                          seal: sealResponse.data,
                          expectedKind: sealKind.name,
                          documentKind: sealKind.abiValue)
        return response.data
    }

    private func parseManifest(_ data: Data) throws -> LauncherUpdateSelection
    {
        guard let manifest = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              (manifest["schema"] as? NSNumber)?.intValue == 1,
              manifest["platform"] as? String == platform else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The signed launcher release manifest is invalid")
        }

        let tag = normalizedVersion(manifest["version"] as? String ?? "")
        guard validVersion(tag) else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The latest launcher release has an invalid version tag")
        }
        let minimum = normalizedVersion(manifest["minimum_version"] as? String ?? "")
        if !minimum.isEmpty && !validVersion(minimum) {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The launcher release has an invalid minimum version")
        }

        let rawName = manifest["file_name"] as? String ?? ""
        let name = safeFileName(rawName)
        let urlText = manifest["url"] as? String ?? ""
        let expectedExtension = platform == "macos" ? ".dmg" : ".appimage"
        guard !name.isEmpty,
              rawName == name,
              name.lowercased().hasSuffix(expectedExtension),
              let url = secureURL(urlText, allowInsecureHTTP: allowInsecureHTTP),
              url.lastPathComponent == name,
              Self.trustedReleaseHost(url.host?.lowercased() ?? "") else {
            throw LauncherServiceFailure(
                soa_launcher_error_unsafe_url,
                "The launcher release contains an invalid package URL")
        }
        var packageURLs = [url]
        if let mirrors = manifest["mirrors"] as? [String] {
            for mirror in mirrors {
                guard let mirrorURL = secureURL(mirror, allowInsecureHTTP: allowInsecureHTTP),
                      mirrorURL.lastPathComponent == name,
                      Self.trustedReleaseHost(mirrorURL.host?.lowercased() ?? "") else {
                    throw LauncherServiceFailure(
                        soa_launcher_error_unsafe_url,
                        "The launcher release contains an invalid mirror URL")
                }
                if !packageURLs.contains(mirrorURL) {
                    packageURLs.append(mirrorURL)
                }
            }
        }

        var digest = (manifest["sha256"] as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .lowercased()
        if digest.hasPrefix("sha256:") { digest.removeFirst(7) }
        guard digest.range(of: #"^[0-9a-f]{64}$"#, options: .regularExpression) != nil else {
            throw LauncherServiceFailure(
                soa_launcher_error_missing_digest,
                "The launcher release package has no valid SHA-256 digest")
        }

        let expectedSize: UInt64
        if let number = manifest["size"] as? NSNumber, number.int64Value > 0 {
            expectedSize = UInt64(number.int64Value)
        } else if let value = manifest["size"] as? String,
                  let parsed = UInt64(value), parsed > 0 {
            expectedSize = parsed
        } else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_size,
                "The launcher release package has no valid size")
        }
        guard expectedSize <= 1024 * 1024 * 1024 else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_size,
                "The launcher release package exceeds the maximum allowed size")
        }

        return LauncherUpdateSelection(
            version: tag,
            minimumVersion: minimum,
            message: manifest["message"] as? String ?? "",
            packageKind: platform == "macos" ? "dmg" : "appimage",
            fileName: name,
            packageURLs: packageURLs,
            sha256: digest,
            expectedSize: expectedSize,
            required: manifest["required"] as? Bool ?? false)
    }

    private func parseCatalogue(_ data: Data) throws
        -> (releases: [LauncherUpdateSelection], json: String)
    {
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              (root["schema"] as? NSNumber)?.intValue == 1,
              root["platform"] as? String == platform,
              let entries = root["releases"] as? [[String: Any]],
              !entries.isEmpty, entries.count <= 3 else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The signed launcher release catalogue is invalid")
        }

        let now = Date()
        let oldestAllowed = now.addingTimeInterval(-365 * 24 * 60 * 60)
        let newestAllowed = now.addingTimeInterval(24 * 60 * 60)
        let formatter = ISO8601DateFormatter()
        var accepted: [(release: LauncherUpdateSelection, entry: [String: Any])] = []
        var versions = Set<String>()

        for entry in entries {
            guard let releasedText = entry["released_at"] as? String,
                  let releasedAt = formatter.date(from: releasedText),
                  releasedAt >= oldestAllowed, releasedAt <= newestAllowed,
                  JSONSerialization.isValidJSONObject(entry) else {
                continue
            }
            let entryData = try JSONSerialization.data(withJSONObject: entry, options: [.sortedKeys])
            let release = try parseManifest(entryData)
            guard versions.insert(release.version).inserted else {
                throw LauncherServiceFailure(
                    soa_launcher_error_invalid_release,
                    "The signed launcher release catalogue contains duplicate versions")
            }
            accepted.append((release, entry))
        }

        guard !accepted.isEmpty else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "No signed launcher release from the last year is available")
        }
        accepted.sort { compareVersions($0.release.version, $1.release.version) > 0 }
        let releases = accepted.map(\.release)
        let filtered: [String: Any] = [
            "schema": 1,
            "platform": platform,
            "releases": accepted.map(\.entry)
        ]
        let filteredData = try JSONSerialization.data(withJSONObject: filtered, options: [.sortedKeys])
        guard let json = String(data: filteredData, encoding: .utf8) else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_release,
                "The launcher release catalogue could not be encoded")
        }
        return (releases, json)
    }

    private func performDownload(_ selected: LauncherUpdateSelection) async
    {
        let finalURL = downloadDirectory.appendingPathComponent(selected.fileName)
        let partialURL = finalURL.appendingPathExtension("part")
        do {
            try FileManager.default.createDirectory(
                at: downloadDirectory,
                withIntermediateDirectories: true)
            var lastError: Error?
            var downloaded = false
            for packageURL in selected.packageURLs {
                do {
                    try? FileManager.default.removeItem(at: partialURL)
                    var request = URLRequest(url: packageURL)
                    request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
                    request.setValue("application/octet-stream", forHTTPHeaderField: "Accept")
                    request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
                    request.setValue("no-store", forHTTPHeaderField: "Cache-Control")

                    let configuration = URLSessionConfiguration.ephemeral
                    configuration.timeoutIntervalForRequest = 30
                    configuration.timeoutIntervalForResource = 60 * 60
                    configuration.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
                    configuration.urlCache = nil
                    let delegate = LauncherAssetDelegate(
                        destination: partialURL,
                        expectedSize: selected.expectedSize,
                        expectedDigest: selected.sha256,
                        progress: { [weak self] received, total in
                            self?.reportProgress(received: received, total: total)
                        })
                    reportProgress(received: 0, total: selected.expectedSize)
                    try await delegate.start(request: request, configuration: configuration)
                    downloaded = true
                    break
                } catch is CancellationError {
                    throw CancellationError()
                } catch {
                    lastError = error
                }
            }
            guard downloaded else {
                throw lastError ?? LauncherServiceFailure(
                    soa_launcher_error_network, "No launcher package mirror was available")
            }
            try? FileManager.default.removeItem(at: finalURL)
            do {
                try FileManager.default.moveItem(at: partialURL, to: finalURL)
            } catch {
                throw LauncherServiceFailure(
                    soa_launcher_error_finalize,
                    error.localizedDescription)
            }
            reportDownload(result: soa_launcher_download_completed,
                           failure: nil,
                           finalPath: finalURL.path)
        } catch is CancellationError {
            try? FileManager.default.removeItem(at: partialURL)
            reportDownload(result: soa_launcher_download_cancelled,
                           failure: LauncherServiceFailure(
                               soa_launcher_error_cancelled,
                               "Cancelled"),
                           finalPath: "")
        } catch let failure as LauncherServiceFailure {
            try? FileManager.default.removeItem(at: partialURL)
            reportDownload(result: soa_launcher_download_failed,
                           failure: failure,
                           finalPath: "")
        } catch {
            try? FileManager.default.removeItem(at: partialURL)
            reportDownload(result: soa_launcher_download_failed,
                           failure: LauncherServiceFailure(
                               soa_launcher_error_network,
                               error.localizedDescription),
                           finalPath: "")
        }
    }

    private func reportCheck(result: soa_launcher_check_result,
                             failure: LauncherServiceFailure?,
                             selection: LauncherUpdateSelection?)
    {
        let callback = checkDone
        let contextAddress = context.map { UInt(bitPattern: $0) } ?? 0
        let empty = ""
        let version = selection?.version ?? empty
        let minimum = selection?.minimumVersion ?? empty
        let message = selection?.message ?? empty
        let kind = selection?.packageKind ?? empty
        let name = selection?.fileName ?? empty
        let url = selection?.packageURL.absoluteString ?? empty
        let digest = selection?.sha256 ?? empty
        let detail = failure?.detail ?? empty
        let expectedSize = selection?.expectedSize ?? 0
        let required = selection?.required ?? false
        let errorCode = failure?.code ?? soa_launcher_error_none
        let status = failure?.status ?? 0
        lock.lock()
        let releases = catalogueJSON
        lock.unlock()

        gate.submit {
            detail.withCString { detailPointer in
                version.withCString { versionPointer in
                    minimum.withCString { minimumPointer in
                        message.withCString { messagePointer in
                            kind.withCString { kindPointer in
                                name.withCString { namePointer in
                                    url.withCString { urlPointer in
                                        digest.withCString { digestPointer in
                                            releases.withCString { releasesPointer in
                                                callback(result, errorCode, Int32(status), detailPointer,
                                                         versionPointer, minimumPointer, messagePointer,
                                                         kindPointer, namePointer, urlPointer, digestPointer,
                                                         expectedSize, required, releasesPointer,
                                                         contextAddress == 0 ? nil : UnsafeMutableRawPointer(bitPattern: contextAddress))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private func reportProgress(received: UInt64, total: UInt64)
    {
        let callback = progress
        let contextAddress = context.map { UInt(bitPattern: $0) } ?? 0
        gate.submit {
            callback(received, total,
                     contextAddress == 0 ? nil : UnsafeMutableRawPointer(bitPattern: contextAddress))
        }
    }

    private func reportDownload(result: soa_launcher_download_result,
                                failure: LauncherServiceFailure?,
                                finalPath: String)
    {
        let callback = downloadDone
        let contextAddress = context.map { UInt(bitPattern: $0) } ?? 0
        let detail = failure?.detail ?? ""
        let errorCode = failure?.code ?? soa_launcher_error_none
        let status = failure?.status ?? 0
        gate.submit {
            detail.withCString { detailPointer in
                finalPath.withCString { pathPointer in
                    callback(result, errorCode, Int32(status), detailPointer, pathPointer,
                             contextAddress == 0 ? nil : UnsafeMutableRawPointer(bitPattern: contextAddress))
                }
            }
        }
    }

    private func soaSealDocumentKind(for url: URL) throws -> (name: String, abiValue: UInt8)
    {
        let name = url.lastPathComponent
        if name == "versions.json" {
            return ("history", 2)
        }
        if name == "manifest.json" {
            return ("manifest", 1)
        }
        throw LauncherServiceFailure(
            soa_launcher_error_invalid_configuration,
            "The launcher update document name is not supported by SOA Seal v1")
    }

    private func verifySOASeal(_ document: Data,
                               seal: Data,
                               expectedKind: String,
                               documentKind: UInt8) throws
    {
        guard var sealText = String(data: seal, encoding: .utf8) else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_signature,
                "The SOA Seal signature is not valid UTF-8")
        }

        sealText = sealText.replacingOccurrences(of: "\r\n", with: "\n")
        if sealText.hasSuffix("\n") {
            sealText.removeLast()
        }
        guard !sealText.contains("\r") else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_signature,
                "The SOA Seal signature has invalid line endings")
        }

        let lines = sealText.split(separator: "\n", omittingEmptySubsequences: false)
        guard lines.count == 4,
              lines[0] == "SOA-SEAL-V1",
              lines[1] == "kind=\(expectedKind)",
              lines[2].hasPrefix("key="),
              lines[3].hasPrefix("sig=") else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_signature,
                "The launcher update is not signed with SOA Seal v1")
        }

        let keyID = String(lines[2].dropFirst(4))
        let signatureText = String(lines[3].dropFirst(4))
        guard keyID.range(
                of: #"^SOA1-(?:[0-9A-F]{4}-){7}[0-9A-F]{4}$"#,
                options: .regularExpression) != nil,
              keyID.utf8.count == 44,
              let signatureBytes = Data(base64Encoded: signatureText),
              signatureBytes.count == 64,
              let publicKey = decodeHex(publicKeyHex), publicKey.count == 32 else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_signature,
                "The SOA Seal signature is malformed")
        }

        #if os(Linux) || canImport(CryptoKit)
        let keyIDCString = keyID.utf8CString
        let valid = keyIDCString.withUnsafeBufferPointer { keyIDBuffer in
            publicKey.withUnsafeBytes { keyBuffer in
                document.withUnsafeBytes { documentBuffer in
                    signatureBytes.withUnsafeBytes { signatureBuffer in
                        soa_verify_soa_seal_v1(
                            keyBuffer.bindMemory(to: UInt8.self).baseAddress,
                            UInt64(publicKey.count),
                            documentKind,
                            documentBuffer.bindMemory(to: UInt8.self).baseAddress,
                            UInt64(document.count),
                            keyIDBuffer.baseAddress,
                            UInt64(keyIDCString.count - 1),
                            signatureBuffer.bindMemory(to: UInt8.self).baseAddress,
                            UInt64(signatureBytes.count))
                    }
                }
            }
        }
        guard valid else {
            throw LauncherServiceFailure(
                soa_launcher_error_invalid_signature,
                "The launcher update does not have a valid Story of Alicia seal")
        }
        #else
        throw LauncherServiceFailure(
            soa_launcher_error_invalid_configuration,
            "SOA Seal verification is unavailable on this platform")
        #endif
    }

    private func decodeHex(_ value: String) -> Data?
    {
        guard value.count % 2 == 0 else { return nil }
        var output = Data()
        output.reserveCapacity(value.count / 2)
        var index = value.startIndex
        while index < value.endIndex {
            let end = value.index(index, offsetBy: 2)
            guard let byte = UInt8(value[index..<end], radix: 16) else { return nil }
            output.append(byte)
            index = end
        }
        return output
    }

    private func normalizedVersion(_ value: String) -> String
    {
        var output = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if output.lowercased().hasPrefix("v") { output.removeFirst() }
        return output
    }

    private func validVersion(_ value: String) -> Bool
    {
        parseVersion(value) != nil
    }

    private func parseVersion(_ value: String) -> ([UInt64], [String])?
    {
        let normalized = normalizedVersion(value)
        let withoutBuild = normalized.split(separator: "+", maxSplits: 1).first.map(String.init) ?? normalized
        let pieces = withoutBuild.split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
        let core = pieces.first.map(String.init) ?? ""
        let components = core.split(separator: ".", omittingEmptySubsequences: false)
        guard !components.isEmpty else { return nil }
        var numbers: [UInt64] = []
        for component in components {
            guard !component.isEmpty,
                  component.allSatisfy({ $0.isNumber }),
                  let number = UInt64(component) else { return nil }
            numbers.append(number)
        }
        let prerelease = pieces.count > 1
            ? pieces[1].split(separator: ".", omittingEmptySubsequences: false).map(String.init)
            : []
        if prerelease.contains(where: { $0.isEmpty }) { return nil }
        return (numbers, prerelease)
    }

    private func compareVersions(_ left: String, _ right: String) -> Int
    {
        guard let lhs = parseVersion(left), let rhs = parseVersion(right) else {
            return left.caseInsensitiveCompare(right) == .orderedAscending ? -1
                : left.caseInsensitiveCompare(right) == .orderedDescending ? 1 : 0
        }
        let count = max(lhs.0.count, rhs.0.count)
        for index in 0..<count {
            let l = index < lhs.0.count ? lhs.0[index] : 0
            let r = index < rhs.0.count ? rhs.0[index] : 0
            if l < r { return -1 }
            if l > r { return 1 }
        }
        if lhs.1.isEmpty && rhs.1.isEmpty { return 0 }
        if lhs.1.isEmpty { return 1 }
        if rhs.1.isEmpty { return -1 }
        let preCount = min(lhs.1.count, rhs.1.count)
        for index in 0..<preCount {
            let l = lhs.1[index]
            let r = rhs.1[index]
            let ln = UInt64(l)
            let rn = UInt64(r)
            if let ln, let rn {
                if ln < rn { return -1 }
                if ln > rn { return 1 }
            } else if ln != nil {
                return -1
            } else if rn != nil {
                return 1
            } else {
                let comparison = l.caseInsensitiveCompare(r)
                if comparison == .orderedAscending { return -1 }
                if comparison == .orderedDescending { return 1 }
            }
        }
        if lhs.1.count < rhs.1.count { return -1 }
        if lhs.1.count > rhs.1.count { return 1 }
        return 0
    }

    private func safeFileName(_ value: String) -> String
    {
        var output = URL(fileURLWithPath: value).lastPathComponent
        output = output.replacingOccurrences(
            of: #"[^A-Za-z0-9._ -]"#,
            with: "_",
            options: .regularExpression)
        while output.hasPrefix(".") { output.removeFirst() }
        return String(output.prefix(180))
    }

    static func trustedReleaseHost(_ host: String) -> Bool
    {
        host == "r2.storyofalicia.com"
            || host == "github.com"
            || host == "objects.githubusercontent.com"
            || host == "release-assets.githubusercontent.com"
            || host.hasSuffix(".githubusercontent.com")
    }
}
