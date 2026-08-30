import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if os(Linux)
import Glibc
#else
import Darwin
#endif
import Soa_Courier

final class DNSResolutionRace: @unchecked Sendable
{
    private let lock = NSLock()
    private var continuation: CheckedContinuation<String, Error>?
    private var result: Result<String, Error>?

    func wait() async throws -> String
    {
        try await withCheckedThrowingContinuation { continuation in
            lock.lock()
            if let result {
                lock.unlock()
                continuation.resume(with: result)
                return
            }
            self.continuation = continuation
            lock.unlock()
        }
    }

    func complete(_ result: Result<String, Error>)
    {
        lock.lock()
        if self.result != nil {
            lock.unlock()
            return
        }
        self.result = result
        let continuation = self.continuation
        self.continuation = nil
        lock.unlock()
        continuation?.resume(with: result)
    }
}

final class SwiftHttpClient: @unchecked Sendable
{
    private let httpDone: soa_http_done_cb
    private let dnsDone: soa_dns_done_cb
    private let context: UnsafeMutableRawPointer?
    private let gate = NetworkCallbackGate()
    private let lock = NSLock()
    private var tasks: [UInt64: Task<Void, Never>] = [:]
    private var nextRequestID: UInt64 = 1
    private var stopped = false

    init(httpDone: @escaping soa_http_done_cb,
         dnsDone: @escaping soa_dns_done_cb,
         context: UnsafeMutableRawPointer?)
    {
        self.httpDone = httpDone
        self.dnsDone = dnsDone
        self.context = context
    }

    func shutdown()
    {
        lock.lock()
        stopped = true
        let active = Array(tasks.values)
        tasks.removeAll()
        lock.unlock()
        active.forEach { $0.cancel() }
        gate.shutdown()
    }

    func cancel(_ requestID: UInt64)
    {
        lock.lock()
        let task = tasks.removeValue(forKey: requestID)
        lock.unlock()
        task?.cancel()
    }

    func cancelAll()
    {
        lock.lock()
        let active = Array(tasks.values)
        tasks.removeAll()
        lock.unlock()
        active.forEach { $0.cancel() }
    }

    private func begin(_ body: @escaping @Sendable (UInt64) async -> Void) -> UInt64
    {
        lock.lock()
        if stopped {
            lock.unlock()
            return 0
        }
        let requestID = nextRequestID
        nextRequestID = nextRequestID == UInt64.max ? 1 : nextRequestID + 1
        let task = Task { [weak self] in
            await body(requestID)
            self?.finish(requestID)
        }
        tasks[requestID] = task
        lock.unlock()
        return requestID
    }

    private func finish(_ requestID: UInt64)
    {
        lock.lock()
        tasks.removeValue(forKey: requestID)
        lock.unlock()
    }

    func request(urlText: String,
                 method: soa_http_method,
                 timeoutMilliseconds: UInt32,
                 maximumBytes: UInt64,
                 accept: String,
                 userAgent: String,
                 allowInsecureHTTP: Bool) -> UInt64
    {
        begin { [weak self] requestID in
            guard let self else { return }
            let started = DispatchTime.now().uptimeNanoseconds
            guard let url = secureURL(urlText, allowInsecureHTTP: allowInsecureHTTP) else {
                self.reportHTTP(requestID, result: soa_http_result_failed, status: 0,
                                data: Data(), finalURL: "", error: "Invalid or insecure URL",
                                elapsed: self.elapsedMilliseconds(started))
                return
            }
            var request = URLRequest(url: url)
            request.httpMethod = method == soa_http_method_head ? "HEAD" : "GET"
            request.setValue("no-store", forHTTPHeaderField: "Cache-Control")
            request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
            if !accept.isEmpty { request.setValue(accept, forHTTPHeaderField: "Accept") }
            if !userAgent.isEmpty { request.setValue(userAgent, forHTTPHeaderField: "User-Agent") }

            do {
                let response = try await fetchBounded(
                    request,
                    timeoutMilliseconds: max(1000, timeoutMilliseconds),
                    maximumBytes: Int(min(maximumBytes, UInt64(Int.max))),
                    allowInsecureHTTP: allowInsecureHTTP,
                    redirectValidator: { source, destination in
                        unrestrictedSafeRedirect(source: source, destination: destination)
                    })
                self.reportHTTP(requestID, result: soa_http_result_completed,
                                status: response.status, data: response.data,
                                finalURL: response.finalURL.absoluteString, error: "",
                                elapsed: self.elapsedMilliseconds(started))
            } catch is CancellationError {
                self.reportHTTP(requestID, result: soa_http_result_cancelled, status: 0,
                                data: Data(), finalURL: "", error: "Cancelled",
                                elapsed: self.elapsedMilliseconds(started))
            } catch let error as NetworkFailure {
                self.reportHTTP(requestID, result: soa_http_result_failed, status: error.status,
                                data: Data(), finalURL: "", error: error.message,
                                elapsed: self.elapsedMilliseconds(started))
            } catch {
                self.reportHTTP(requestID, result: soa_http_result_failed, status: 0,
                                data: Data(), finalURL: "", error: error.localizedDescription,
                                elapsed: self.elapsedMilliseconds(started))
            }
        }
    }

