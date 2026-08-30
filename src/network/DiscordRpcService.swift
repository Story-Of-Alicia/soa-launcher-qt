import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if os(Linux)
import Glibc
#else
import Darwin
#endif

struct DiscordPresenceValue: Equatable
{
    var details: String
    var state: String
    var startedAt: Int64
    var largeImageKey: String
    var largeImageText: String
    var smallImageKey: String
    var smallImageText: String
}

final class DiscordRpcService: @unchecked Sendable
{
    private enum Mode
    {
        case launcher
        case game
    }

    private let applicationID: String
    private let processID: Int64
    private let proxyBaseURL: String
    private let queue = DispatchQueue(label: "com.storyofalicia.discord-rpc")
    private var mode = Mode.launcher
    private var username = ""
    private var modeGeneration: UInt64 = 0
    private var desiredPayload = Data()
    private var pendingPayload = Data()
    private var lastSentPayload = Data()
    private var activityIdentity = Data()
    private var activityStartedAt: Int64 = 0
    private var lastSentAt: UInt64 = 0
    private var lastRpcLogAt: UInt64 = 0
    private var lastProxyLogAt: UInt64 = 0
    private var socketFD: Int32 = -1
    private var readSource: DispatchSourceRead?
    private var socketBuffer = Data()
    private var ready = false
    private var reconnectScheduled = false
    private var flushScheduled = false
    private var proxyTask: Task<Void, Never>?
    private var stopped = false

    init(applicationID: String, processID: Int64, proxyBaseURL: String)
    {
        self.applicationID = applicationID
        self.processID = processID
        self.proxyBaseURL = proxyBaseURL
    }

    func setLauncherPresence()
    {
        queue.async {
            guard !self.stopped else { return }
            self.mode = .launcher
            self.modeGeneration &+= 1
            self.username = ""
            self.stopProxyPolling()
            self.queuePresence(DiscordPresenceValue(
                details: "In Launcher",
                state: "",
                startedAt: 0,
                largeImageKey: "",
                largeImageText: "",
                smallImageKey: "",
                smallImageText: ""))
        }
    }

    func setGamePresence(username: String)
    {
        queue.async {
            guard !self.stopped else { return }
            self.mode = .game
            self.modeGeneration &+= 1
            self.username = username.trimmingCharacters(in: .whitespacesAndNewlines)
            self.stopProxyPolling()
            self.queuePresence(self.defaultGamePresence())
            self.startProxyPolling()
        }
    }

    func shutdown()
    {
        queue.sync {
            guard !stopped else { return }
            stopped = true
            stopProxyPolling()
            if ready && socketFD >= 0 {
                _ = sendActivity(nil)
            }
            closeSocket()
            desiredPayload.removeAll()
            pendingPayload.removeAll()
            lastSentPayload.removeAll()
        }
    }

    private func queuePresence(_ presence: DiscordPresenceValue)
    {
        guard let activity = makeActivity(presence),
              let serialized = try? JSONSerialization.data(withJSONObject: activity, options: [.sortedKeys]) else {
            return
        }
        desiredPayload = serialized
        if serialized == lastSentPayload || serialized == pendingPayload { return }
        pendingPayload = serialized
        flushPresence()
    }

