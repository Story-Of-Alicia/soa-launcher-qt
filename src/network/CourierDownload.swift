import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif


private final class LimitedDataDelegate: NSObject, URLSessionDataDelegate, URLSessionTaskDelegate, @unchecked Sendable
{
    private let maximumBytes: Int
    private let descriptionText: String
    private let allowedScheme: String
    private let allowedHost: String
    private let allowedPort: Int?
    private let queue: OperationQueue
    private let cancellation = URLSessionTaskCancellationGate()
    private var continuation: CheckedContinuation<Data, Error>?
    private var payload = Data()
    private var terminalError: Error?
    private var completed = false

    init(maximumBytes: Int, description: String, origin: URL)
    {
        self.maximumBytes = maximumBytes
        self.descriptionText = description
        self.allowedScheme = origin.scheme?.lowercased() ?? ""
        self.allowedHost = origin.host?.lowercased() ?? ""
        self.allowedPort = origin.port
        self.queue = OperationQueue()
        self.queue.maxConcurrentOperationCount = 1
        self.queue.qualityOfService = .utility
        self.payload.reserveCapacity(min(maximumBytes, 1024 * 1024))
    }

    func start(url: URL, configuration: URLSessionConfiguration) async throws -> Data
    {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { continuation in
                self.continuation = continuation
                let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
                let task = session.dataTask(with: url)
                self.cancellation.start(task)
            }
        }, onCancel: {
            self.cancellation.cancel()
        })
    }


    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void)
    {
        guard let destination = request.url,
              destination.scheme?.lowercased() == allowedScheme,
              destination.host?.lowercased() == allowedHost,
              destination.port == allowedPort else {
            terminalError = Err("Remote redirected \(descriptionText) to an untrusted origin")
            completionHandler(nil)
            return
        }
        completionHandler(request)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void)
    {
        guard let http = response as? HTTPURLResponse else {
            terminalError = Err("Remote returned a non-HTTP response for \(descriptionText)")
            completionHandler(.cancel)
            return
        }
        guard http.statusCode == 200 else {
            terminalError = Err(
                "Remote returned HTTP \(http.statusCode) while retrieving \(descriptionText)",
                retryable: http.statusCode == 408 || http.statusCode == 429 || http.statusCode >= 500)
            completionHandler(.cancel)
            return
        }
        if http.expectedContentLength > Int64(maximumBytes) {
            terminalError = Err("Remote \(descriptionText) exceeds the allowed size")
            completionHandler(.cancel)
            return
        }
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data)
    {
        guard terminalError == nil else { return }
        if payload.count > maximumBytes - data.count {
            terminalError = Err("Remote \(descriptionText) exceeds the allowed size")
            dataTask.cancel()
            return
        }
        payload.append(data)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?)
    {
        guard !completed else { return }
        completed = true
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
                continuation?.resume(throwing: Err(
                    "Failed to retrieve \(descriptionText): \(error.localizedDescription)",
                    retryable: true))
            }
            return
        }
        continuation?.resume(returning: payload)
    }
}

extension Courier
{
    private func fetchData(url: URL, maximumBytes: Int, description: String) async throws -> Data
    {
        var attempt = 0
        while true {
            try Task.checkCancellation()
            attempt += 1
            do {
                guard let origin = URL(string: cdnBaseURL) else {
                    throw Err("Invalid Courier CDN origin")
                }
                let delegate = LimitedDataDelegate(
                    maximumBytes: maximumBytes,
                    description: description,
                    origin: origin)
                return try await delegate.start(url: url, configuration: sessionConfiguration)
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as Err {
                if !error.retryable || attempt >= 3 { throw error }
                log(3, "Retrying \(description) request after transient failure: \(error.message)")
            } catch {
                if attempt >= 3 {
                    throw Err("Failed to retrieve \(description): \(error)")
                }
                log(3, "Retrying \(description) request after transient failure: \(error)")
            }
            let delayNanoseconds = UInt64(1 << (attempt - 1)) * 1_000_000_000
            try await Task.sleep(nanoseconds: delayNanoseconds)
        }
    }

    func fetchRemoteVersion() async throws -> String
    {
        guard let url = URL(string: "\(cdnBaseURL)/version") else {
            throw Err("Invalid remote version URL")
        }
        let data = try await fetchData(url: url, maximumBytes: 4096, description: "game version")
        let version = String(decoding: data, as: UTF8.self)
            .trimmingCharacters(in: .whitespacesAndNewlines)

        guard !version.isEmpty,
              version.count <= 128,
              version.range(of: #"^[A-Za-z0-9._-]+$"#, options: .regularExpression) != nil else {
            throw Err("Remote returned an invalid game version")
        }
        return version
    }

    func fetchManifest(version: String) async throws -> [ValidatedManifestEntry]
    {
        guard let url = URL(string: "\(cdnBaseURL)/\(version)/manifest.json") else {
            throw Err("Invalid remote manifest URL")
        }
        let data = try await fetchData(url: url, maximumBytes: 8 * 1024 * 1024, description: "game manifest")
        do {
            return try validateManifest(JSONDecoder().decode(Manifest.self, from: data))
        } catch let error as Err {
            throw error
        } catch {
            throw Err("Invalid manifest JSON: \(error)")
        }
    }

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
            let chunk = handle.readData(ofLength: 1 << 16)
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

    func tempPath(for destination: URL) -> URL
    {
        destination.appendingPathExtension("download")
    }

    func atomicWriteJSON<T: Encodable>(_ value: T, to url: URL) throws
    {
        let data = try JSONEncoder.pretty.encode(value)
        try data.write(to: url, options: .atomic)
    }

    func writeVersionJSON(installRoot: URL, version: String) throws
    {
        struct VersionFile: Codable { let version: String }
        try atomicWriteJSON(VersionFile(version: version), to: installRoot.appendingPathComponent("version.json"))
    }

    func readLocalVersion(installPath: String) -> String?
    {
        let path = (installPath as NSString).appendingPathComponent("version.json")
        guard let data = FileManager.default.contents(atPath: path),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }
        return (object["version"] as? String) ?? (object["latest"] as? String)
    }

    func readManagedManifest(installRoot: URL) -> ManagedManifest?
    {
        let url = installRoot.appendingPathComponent(".soa-managed-manifest.json")
        guard let data = try? Data(contentsOf: url), data.count <= 16 * 1024 * 1024 else { return nil }
        return try? JSONDecoder().decode(ManagedManifest.self, from: data)
    }
}

private extension JSONEncoder
{
    static var pretty: JSONEncoder
    {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return encoder
    }
}
