import Foundation
import Soa_Courier

private let httpSemaphore = DispatchSemaphore(value: 0)
private let updateSemaphore = DispatchSemaphore(value: 0)
private let lock = NSLock()
private var httpResult = soa_http_result_completed
private var httpError = ""
private var updateResult = soa_launcher_check_no_update
private var updateError = soa_launcher_error_none

private func httpDone(_ requestID: UInt64,
                      _ result: soa_http_result,
                      _ status: Int32,
                      _ data: UnsafePointer<UInt8>?,
                      _ dataSize: UInt64,
                      _ finalURL: UnsafePointer<CChar>?,
                      _ error: UnsafePointer<CChar>?,
                      _ elapsedMilliseconds: UInt64,
                      _ context: UnsafeMutableRawPointer?)
{
    lock.lock()
    httpResult = result
    httpError = error.map(String.init(cString:)) ?? ""
    lock.unlock()
    httpSemaphore.signal()
}

private func dnsDone(_ requestID: UInt64,
                     _ result: soa_http_result,
                     _ address: UnsafePointer<CChar>?,
                     _ error: UnsafePointer<CChar>?,
                     _ elapsedMilliseconds: UInt64,
                     _ context: UnsafeMutableRawPointer?)
{
}

private func updateCheckDone(_ result: soa_launcher_check_result,
                             _ errorCode: soa_launcher_error,
                             _ status: Int32,
                             _ errorDetail: UnsafePointer<CChar>?,
                             _ version: UnsafePointer<CChar>?,
                             _ minimumVersion: UnsafePointer<CChar>?,
                             _ message: UnsafePointer<CChar>?,
                             _ packageKind: UnsafePointer<CChar>?,
                             _ packageFileName: UnsafePointer<CChar>?,
                             _ packageURL: UnsafePointer<CChar>?,
                             _ sha256: UnsafePointer<CChar>?,
                             _ expectedSize: UInt64,
                             _ required: Bool,
                             _ releasesJSON: UnsafePointer<CChar>?,
                             _ context: UnsafeMutableRawPointer?)
{
    lock.lock()
    updateResult = result
    updateError = errorCode
    lock.unlock()
    updateSemaphore.signal()
}

private func updateProgress(_ received: UInt64,
                            _ total: UInt64,
                            _ context: UnsafeMutableRawPointer?)
{
}

private func updateDownloadDone(_ result: soa_launcher_download_result,
                                _ errorCode: soa_launcher_error,
                                _ status: Int32,
                                _ errorDetail: UnsafePointer<CChar>?,
                                _ finalPath: UnsafePointer<CChar>?,
                                _ context: UnsafeMutableRawPointer?)
{
}

@main
struct SwiftNetworkABITests
{
    static func main()
    {
        guard let client = soa_http_client_create(httpDone, dnsDone, nil) else {
            fatalError("Could not create the Swift HTTP client")
        }
        let requestID = soa_http_client_request(
            client,
            "file:///etc/passwd",
            soa_http_method_get,
            1000,
            1024,
            "text/plain",
            "soa-test",
            false)
        guard requestID != 0 else {
            fatalError("The Swift HTTP client did not start the validation request")
        }
        guard httpSemaphore.wait(timeout: .now() + 2) == .success else {
            fatalError("The Swift HTTP client did not complete the validation request")
        }
        lock.lock()
        let capturedHTTPResult = httpResult
        let capturedHTTPError = httpError
        lock.unlock()
        guard capturedHTTPResult == soa_http_result_failed,
              capturedHTTPError.contains("Invalid or insecure URL") else {
            fatalError("The Swift HTTP client accepted an unsafe URL")
        }
        soa_http_client_shutdown(client)
        soa_http_client_destroy(client)

        guard let updater = soa_launcher_updater_create(
            "https://example.com/launcher/version",
            "https://example.com/launcher/fallback-version",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "1.0.0",
            "linux-x86_64",
            NSTemporaryDirectory(),
            "soa-test",
            false,
            updateCheckDone,
            updateProgress,
            updateDownloadDone,
            nil) else {
            fatalError("Could not create the Swift launcher updater")
        }
        guard !soa_launcher_updater_select_version(updater, "1.0.0") else {
            fatalError("The Swift launcher updater selected a release before loading a catalogue")
        }
        soa_launcher_updater_check(updater)
        guard updateSemaphore.wait(timeout: .now() + 2) == .success else {
            fatalError("The Swift launcher updater did not reject invalid configuration")
        }
        lock.lock()
        let capturedUpdateResult = updateResult
        let capturedUpdateError = updateError
        lock.unlock()
        guard capturedUpdateResult == soa_launcher_check_failed,
              capturedUpdateError == soa_launcher_error_invalid_configuration else {
            fatalError("The Swift launcher updater returned an unexpected validation result")
        }
        soa_launcher_updater_shutdown(updater)
        soa_launcher_updater_destroy(updater)

        guard let rpc = soa_discord_rpc_create("1", 1, "http://127.0.0.1:18080") else {
            fatalError("Could not create the Swift Discord RPC service")
        }
        soa_discord_rpc_set_launcher_presence(rpc)
        soa_discord_rpc_set_game_presence(rpc, "tester")
        soa_discord_rpc_shutdown(rpc)
        soa_discord_rpc_destroy(rpc)
    }
}
