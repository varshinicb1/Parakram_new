import Foundation
import CoreBluetooth
import Combine

// MARK: - BLE Device Model

struct BLEDevice: Identifiable, Equatable {
    let id: UUID
    let name: String
    let address: String
    let rssi: Int
    var peripheral: CBPeripheral?

    static func == (lhs: BLEDevice, rhs: BLEDevice) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - BLE Connection State

enum BLEConnectionState: Equatable {
    case idle
    case scanning
    case connecting
    case connected(BLEDevice)
    case error(String)

    static func == (lhs: BLEConnectionState, rhs: BLEConnectionState) -> Bool {
        switch (lhs, rhs) {
        case (.idle, .idle):           return true
        case (.scanning, .scanning):   return true
        case (.connecting, .connecting): return true
        case (.connected(let a), .connected(let b)): return a == b
        case (.error(let a), .error(let b)): return a == b
        default: return false
        }
    }

    var displayName: String {
        switch self {
        case .idle:                 return "Idle"
        case .scanning:             return "Scanning…"
        case .connecting:           return "Connecting…"
        case .connected(let dev):   return "Connected: \(dev.name)"
        case .error(let msg):       return "Error: \(msg)"
        }
    }

    var isScanning: Bool {
        if case .scanning = self { return true }
        return false
    }

    var connectedDevice: BLEDevice? {
        if case .connected(let dev) = self { return dev }
        return nil
    }
}

// MARK: - BLE Manager

class BLEManager: NSObject, ObservableObject {
    // MARK: Published State
    @Published var discoveredDevices: [BLEDevice] = []
    @Published var connectionState: BLEConnectionState = .idle
    @Published var isScanning: Bool = false
    @Published var centralManagerState: CBManagerState = .unknown
    @Published var rssiMap: [UUID: Int] = [:]

    // MARK: Private
    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var scanTimer: Timer?

    // Parakram service UUID (Nordic UART Service compatible)
    static let parakramServiceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let txCharUUID          = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let rxCharUUID          = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

    override init() {
        super.init()
        centralManager = CBCentralManager(
            delegate: self,
            queue: DispatchQueue(label: "com.parakram.ble", qos: .userInitiated)
        )
    }

    // MARK: - Scan

    func startScan() {
        guard centralManager.state == .poweredOn else {
            DispatchQueue.main.async { self.connectionState = .error("Bluetooth is not available") }
            return
        }
        DispatchQueue.main.async {
            self.discoveredDevices.removeAll()
            self.connectionState = .scanning
            self.isScanning = true
        }
        centralManager.scanForPeripherals(
            withServices: nil, // Scan all services, filter by name prefix
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )

        // Auto-stop after 30 seconds
        scanTimer?.invalidate()
        scanTimer = Timer.scheduledTimer(withTimeInterval: 30, repeats: false) { [weak self] _ in
            self?.stopScan()
        }
    }

    func stopScan() {
        centralManager.stopScan()
        scanTimer?.invalidate()
        DispatchQueue.main.async {
            self.isScanning = false
            if case .scanning = self.connectionState {
                self.connectionState = .idle
            }
        }
    }

    // MARK: - Connect

    func connect(to device: BLEDevice) {
        guard let peripheral = device.peripheral else { return }
        DispatchQueue.main.async { self.connectionState = .connecting }
        connectedPeripheral = peripheral
        peripheral.delegate = self
        centralManager.connect(peripheral, options: nil)
    }

    func disconnect() {
        if let peripheral = connectedPeripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
    }

    // MARK: - RSSI Update

    func readRSSI(for device: BLEDevice) {
        device.peripheral?.readRSSI()
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        DispatchQueue.main.async { self.centralManagerState = central.state }
        switch central.state {
        case .poweredOn:
            break
        case .poweredOff:
            DispatchQueue.main.async {
                self.connectionState = .error("Bluetooth is powered off")
                self.isScanning = false
                self.discoveredDevices.removeAll()
            }
        case .unauthorized:
            DispatchQueue.main.async { self.connectionState = .error("Bluetooth access denied") }
        case .unsupported:
            DispatchQueue.main.async { self.connectionState = .error("Bluetooth not supported") }
        default:
            break
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? "Unknown Device"
        guard name != "Unknown Device" else { return }

        let device = BLEDevice(
            id: peripheral.identifier,
            name: name,
            address: peripheral.identifier.uuidString,
            rssi: RSSI.intValue,
            peripheral: peripheral
        )

        DispatchQueue.main.async {
            if let idx = self.discoveredDevices.firstIndex(where: { $0.id == device.id }) {
                var updated = self.discoveredDevices[idx]
                updated = BLEDevice(
                    id: updated.id,
                    name: updated.name,
                    address: updated.address,
                    rssi: RSSI.intValue,
                    peripheral: peripheral
                )
                self.discoveredDevices[idx] = updated
            } else {
                self.discoveredDevices.append(device)
            }
            self.rssiMap[device.id] = RSSI.intValue
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([BLEManager.parakramServiceUUID])
        if let dev = discoveredDevices.first(where: { $0.id == peripheral.identifier }) {
            DispatchQueue.main.async { self.connectionState = .connected(dev) }
        }
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.connectionState = .error(error?.localizedDescription ?? "Connection failed")
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async {
            self.connectionState = .idle
            self.connectedPeripheral = nil
        }
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = peripheral.services else { return }
        for service in services {
            peripheral.discoverCharacteristics(
                [BLEManager.txCharUUID, BLEManager.rxCharUUID],
                for: service
            )
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        guard error == nil, let characteristics = service.characteristics else { return }
        for char in characteristics {
            if char.uuid == BLEManager.rxCharUUID {
                peripheral.setNotifyValue(true, for: char)
            }
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard error == nil, let data = characteristic.value else { return }
        // Handle incoming data from device
        if let text = String(data: data, encoding: .utf8) {
            print("BLE RX: \(text)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        guard error == nil else { return }
        DispatchQueue.main.async {
            self.rssiMap[peripheral.identifier] = RSSI.intValue
            if let idx = self.discoveredDevices.firstIndex(where: { $0.id == peripheral.identifier }) {
                let old = self.discoveredDevices[idx]
                self.discoveredDevices[idx] = BLEDevice(
                    id: old.id,
                    name: old.name,
                    address: old.address,
                    rssi: RSSI.intValue,
                    peripheral: peripheral
                )
            }
        }
    }
}

// MARK: - RSSI Signal Quality

extension Int {
    var rssiSignalQuality: Int {
        switch self {
        case -50...: return 4  // Excellent
        case -65..<(-50): return 3  // Good
        case -75..<(-65): return 2  // Fair
        case -85..<(-75): return 1  // Poor
        default: return 0           // No signal
        }
    }
}
