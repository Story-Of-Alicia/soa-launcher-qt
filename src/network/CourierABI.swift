import Foundation
import Soa_Courier

@_cdecl("courier_set_log_callback")
public func courier_set_log_callback(_ callback: courier_log_cb?,
                                     _ ctx: UnsafeMutableRawPointer?)
{
    CourierLogger.shared.set(callback, context: ctx)
}

@_cdecl("courier_create")
public func courier_create(_ cdn: UnsafePointer<CChar>?,
                           _ onProgress: courier_progress_cb?,
                           _ onDone: courier_done_cb?,
                           _ ctx: UnsafeMutableRawPointer?) -> UnsafeMutableRawPointer?
{
    guard let cdn, let onProgress, let onDone else { return nil }
    let base = String(cString: cdn)
    guard let parsed = URL(string: base), parsed.scheme?.lowercased() == "https",
          parsed.host != nil, parsed.user == nil, parsed.password == nil,
          parsed.query == nil, parsed.fragment == nil else {
        log(4, "Rejected unsafe Courier CDN base URL")
        return nil
    }
    let courier = Courier(
        cdnBaseURL: base,
        onProgress: onProgress,
        onDone: onDone,
        ctx: ctx)
    return Unmanaged.passRetained(courier).toOpaque()
}

@_cdecl("courier_destroy")
public func courier_destroy(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    let courier = Unmanaged<Courier>.fromOpaque(pointer).takeUnretainedValue()
    courier.cancel()
    Unmanaged<Courier>.fromOpaque(pointer).release()
}

@_cdecl("courier_integrity_check")
public func courier_integrity_check(_ pointer: UnsafeMutableRawPointer?,
                                    _ installPath: UnsafePointer<CChar>?) -> UInt64
{
    guard let pointer, let installPath else { return 0 }
    let courier = Unmanaged<Courier>.fromOpaque(pointer).takeUnretainedValue()
    return courier.startIntegrityCheck(installPath: String(cString: installPath))
}

@_cdecl("courier_update_check")
public func courier_update_check(_ pointer: UnsafeMutableRawPointer?,
                                 _ installPath: UnsafePointer<CChar>?) -> UInt64
{
    guard let pointer, let installPath else { return 0 }
    let courier = Unmanaged<Courier>.fromOpaque(pointer).takeUnretainedValue()
    return courier.startUpdateCheck(installPath: String(cString: installPath))
}

@_cdecl("courier_update")
public func courier_update(_ pointer: UnsafeMutableRawPointer?,
                           _ installPath: UnsafePointer<CChar>?) -> UInt64
{
    guard let pointer, let installPath else { return 0 }
    let courier = Unmanaged<Courier>.fromOpaque(pointer).takeUnretainedValue()
    return courier.startUpdate(installPath: String(cString: installPath))
}

@_cdecl("courier_cancel")
public func courier_cancel(_ pointer: UnsafeMutableRawPointer?)
{
    guard let pointer else { return }
    let courier = Unmanaged<Courier>.fromOpaque(pointer).takeUnretainedValue()
    courier.cancel()
}
