import Foundation
import Combine

class DevicesViewModel: ObservableObject {
    @Published var devices: [BLEDevice] = []
    @Published var connectionState: BLEConnectionState = .idle
    @Published var isScanning: Bool = false
    @Published var selectedDevice: BLEDevice? = nil
    @Published var errorMessage: String? = nil

    @Published var bleManager = BLEManager()

    private var cancellables = Set<AnyCancellable>()

    init() {
        // Sync BLE manager state into this view model
        bleManager.$discoveredDevices
            .receive(on: DispatchQueue.main)
            .assign(to: &$devices)

        bleManager.$connectionState
            .receive(on: DispatchQueue.main)
            .assign(to: &$connectionState)

        bleManager.$isScanning
            .receive(on: DispatchQueue.main)
            .assign(to: &$isScanning)
    }

    func startScan() {
        bleManager.startScan()
    }

    func stopScan() {
        bleManager.stopScan()
    }

    func connect(to device: BLEDevice) {
        selectedDevice = device
        bleManager.connect(to: device)
    }

    func disconnect() {
        bleManager.disconnect()
        selectedDevice = nil
    }

    func toggleScan() {
        if isScanning {
            stopScan()
        } else {
            startScan()
        }
    }

    @MainActor
    func refresh() async {
        if isScanning { stopScan() }
        devices.removeAll()
        startScan()
        // Run for 5 seconds then stop
        try? await Task.sleep(nanoseconds: 5_000_000_000)
        stopScan()
    }

    var connectedDevice: BLEDevice? {
        connectionState.connectedDevice
    }

    var isConnected: Bool {
        if case .connected = connectionState { return true }
        return false
    }

    func rssiIcon(for rssi: Int) -> String {
        switch rssi.rssiSignalQuality {
        case 4:  return "wifi"
        case 3:  return "wifi"
        case 2:  return "wifi.exclamationmark"
        case 1:  return "wifi.exclamationmark"
        default: return "wifi.slash"
        }
    }
}