    private func makeActivity(_ presence: DiscordPresenceValue) -> [String: Any]?
    {
        let details = bounded(presence.details, limit: 128)
        guard !details.isEmpty else { return nil }
        var identity: [String: Any] = ["details": details]
        let state = bounded(presence.state, limit: 128)
        let largeImage = bounded(presence.largeImageKey, limit: 256)
        let largeText = bounded(presence.largeImageText, limit: 128)
        let smallImage = bounded(presence.smallImageKey, limit: 256)
        let smallText = bounded(presence.smallImageText, limit: 128)
        if !state.isEmpty { identity["state"] = state }
        if !largeImage.isEmpty { identity["large_image"] = largeImage }
        if !largeText.isEmpty { identity["large_text"] = largeText }
        if !smallImage.isEmpty { identity["small_image"] = smallImage }
        if !smallText.isEmpty { identity["small_text"] = smallText }

        guard let identityData = try? JSONSerialization.data(withJSONObject: identity, options: [.sortedKeys]) else {
            return nil
        }
        if presence.startedAt > 0 {
            activityIdentity = identityData
            activityStartedAt = presence.startedAt
        } else if identityData != activityIdentity || activityStartedAt <= 0 {
            activityIdentity = identityData
            activityStartedAt = Int64(Date().timeIntervalSince1970)
        }

        var activity: [String: Any] = [
            "type": 0,
            "details": details,
            "timestamps": ["start": activityStartedAt]
        ]
        if !state.isEmpty { activity["state"] = state }
        var assets: [String: String] = [:]
        if !largeImage.isEmpty { assets["large_image"] = largeImage }
        if !largeText.isEmpty { assets["large_text"] = largeText }
        if !smallImage.isEmpty { assets["small_image"] = smallImage }
        if !smallText.isEmpty { assets["small_text"] = smallText }
        if !assets.isEmpty { activity["assets"] = assets }
        return activity
    }

    private func flushPresence()
    {
        guard !stopped, !pendingPayload.isEmpty else { return }
        let now = monotonicMilliseconds()
        if lastSentAt > 0 && now - lastSentAt < 10_000 {
            scheduleFlush(after: 10_000 - (now - lastSentAt))
            return
        }
        guard ready && socketFD >= 0 else {
            connectToDiscord()
            return
        }
        guard let activity = try? JSONSerialization.jsonObject(with: pendingPayload) else { return }
        guard sendActivity(activity) else {
            disconnectAndRetry("Discord RPC write failed; retrying.")
            return
        }
        lastSentAt = now
        lastSentPayload = pendingPayload
        pendingPayload.removeAll()
    }

    private func scheduleFlush(after milliseconds: UInt64)
    {
        guard !flushScheduled else { return }
        flushScheduled = true
        queue.asyncAfter(deadline: .now() + .milliseconds(Int(milliseconds))) {
            self.flushScheduled = false
            self.flushPresence()
        }
    }

    private func connectToDiscord()
    {
        guard !stopped, socketFD < 0 else { return }
        for candidate in socketCandidates() {
            let fd = Self.connectUnixSocket(candidate)
            if fd < 0 { continue }
            socketFD = fd
            socketBuffer.removeAll(keepingCapacity: true)
            ready = false
            installReadSource(fd)
            let handshake: [String: Any] = ["v": 1, "client_id": applicationID]
            guard let data = try? JSONSerialization.data(withJSONObject: handshake),
                  sendPacket(opcode: 0, payload: data) else {
                closeSocket()
                continue
            }
            return
        }
        scheduleReconnect()
    }

    private func installReadSource(_ fd: Int32)
    {
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler {
            self.readSocket()
        }
        source.setCancelHandler {}
        readSource = source
        source.resume()
    }

    private func readSocket()
    {
        guard socketFD >= 0 else { return }
        var bytes = [UInt8](repeating: 0, count: 64 * 1024)
        while true {
            let count = recv(socketFD, &bytes, bytes.count, 0)
            if count > 0 {
                socketBuffer.append(bytes, count: count)
                parseFrames()
                continue
            }
            if count == 0 {
                disconnectAndRetry("Discord RPC disconnected; retrying.")
                return
            }
            if errno == EAGAIN || errno == EWOULDBLOCK { return }
            if errno == EINTR { continue }
            disconnectAndRetry("Discord RPC read failed; retrying.")
            return
        }
    }

    private func parseFrames()
    {
        while socketBuffer.count >= 8 {
            let opcode = readUInt32(socketBuffer, offset: 0)
            let length = readUInt32(socketBuffer, offset: 4)
            if length > 1024 * 1024 {
                logRpcWarning("Discord RPC sent an oversized frame.")
                disconnectAndRetry("")
                return
            }
            let frameSize = 8 + Int(length)
            if socketBuffer.count < frameSize { return }
            let payload = socketBuffer.subdata(in: 8..<frameSize)
            socketBuffer.removeSubrange(0..<frameSize)
            handleFrame(opcode: opcode, payload: payload)
        }
    }

