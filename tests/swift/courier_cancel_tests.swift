import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import Soa_Courier

func log(_ level: Int32, _ message: String)
{
}

struct Err: Error
{
    let message: String
    let retryable: Bool

    init(_ message: String, retryable: Bool = false)
    {
        self.message = message
        self.retryable = retryable
    }
}

private let completion = DispatchSemaphore(value: 0)
private let resultLock = NSLock()
private var completedOperationID: UInt64 = 0
private var completedResult = courier_result_completed
private var completedMessage = ""

private func progressCallback(_ operationID: UInt64,
                              _ phase: courier_phase,
                              _ message: UnsafePointer<CChar>?,
                              _ percent: Int32,
                              _ received: UInt64,
                              _ total: UInt64,
                              _ throughput: UInt64,
                              _ fileIndex: Int32,
                              _ fileCount: Int32,
                              _ context: UnsafeMutableRawPointer?)
{
}

private func doneCallback(_ operationID: UInt64,
                          _ result: courier_result,
                          _ message: UnsafePointer<CChar>?,
                          _ context: UnsafeMutableRawPointer?)
{
    resultLock.lock()
    completedOperationID = operationID
    completedResult = result
    completedMessage = message.map(String.init(cString:)) ?? ""
    resultLock.unlock()
    completion.signal()
}

@main
struct CourierCancelTests
{
    static func main()
    {
        let courier = Courier(
            cdnBaseURL: "https://example.invalid",
            onProgress: progressCallback,
            onDone: doneCallback,
            ctx: nil)

        _ = courier.run { _ in
            try await Task.sleep(nanoseconds: 30_000_000_000)
        }
        let operationID = courier.run { _ in
            try await Task.sleep(nanoseconds: 30_000_000_000)
        }
        courier.cancel()

        guard completion.wait(timeout: .now() + 2) == .success else {
            fatalError("Cancellation did not produce a terminal callback")
        }

        resultLock.lock()
        let callbackID = completedOperationID
        let callbackResult = completedResult
        let callbackMessage = completedMessage
        resultLock.unlock()

        guard callbackID == operationID else {
            fatalError("Cancellation callback used the wrong operation ID")
        }
        guard callbackResult == courier_result_cancelled else {
            fatalError("Cancellation callback had the wrong typed result")
        }
        guard callbackMessage == "Cancelled." else {
            fatalError("Cancellation callback had an unexpected message: \(callbackMessage)")
        }
    }
}
