import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

private func parsedContentRangeStart(_ value: String, expectedTotal: Int) -> Int64?
{
    let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
    guard trimmed.lowercased().hasPrefix("bytes ") else { return nil }
    let payload = trimmed.dropFirst(6)
    let halves = payload.split(separator: "/", maxSplits: 1, omittingEmptySubsequences: false)
    guard halves.count == 2 else { return nil }
    let bounds = halves[0].split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
    guard bounds.count == 2, let start = Int64(bounds[0]), let end = Int64(bounds[1]), end >= start else {
        return nil
    }
    if halves[1] != "*" {
        guard let total = Int(halves[1]), total == expectedTotal else { return nil }
    }
    return start
}

private final class StreamingDownloadDelegate: NSObject, URLSessionDataDelegate, URLSessionTaskDelegate, @unchecked Sendable
{
    private let destination: URL
    private let expectedSize: Int
    private let startingOffset: Int64
    private let allowedScheme: String
    private let allowedHost: String
    private let allowedPort: Int?
    private let onBytes: (Int, UInt64) -> Void
    private let queue: OperationQueue

    private let cancellation = URLSessionTaskCancellationGate()
    private var handle: FileHandle?
    private var continuation: CheckedContinuation<Void, Error>?
    private var terminalError: Error?
    private var received: UInt64
    private var resumed = false
    private var completed = false

    init(destination: URL, expectedSize: Int, startingOffset: Int64,
         origin: URL,
         onBytes: @escaping (Int, UInt64) -> Void)
    {
        self.destination = destination
        self.expectedSize = expectedSize
        self.startingOffset = startingOffset
        self.allowedScheme = origin.scheme?.lowercased() ?? ""
        self.allowedHost = origin.host?.lowercased() ?? ""
        self.allowedPort = origin.port
        self.onBytes = onBytes
        self.received = UInt64(max(0, startingOffset))
        self.queue = OperationQueue()
        self.queue.maxConcurrentOperationCount = 1
        self.queue.qualityOfService = .utility
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
            terminalError = Err(
                "Download redirected to an untrusted origin")
            completionHandler(nil)
            return
        }
        completionHandler(request)
    }

    func start(request: URLRequest, configuration: URLSessionConfiguration) async throws
    {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                self.continuation = continuation
                let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
                let task = session.dataTask(with: request)
                self.cancellation.start(task)
            }
        }, onCancel: {
            self.cancellation.cancel()
        })
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void)
    {
        guard let http = response as? HTTPURLResponse else {
            terminalError = Err("Download returned a non-HTTP response")
            completionHandler(.cancel)
            return
        }

        let status = http.statusCode
        if status == 206 && startingOffset > 0 {
            let contentRange = http.value(forHTTPHeaderField: "Content-Range") ?? ""
            guard parsedContentRangeStart(contentRange, expectedTotal: expectedSize) == startingOffset else {
                try? FileManager.default.removeItem(at: destination)
                terminalError = Err("Server returned an invalid resume range; retrying the file from the beginning", retryable: true)
                completionHandler(.cancel)
                return
            }
            resumed = true
        } else if status == 416 && startingOffset > 0 {
            try? FileManager.default.removeItem(at: destination)
            terminalError = Err("Server rejected the saved partial file; retrying from the beginning", retryable: true)
            completionHandler(.cancel)
            return
        } else if (200...299).contains(status) {


            resumed = false
            received = 0
            try? FileManager.default.removeItem(at: destination)
        } else {
            terminalError = Err(
                "HTTP \(status) while downloading \(dataTask.originalRequest?.url?.lastPathComponent ?? "file")",
                retryable: status == 408 || status == 429 || status >= 500)
            completionHandler(.cancel)
            return
        }

        do {
            let parent = destination.deletingLastPathComponent()
            try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true)
            if !FileManager.default.fileExists(atPath: destination.path) {
                _ = FileManager.default.createFile(atPath: destination.path, contents: nil)
            }
            handle = try FileHandle(forWritingTo: destination)
            if resumed {
                try handle?.seekToEnd()
            } else {
                try handle?.truncate(atOffset: 0)
            }
            completionHandler(.allow)
        } catch {
            terminalError = Err("Could not open temporary download file: \(error)")
            completionHandler(.cancel)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data)
    {
        guard terminalError == nil else { return }
        do {
            try handle?.write(contentsOf: data)
            received += UInt64(data.count)
            onBytes(data.count, received)
        } catch {
            terminalError = Err("Could not write temporary download file: \(error)")
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
                continuation?.resume(throwing: Err("Network download failed: \(error.localizedDescription)", retryable: true))
            }
            return
        }

        if expectedSize >= 0 && received != UInt64(expectedSize) {
            continuation?.resume(throwing: Err(
                "Downloaded size mismatch: expected \(expectedSize) bytes, received \(received)", retryable: true))
            return
        }
        continuation?.resume()
    }
}

extension Courier
{
    func streamingDownload(from url: URL, to destination: URL, expectedSize: Int,
                           onBytes: @escaping (Int, UInt64) -> Void) async throws
    {
        var attempt = 0
        while true {
            try Task.checkCancellation()
            attempt += 1

            let existing = (try? FileManager.default.attributesOfItem(atPath: destination.path)[.size] as? NSNumber)?.int64Value ?? 0
            var request = URLRequest(url: url)
            request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
            if existing > 0 && existing < expectedSize {
                request.setValue("bytes=\(existing)-", forHTTPHeaderField: "Range")
            } else if existing >= expectedSize {
                try? FileManager.default.removeItem(at: destination)
            }

            let delegate = StreamingDownloadDelegate(
                destination: destination,
                expectedSize: expectedSize,
                startingOffset: existing > 0 && existing < expectedSize ? existing : 0,
                origin: url,
                onBytes: onBytes)

            do {
                try await delegate.start(request: request, configuration: sessionConfiguration)
                return
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as Err {
                if !error.retryable || attempt >= 3 { throw error }
                log(3, "Retrying download after transient failure (attempt \(attempt)): \(error.message)")
            } catch {
                if attempt >= 3 { throw Err("Download failed: \(error)") }
                log(3, "Retrying download after transient failure (attempt \(attempt)): \(error)")
            }

            let delayNanoseconds = UInt64(1 << (attempt - 1)) * 1_000_000_000
            try await Task.sleep(nanoseconds: delayNanoseconds)
        }
    }
}