    func resolve(hostname: String, timeoutMilliseconds: UInt32) -> UInt64
    {
        begin { [weak self] requestID in
            guard let self else { return }
            let started = DispatchTime.now().uptimeNanoseconds
            let race = DNSResolutionRace()
            let resolver = Task.detached(priority: .utility) {
                do {
                    race.complete(.success(try Self.resolveAddress(hostname)))
                } catch {
                    race.complete(.failure(error))
                }
            }
            let timeout = Task.detached(priority: .utility) {
                do {
                    let timeoutNanoseconds = UInt64(max(1_000, timeoutMilliseconds)) * 1_000_000
                    try await Task.sleep(nanoseconds: timeoutNanoseconds)
                    race.complete(.failure(NetworkFailure("DNS lookup timed out")))
                } catch {
                }
            }

            do {
                let result = try await withTaskCancellationHandler(operation: {
                    try await race.wait()
                }, onCancel: {
                    race.complete(.failure(CancellationError()))
                })
                timeout.cancel()
                self.reportDNS(requestID, result: soa_http_result_completed,
                               address: result, error: "",
                               elapsed: self.elapsedMilliseconds(started))
            } catch is CancellationError {
                timeout.cancel()
                resolver.cancel()
                self.reportDNS(requestID, result: soa_http_result_cancelled,
                               address: "", error: "Cancelled",
                               elapsed: self.elapsedMilliseconds(started))
            } catch let error as NetworkFailure {
                timeout.cancel()
                resolver.cancel()
                self.reportDNS(requestID, result: soa_http_result_failed,
                               address: "", error: error.message,
                               elapsed: self.elapsedMilliseconds(started))
            } catch {
                timeout.cancel()
                resolver.cancel()
                self.reportDNS(requestID, result: soa_http_result_failed,
                               address: "", error: error.localizedDescription,
                               elapsed: self.elapsedMilliseconds(started))
            }
        }
    }

    private func reportHTTP(_ requestID: UInt64,
                            result: soa_http_result,
                            status: Int,
                            data: Data,
                            finalURL: String,
                            error: String,
                            elapsed: UInt64)
    {
        let callback = httpDone
        let contextAddress = context.map { UInt(bitPattern: $0) } ?? 0
        gate.submit {
            finalURL.withCString { finalPointer in
                error.withCString { errorPointer in
                    data.withUnsafeBytes { bytes in
                        callback(requestID, result, Int32(status),
                                 bytes.bindMemory(to: UInt8.self).baseAddress,
                                 UInt64(data.count), finalPointer, errorPointer,
                                 elapsed,
                                 contextAddress == 0 ? nil : UnsafeMutableRawPointer(bitPattern: contextAddress))
                    }
                }
            }
        }
    }

    private func reportDNS(_ requestID: UInt64,
                           result: soa_http_result,
                           address: String,
                           error: String,
                           elapsed: UInt64)
    {
        let callback = dnsDone
        let contextAddress = context.map { UInt(bitPattern: $0) } ?? 0
        gate.submit {
            address.withCString { addressPointer in
                error.withCString { errorPointer in
                    callback(requestID, result, addressPointer, errorPointer, elapsed,
                             contextAddress == 0 ? nil : UnsafeMutableRawPointer(bitPattern: contextAddress))
                }
            }
        }
    }

    private func elapsedMilliseconds(_ started: UInt64) -> UInt64
    {
        (DispatchTime.now().uptimeNanoseconds - started) / 1_000_000
    }

    private static func resolveAddress(_ hostname: String) throws -> String
    {
        guard !hostname.isEmpty && hostname.utf8.count <= 253 else {
            throw NetworkFailure("Invalid DNS hostname")
        }

#if os(Linux)
        let streamType = Int32(SOCK_STREAM.rawValue)
#else
        let streamType = SOCK_STREAM
#endif

        var hints = addrinfo()
        hints.ai_flags = AI_ADDRCONFIG
        hints.ai_family = AF_UNSPEC
        hints.ai_socktype = streamType
        hints.ai_protocol = 0
        var result: UnsafeMutablePointer<addrinfo>?
        let code = getaddrinfo(hostname, nil, &hints, &result)
        guard code == 0, let first = result else {
            throw NetworkFailure(String(cString: gai_strerror(code)))
        }
        defer { freeaddrinfo(first) }

        var ipv6Fallback: String?
        var current: UnsafeMutablePointer<addrinfo>? = first
        while let entry = current {
            var buffer = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            let status = getnameinfo(entry.pointee.ai_addr,
                                     entry.pointee.ai_addrlen,
                                     &buffer,
                                     socklen_t(buffer.count),
                                     nil,
                                     0,
                                     NI_NUMERICHOST)
            if status == 0 {
                let address = String(cString: buffer)
                if entry.pointee.ai_family == AF_INET {
                    return address
                }
                if ipv6Fallback == nil {
                    ipv6Fallback = address
                }
            }
            current = entry.pointee.ai_next
        }
        if let ipv6Fallback {
            return ipv6Fallback
        }
        throw NetworkFailure("DNS lookup returned no usable address")
    }
}
