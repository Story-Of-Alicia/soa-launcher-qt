import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import Soa_Courier

final class Courier
{
    let cdnBaseURL: String
    let onProgress: courier_progress_cb
    let onDone: courier_done_cb
    let ctx: UnsafeMutableRawPointer?
    let sessionConfiguration: URLSessionConfiguration

    private var task: Task<Void, Never>?
    private let generationLock = NSLock()
    private let instanceID: UInt64 = UInt64.random(in: 1...UInt64(UInt32.max))
    private var generation: UInt64 = 0
    private var currentOperationID: UInt64 = 0

    init(cdnBaseURL: String,
         onProgress: @escaping courier_progress_cb,
         onDone: @escaping courier_done_cb,
         ctx: UnsafeMutableRawPointer?)
    {
        self.cdnBaseURL = cdnBaseURL.hasSuffix("/")
            ? String(cdnBaseURL.dropLast())
            : cdnBaseURL
        self.onProgress = onProgress
        self.onDone = onDone
        self.ctx = ctx

        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 30
        config.timeoutIntervalForResource = 60 * 60
        config.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        config.httpMaximumConnectionsPerHost = 2
        self.sessionConfiguration = config
    }

    private func isCurrent(_ operationID: UInt64) -> Bool
    {
        generationLock.lock()
        defer { generationLock.unlock() }
        return currentOperationID == operationID
    }

    func reportProgress(_ operationID: UInt64,
                        _ phase: courier_phase,
                        _ message: String,
                        _ percent: Int,
                        _ received: UInt64,
                        _ total: UInt64,
                        _ throughput: UInt64,
                        _ fileIndex: Int,
                        _ fileCount: Int)
    {
        guard isCurrent(operationID) else { return }
        message.withCString { c in
            onProgress(operationID, phase, c, Int32(percent), received, total, throughput,
                       Int32(fileIndex), Int32(fileCount), ctx)
        }
    }

    func reportDone(_ operationID: UInt64, _ result: courier_result, _ message: String)
    {
        generationLock.lock()
        let current = currentOperationID == operationID
        if current
        {
            currentOperationID = 0
            task = nil
        }
        generationLock.unlock()

        guard current else { return }
        message.withCString { c in onDone(operationID, result, c, ctx) }
    }

    func cancel()
    {
        generationLock.lock()
        let operationID = currentOperationID
        currentOperationID = 0
        let activeTask = task
        task = nil
        generationLock.unlock()

        activeTask?.cancel()
        guard operationID != 0 else { return }
        "Cancelled.".withCString { c in onDone(operationID, courier_result_cancelled, c, ctx) }
    }

    @discardableResult
    func run(_ body: @escaping (UInt64) async throws -> Void) -> UInt64
    {



        generationLock.lock()
        let previousTask = task
        generation = (generation &+ 1) & 0xffff_ffff
        if generation == 0 { generation = 1 }
        let operationID = (instanceID << 32) | generation
        currentOperationID = operationID
        let newTask = Task { [self] in
            do {
                try await body(operationID)
            } catch is CancellationError {
                reportDone(operationID, courier_result_cancelled, "Cancelled.")
            } catch let error as Err {
                log(4, error.message)
                reportDone(operationID, courier_result_failed, error.message)
            } catch {
                log(4, "\(error)")
                reportDone(operationID, courier_result_failed, "\(error)")
            }
        }
        task = newTask
        generationLock.unlock()
        previousTask?.cancel()
        return operationID
    }
}