    private func handleFrame(opcode: UInt32, payload: Data)
    {
        if opcode == 3 {
            _ = sendPacket(opcode: 4, payload: payload)
            return
        }
        if opcode == 2 {
            disconnectAndRetry("")
            return
        }
        guard opcode == 1,
              let object = try? JSONSerialization.jsonObject(with: payload) as? [String: Any] else {
            return
        }
        let event = object["evt"] as? String ?? ""
        if event == "READY" {
            ready = true
            reconnectScheduled = false
            flushPresence()
        } else if event == "ERROR" {
            let data = object["data"] as? [String: Any] ?? [:]
            let code = (data["code"] as? NSNumber)?.intValue ?? 0
            let message = data["message"] as? String ?? "Unknown error"
            logRpcWarning("Discord RPC error \(code): \(message)")
        }
    }

    private func sendActivity(_ activity: Any?) -> Bool
    {
        let payload: [String: Any] = [
            "cmd": "SET_ACTIVITY",
            "args": [
                "pid": processID,
                "activity": activity ?? NSNull()
            ],
            "nonce": UUID().uuidString.lowercased()
        ]
        guard let data = try? JSONSerialization.data(withJSONObject: payload) else { return false }
        return sendPacket(opcode: 1, payload: data)
    }

    private func sendPacket(opcode: UInt32, payload: Data) -> Bool
    {
        guard socketFD >= 0, payload.count <= 1024 * 1024 else { return false }
        var frame = Data()
        appendUInt32(opcode, to: &frame)
        appendUInt32(UInt32(payload.count), to: &frame)
        frame.append(payload)
        return frame.withUnsafeBytes { bytes in
            guard let base = bytes.baseAddress else { return false }
            var offset = 0
            while offset < bytes.count {
#if os(Linux)
                let written = send(socketFD, base.advanced(by: offset), bytes.count - offset, Int32(MSG_NOSIGNAL))
#else
                let written = send(socketFD, base.advanced(by: offset), bytes.count - offset, 0)
#endif
                if written > 0 {
                    offset += written
                    continue
                }
                if written < 0 && errno == EINTR { continue }
                if written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) {
                    var descriptor = pollfd(fd: socketFD, events: Int16(POLLOUT), revents: 0)
                    if poll(&descriptor, 1, 200) > 0 { continue }
                }
                return false
            }
            return true
        }
    }

    private func disconnectAndRetry(_ warning: String)
    {
        let hadReady = ready
        closeSocket()
        lastSentPayload.removeAll()
        lastSentAt = 0
        if !desiredPayload.isEmpty { pendingPayload = desiredPayload }
        if hadReady && !warning.isEmpty { logRpcWarning(warning) }
        scheduleReconnect()
    }

    private func closeSocket()
    {
        ready = false
        socketBuffer.removeAll(keepingCapacity: true)
        if let source = readSource {
            readSource = nil
            source.cancel()
        }
        if socketFD >= 0 {
#if os(Linux)
            _ = Glibc.close(socketFD)
#else
            _ = Darwin.close(socketFD)
#endif
            socketFD = -1
        }
    }

    private func scheduleReconnect()
    {
        guard !stopped, !reconnectScheduled else { return }
        reconnectScheduled = true
        queue.asyncAfter(deadline: .now() + .seconds(2)) {
            self.reconnectScheduled = false
            self.connectToDiscord()
        }
    }

    private func socketCandidates() -> [String]
    {
        let environment = ProcessInfo.processInfo.environment
        var baseDirectories: [String] = []
        for name in ["XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"] {
            let value = environment[name]?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            if !value.isEmpty { baseDirectories.append(value) }
        }
        #if os(Linux)
        baseDirectories.append("/run/user/\(getuid())")
        #endif
        baseDirectories.append("/tmp")

        var directories = baseDirectories
        #if os(Linux)
        for base in baseDirectories {
            let root = URL(fileURLWithPath: base, isDirectory: true)
            directories.append(root.appendingPathComponent("app/com.discordapp.Discord").path)
            directories.append(root.appendingPathComponent("snap.discord").path)
        }
        #endif
        var seen = Set<String>()
        var output: [String] = []
        for directory in directories {
            let clean = URL(fileURLWithPath: directory, isDirectory: true).standardizedFileURL.path
            if clean.isEmpty || seen.contains(clean) { continue }
            seen.insert(clean)
            for index in 0..<10 {
                let path = URL(fileURLWithPath: clean, isDirectory: true)
                    .appendingPathComponent("discord-ipc-\(index)").path
                if FileManager.default.fileExists(atPath: path) { output.append(path) }
            }
        }
        return output
    }

    private func startProxyPolling()
    {
        guard mode == .game, !username.isEmpty else { return }
        let generation = modeGeneration
        let currentUsername = username
        proxyTask = Task { [weak self] in
            guard let self else { return }
            while !Task.isCancelled {
                await self.pollProxy(username: currentUsername, generation: generation)
                do {
                    try await Task.sleep(nanoseconds: 5_000_000_000)
                } catch {
                    return
                }
            }
        }
    }

    private func stopProxyPolling()
    {
        proxyTask?.cancel()
        proxyTask = nil
    }

    private func pollProxy(username: String, generation: UInt64) async
    {
        guard let base = validProxyBaseURL(),
              var components = URLComponents(url: base, resolvingAgainstBaseURL: false) else {
            queue.async {
                self.logProxyWarning("Discord presence proxy URL is invalid.")
                if self.modeGeneration == generation { self.queuePresence(self.defaultGamePresence()) }
            }
            return
        }
        var path = components.path
        if !path.hasSuffix("/") { path += "/" }
        components.path = path + "presence"
        components.queryItems = [
            URLQueryItem(name: "username", value: username),
            URLQueryItem(name: "t", value: String(Int64(Date().timeIntervalSince1970 * 1000)))
        ]
        guard let url = components.url else { return }
        var request = URLRequest(url: url)
        request.setValue("no-store", forHTTPHeaderField: "Cache-Control")
        request.setValue("application/json", forHTTPHeaderField: "Accept")

        do {
            let response = try await fetchBounded(
                request,
                timeoutMilliseconds: 4_000,
                maximumBytes: 1024 * 1024,
                allowInsecureHTTP: base.scheme?.lowercased() == "http",
                redirectValidator: { source, destination in
                    source.scheme?.lowercased() == destination.scheme?.lowercased()
                    && source.host?.lowercased() == destination.host?.lowercased()
                    && source.port == destination.port
                })
            guard (200...299).contains(response.status),
                  let object = try JSONSerialization.jsonObject(with: response.data) as? [String: Any] else {
                throw NetworkFailure("Discord presence proxy returned invalid data", status: response.status)
            }
            let presence = parseProxyPresence(object)
            queue.async {
                guard !self.stopped,
                      self.mode == .game,
                      self.modeGeneration == generation else { return }
                self.queuePresence(presence ?? self.defaultGamePresence())
            }
        } catch is CancellationError {
        } catch {
            queue.async {
                guard !self.stopped,
                      self.mode == .game,
                      self.modeGeneration == generation else { return }
                self.logProxyWarning("Discord presence proxy request failed: \(error.localizedDescription)")
                self.queuePresence(self.defaultGamePresence())
            }
        }
    }

    private func validProxyBaseURL() -> URL?
    {
        guard let url = URL(string: proxyBaseURL), let host = url.host?.lowercased() else { return nil }
        let scheme = url.scheme?.lowercased()
        if scheme == "https" { return url }
        if scheme == "http" && ["127.0.0.1", "localhost", "::1"].contains(host) { return url }
        return nil
    }

    private func parseProxyPresence(_ payload: [String: Any]) -> DiscordPresenceValue?
    {
        guard payload["found"] as? Bool == true else { return nil }
        let timestamps = payload["timestamps"] as? [String: Any] ?? [:]
        let assets = payload["assets"] as? [String: Any] ?? [:]
        return DiscordPresenceValue(
            details: nonEmpty(payload["details"]) ?? "In Game",
            state: nonEmpty(payload["state"]) ?? "",
            startedAt: (timestamps["started_at"] as? NSNumber)?.int64Value ?? 0,
            largeImageKey: nonEmpty(assets["large_image"]) ?? "",
            largeImageText: nonEmpty(assets["large_text"]) ?? "",
            smallImageKey: nonEmpty(assets["small_image"]) ?? "",
            smallImageText: nonEmpty(assets["small_text"]) ?? "")
    }

    private func defaultGamePresence() -> DiscordPresenceValue
    {
        DiscordPresenceValue(
            details: "In Game",
            state: "",
            startedAt: 0,
            largeImageKey: "",
            largeImageText: "",
            smallImageKey: "",
            smallImageText: "")
    }

    private func nonEmpty(_ value: Any?) -> String?
    {
        guard let string = value as? String else { return nil }
        let output = string.trimmingCharacters(in: .whitespacesAndNewlines)
        return output.isEmpty ? nil : output
    }

    private func bounded(_ value: String, limit: Int) -> String
    {
        String(value.trimmingCharacters(in: .whitespacesAndNewlines).prefix(limit))
    }

    private func logRpcWarning(_ message: String)
    {
        let now = monotonicMilliseconds()
        if now - lastRpcLogAt < 60_000 { return }
        lastRpcLogAt = now
        log(3, "discord-rpc: \(message)")
    }

    private func logProxyWarning(_ message: String)
    {
        let now = monotonicMilliseconds()
        if now - lastProxyLogAt < 60_000 { return }
        lastProxyLogAt = now
        log(3, "discord-rpc: \(message)")
    }

    private func monotonicMilliseconds() -> UInt64
    {
        DispatchTime.now().uptimeNanoseconds / 1_000_000
    }

    private func readUInt32(_ data: Data, offset: Int) -> UInt32
    {
        data.withUnsafeBytes { bytes in
            let value = bytes.loadUnaligned(fromByteOffset: offset, as: UInt32.self)
            return UInt32(littleEndian: value)
        }
    }

    private func appendUInt32(_ value: UInt32, to data: inout Data)
    {
        var little = value.littleEndian
        withUnsafeBytes(of: &little) { data.append(contentsOf: $0) }
    }

    private static func connectUnixSocket(_ path: String) -> Int32
    {
        let bytes = Array(path.utf8)
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let capacity = MemoryLayout.size(ofValue: address.sun_path)
        guard !bytes.isEmpty && bytes.count < capacity else { return -1 }
        withUnsafeMutableBytes(of: &address.sun_path) { destination in
            destination.initializeMemory(as: UInt8.self, repeating: 0)
            destination.copyBytes(from: bytes)
        }

#if os(Linux)
        let streamType = Int32(SOCK_STREAM.rawValue)
#else
        let streamType = SOCK_STREAM
#endif
        let fd = socket(AF_UNIX, streamType, 0)
        guard fd >= 0 else { return -1 }
#if !os(Linux)
        var noSignal: Int32 = 1
        _ = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, socklen_t(MemoryLayout<Int32>.size))
#endif
        let flags = fcntl(fd, F_GETFL, 0)
        _ = fcntl(fd, F_SETFL, flags | O_NONBLOCK)
        let result = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        if result == 0 { return fd }
        if errno == EINPROGRESS {
            var descriptor = pollfd(fd: fd, events: Int16(POLLOUT), revents: 0)
            if poll(&descriptor, 1, 400) > 0 {
                var socketError: Int32 = 0
                var length = socklen_t(MemoryLayout<Int32>.size)
                if getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0 {
                    return fd
                }
            }
        }
#if os(Linux)
        _ = Glibc.close(fd)
#else
        _ = Darwin.close(fd)
#endif
        return -1
    }
}
