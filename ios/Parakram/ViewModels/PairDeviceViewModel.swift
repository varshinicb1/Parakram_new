import Foundation
import Combine

// MARK: - Pair Step

enum PairStep {
    case scan
    case name
    case success
}

// MARK: - Board Options

struct BoardOption: Identifiable {
    let id: String       // sku
    let displayName: String
}

// MARK: - PairDeviceViewModel

@MainActor
class PairDeviceViewModel: ObservableObject {
    @Published var step: PairStep = .scan
    @Published var discoveredDevices: [BLEDevice] = []
    @Published var selectedDevice: BLEDevice? = nil
    @Published var deviceName: String = ""
    @Published var boardSku: String = "vdyt-s3-r1"
    @Published var isScanning: Bool = false
    @Published var isPairing: Bool = false
    @Published var errorMessage: String? = nil
    @Published var pairedDevice: Device? = nil

    private let bleManager = BLEManager()
    private var cancellables = Set<AnyCancellable>()
    private var scanStopTask: Task<Void, Never>? = nil

    static let boardOptions: [BoardOption] = [
        BoardOption(id: "vdyt-s3-r1", displayName: "ESP32-S3"),
        BoardOption(id: "rp2040",     displayName: "RP2040 Pico"),
        BoardOption(id: "stm32f4",    displayName: "STM32F4"),
        BoardOption(id: "arduino",    displayName: "Arduino")
    ]

    init() {
        bleManager.$discoveredDevices
            .receive(on: RunLoop.main)
            .assign(to: &$discoveredDevices)

        bleManager.$isScanning
            .receive(on: RunLoop.main)
            .assign(to: &$isScanning)
    }

    // MARK: - BLE Actions

    func startScan() {
        errorMessage = nil
        bleManager.startScan()
        // Auto-stop after 15 seconds
        scanStopTask?.cancel()
        scanStopTask = Task {
            try? await Task.sleep(nanoseconds: 15_000_000_000)
            guard !Task.isCancelled else { return }
            stopScan()
        }
    }

    func stopScan() {
        scanStopTask?.cancel()
        bleManager.stopScan()
    }

    func selectDevice(_ device: BLEDevice) {
        selectedDevice = device
        // Pre-fill name from BLE device name, stripping common prefixes
        deviceName = device.name
        stopScan()
        withAnimation {
            step = .name
        }
    }

    // MARK: - Pairing

    func pairDevice(token: String) async {
        guard let device = selectedDevice, !deviceName.trimmingCharacters(in: .whitespaces).isEmpty else {
            errorMessage = "Please select a device and enter a name"
            return
        }
        isPairing = true
        errorMessage = nil
        let req = PairDeviceRequest(
            bleAddress: device.address,
            name: deviceName.trimmingCharacters(in: .whitespaces),
            boardSku: boardSku
        )
        do {
            let paired = try await ParakramAPI.shared.pairDevice(token: token, request: req)
            pairedDevice = paired
            withAnimation {
                step = .success
            }
        } catch {
            errorMessage = error.localizedDescription
        }
        isPairing = false
    }

    // MARK: - Reset

    func reset() {
        stopScan()
        step = .scan
        selectedDevice = nil
        deviceName = ""
        boardSku = "vdyt-s3-r1"
        errorMessage = nil
        pairedDevice = nil
    }
}
