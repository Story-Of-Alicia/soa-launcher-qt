import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

struct HTTPResponseValue
{
    let status: Int
    let data: Data
    let finalURL: URL
}

struct NetworkFailure: Error
{
    let message: String
    let status: Int

    init(_ message: String, status: Int = 0)
    {
        self.message = message
        self.status = status
    }
}

final class NetworkCallbackGate: @unchecked Sendable
{
    private let queue = DispatchQueue(label: "com.storyofalicia.network.callbacks")
    private var active = true

    func submit(_ body: @escaping @Sendable () -> Void)
    {
        queue.async {
            guard self.active else { return }
            body()
        }
    }

    func shutdown()
    {
        queue.sync {
            active = false
        }
    }
}

final class URLSessionTaskCancellationGate: @unchecked Sendable
{
    private let lock = NSLock()
    private var task: URLSessionTask?
    private var cancellationRequested = false

    func start(_ task: URLSessionTask)
    {
        lock.lock()
        self.task = task
        let shouldCancel = cancellationRequested
        task.resume()
        lock.unlock()
        if shouldCancel { task.cancel() }
    }

    func cancel()
    {
        lock.lock()
        cancellationRequested = true
        let activeTask = task
        lock.unlock()
        activeTask?.cancel()
    }

    func clear()
    {
        lock.lock()
        task = nil
        lock.unlock()
    }
}

final class BoundedHTTPDelegate: NSObject, URLSessionDataDelegate, URLSessionTaskDelegate, @unchecked Sendable
{
    private let maximumBytes: Int
    private let allowInsecureHTTP: Bool
    private let redirectValidator: @Sendable (URL, URL) -> Bool
    private let queue: OperationQueue
    private let cancellation = URLSessionTaskCancellationGate()
    private var continuation: CheckedContinuation<HTTPResponseValue, Error>?
    private var response: HTTPURLResponse?
    private var payload = Data()
    private var terminalError: Error?
    private var completed = false

    init(maximumBytes: Int,
         allowInsecureHTTP: Bool,
         redirectValidator: @escaping @Sendable (URL, URL) -> Bool)
    {
        self.maximumBytes = max(0, maximumBytes)
        self.allowInsecureHTTP = allowInsecureHTTP
        self.redirectValidator = redirectValidator
        self.queue = OperationQueue()
        self.queue.maxConcurrentOperationCount = 1
        self.queue.qualityOfService = .utility
    }

    func start(request: URLRequest, configuration: URLSessionConfiguration) async throws -> HTTPResponseValue
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
        guard let source = response.url, let destination = request.url else {
            terminalError = NetworkFailure("The server returned an invalid redirect")
            completionHandler(nil)
            return
        }
        let scheme = destination.scheme?.lowercased() ?? ""
        let safeScheme = scheme == "https" || (allowInsecureHTTP && scheme == "http")
        guard safeScheme && redirectValidator(source, destination) else {
            terminalError = NetworkFailure("The server redirected to an untrusted URL")
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
        guard let http = response as? HTTPURLResponse, let url = http.url else {
            terminalError = NetworkFailure("The server returned a non-HTTP response")
            completionHandler(.cancel)
            return
        }
        let scheme = url.scheme?.lowercased() ?? ""
        guard scheme == "https" || (allowInsecureHTTP && scheme == "http") else {
            terminalError = NetworkFailure("The server returned an insecure URL")
            completionHandler(.cancel)
            return
        }
        if http.expectedContentLength > Int64(maximumBytes) && maximumBytes > 0 {
            terminalError = NetworkFailure("The response exceeds the allowed size", status: http.statusCode)
            completionHandler(.cancel)
            return
        }
        self.response = http
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data)
    {
        guard terminalError == nil else { return }
        if maximumBytes > 0 && payload.count > maximumBytes - data.count {
            terminalError = NetworkFailure("The response exceeds the allowed size", status: response?.statusCode ?? 0)
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
                continuation?.resume(throwing: NetworkFailure(error.localizedDescription, status: response?.statusCode ?? 0))
            }
            return
        }
        guard let response, let finalURL = response.url else {
            continuation?.resume(throwing: NetworkFailure("The server returned no response"))
            return
        }
        continuation?.resume(returning: HTTPResponseValue(
            status: response.statusCode,
            data: payload,
            finalURL: finalURL))
    }
}

func secureURL(_ value: String, allowInsecureHTTP: Bool) -> URL?
{
    guard let url = URL(string: value),
          url.host != nil,
          url.user == nil,
          url.password == nil else {
        return nil
    }
    let scheme = url.scheme?.lowercased()
    if scheme == "https" { return url }
    if allowInsecureHTTP && scheme == "http" { return url }
    return nil
}

func ephemeralConfiguration(timeoutMilliseconds: UInt32) -> URLSessionConfiguration
{
    let configuration = URLSessionConfiguration.ephemeral
    let timeout = max(1.0, Double(timeoutMilliseconds) / 1000.0)
    configuration.timeoutIntervalForRequest = timeout
    configuration.timeoutIntervalForResource = timeout
    configuration.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
    configuration.urlCache = nil
    configuration.httpMaximumConnectionsPerHost = 2
    return configuration
}

func fetchBounded(_ request: URLRequest,
                  timeoutMilliseconds: UInt32,
                  maximumBytes: Int,
                  allowInsecureHTTP: Bool,
                  redirectValidator: @escaping @Sendable (URL, URL) -> Bool) async throws -> HTTPResponseValue
{
    let delegate = BoundedHTTPDelegate(
        maximumBytes: maximumBytes,
        allowInsecureHTTP: allowInsecureHTTP,
        redirectValidator: redirectValidator)
    return try await delegate.start(
        request: request,
        configuration: ephemeralConfiguration(timeoutMilliseconds: timeoutMilliseconds))
}

func unrestrictedSafeRedirect(source: URL, destination: URL) -> Bool
{
    let sourceScheme = source.scheme?.lowercased() ?? ""
    let destinationScheme = destination.scheme?.lowercased() ?? ""
    if sourceScheme == "https" && destinationScheme != "https" { return false }
    return true
}

func cStringValue(_ pointer: UnsafePointer<CChar>?) -> String
{
    guard let pointer else { return "" }
    return String(cString: pointer)
}
