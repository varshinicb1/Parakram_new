import Foundation
import Combine

// MARK: - WebSocket Status

enum WsStatus: Equatable {
    case connecting
    case connected
    case disconnected
    case error(String)

    var displayLabel: String {
        switch self {
        case .connecting:      return "Connecting…"
        case .connected:       return "Live"
        case .disconnected:    return "Disconnected"
        case .error(let msg):  return "Error: \(msg)"
        }
    }

    var isLive: Bool { self == .connected }
}

// MARK: - Telemetry Reading

struct TelemetryReading: Identifiable {
    let id = UUID()
    let ts: TimeInterval
    let temperature: Double
    let humidity: Double
    let uptimeSeconds: Int
    let freeHeap: Int
    let rssi: Int

    // Convenience formatted strings

    var temperatureString: String {
        String(format: "%.1f°C", temperature)
    }

    var humidityString: String {
        String(format: "%.0f%%", humidity)
    }

    var uptimeString: String {
        let h = uptimeSeconds / 3600
        let m = (uptimeSeconds % 3600) / 60
        let s = uptimeSeconds % 60
        if h > 0 { return "\(h)h \(m)m" }
        if m > 0 { return "\(m)m \(s)s" }
        return "\(s)s"
    }

    var freeHeapKBString: String {
        String(format: "%.1f KB", Double(freeHeap) / 1024.0)
    }

    var rssiString: String { "\(rssi) dBm" }
}

// MARK: - Raw WebSocket Frame

private struct TelemetryFrame: Decodable {
    let type: String
    let ts: TimeInterval
    let data: TelemetryData

    struct TelemetryData: Decodable {
        let temperature: Double
        let humidity: Double
        let uptime_s: Int
        let free_heap: Int
        let rssi: Int
    }
}

// MARK: - TelemetryViewModel

@MainActor
class TelemetryViewModel: ObservableObject {
    @Published var readings: [TelemetryReading] = []
    @Published var latest: TelemetryReading? = nil
    @Published var status: WsStatus = .disconnected

    private var wsTask: URLSessionWebSocketTask?
    private var session: URLSession = .shared
    private var manuallyDisconnected = false
    private var currentDeviceId: String = ""
    private var currentToken: String = ""
    private var currentBaseURL: String = ""

    // MARK: - Public Interface

    func connect(
        deviceId: String,
        token: String,
        baseURL: String = "http://localhost:8400"
    ) {
        manuallyDisconnected = false
        currentDeviceId = deviceId
        currentToken = token
        currentBaseURL = baseURL

        openSocket(deviceId: deviceId, token: token, baseURL: baseURL)
    }

    func disconnect() {
        manuallyDisconnected = true
        closeSocket()
        status = .disconnected
    }

    // MARK: - Private Socket Management

    private func openSocket(deviceId: String, token: String, baseURL: String) {
        // Convert http/https → ws/wss
        let wsBase = baseURL
            .replacingOccurrences(of: "https://", with: "wss://")
            .replacingOccurrences(of: "http://", with: "ws://")
        let urlString = "\(wsBase)/api/telemetry/ws/\(deviceId)?token=\(token)"

        guard let url = URL(string: urlString) else {
            status = .error("Invalid URL")
            return
        }

        status = .connecting
        wsTask = session.webSocketTask(with: url)
        wsTask?.resume()
        status = .connected
        receiveLoop()
    }

    private func closeSocket() {
        wsTask?.cancel(with: .goingAway, reason: nil)
        wsTask = nil
    }

    // MARK: - Recursive Receive Loop

    private func receiveLoop() {
        guard let task = wsTask else { return }

        task.receive { [weak self] result in
            guard let self else { return }
            Task { @MainActor in
                switch result {
                case .success(let message):
                    self.handleMessage(message)
                    self.receiveLoop()

                case .failure(let error):
                    guard !self.manuallyDisconnected else { return }
                    self.status = .error(error.localizedDescription)
                    self.closeSocket()
                    self.scheduleRetry()
                }
            }
        }
    }

    private func handleMessage(_ message: URLSessionWebSocketTask.Message) {
        var jsonString: String?
        switch message {
        case .string(let s):  jsonString = s
        case .data(let d):    jsonString = String(data: d, encoding: .utf8)
        @unknown default:     return
        }

        guard let s = jsonString,
              let data = s.data(using: .utf8) else { return }

        do {
            let frame = try JSONDecoder().decode(TelemetryFrame.self, from: data)
            guard frame.type == "telemetry" else { return }

            let reading = TelemetryReading(
                ts:              frame.ts,
                temperature:     frame.data.temperature,
                humidity:        frame.data.humidity,
                uptimeSeconds:   frame.data.uptime_s,
                freeHeap:        frame.data.free_heap,
                rssi:            frame.data.rssi
            )

            if status != .connected { status = .connected }
            readings.append(reading)
            if readings.count > 50 { readings.removeFirst(readings.count - 50) }
            latest = reading

        } catch {
            // Non-fatal: ignore unrecognised frames
        }
    }

    private func scheduleRetry() {
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            guard !self.manuallyDisconnected else { return }
            self.openSocket(
                deviceId: self.currentDeviceId,
                token:    self.currentToken,
                baseURL:  self.currentBaseURL
            )
        }
    }
}
