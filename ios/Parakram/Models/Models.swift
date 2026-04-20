import Foundation

// MARK: - Device

struct Device: Codable, Identifiable, Equatable {
    let id: String
    let name: String
    let boardType: String
    let firmwareVersion: String?
    let isConnected: Bool
    let lastSeen: String?
    let ipAddress: String?
    let macAddress: String?
    // Pairing / BLE fields (from /api/devices/pair response)
    let boardSku: String?
    let bleAddress: String?
    let status: String?

    enum CodingKeys: String, CodingKey {
        case id
        // /api/devices/pair returns "deviceId"; list endpoint returns "id"
        case deviceId        = "deviceId"
        case name
        case boardType       = "board_type"
        case firmwareVersion = "firmware_version"
        case isConnected     = "is_connected"
        case lastSeen        = "last_seen"
        case ipAddress       = "ip_address"
        case macAddress      = "mac_address"
        case boardSku        = "board_sku"
        case bleAddress      = "ble_address"
        case status
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        // Accept either "id" or "deviceId"
        if let rawId = try? c.decode(String.self, forKey: .id) {
            id = rawId
        } else {
            id = try c.decode(String.self, forKey: .deviceId)
        }
        name             = try c.decode(String.self, forKey: .name)
        boardType        = (try? c.decode(String.self, forKey: .boardType)) ?? ""
        firmwareVersion  = try? c.decode(String.self, forKey: .firmwareVersion)
        isConnected      = (try? c.decode(Bool.self,   forKey: .isConnected)) ?? false
        lastSeen         = try? c.decode(String.self, forKey: .lastSeen)
        ipAddress        = try? c.decode(String.self, forKey: .ipAddress)
        macAddress       = try? c.decode(String.self, forKey: .macAddress)
        boardSku         = try? c.decode(String.self, forKey: .boardSku)
        bleAddress       = try? c.decode(String.self, forKey: .bleAddress)
        status           = try? c.decode(String.self, forKey: .status)
    }

    init(
        id: String,
        name: String,
        boardType: String,
        firmwareVersion: String?,
        isConnected: Bool,
        lastSeen: String?,
        ipAddress: String?,
        macAddress: String?,
        boardSku: String? = nil,
        bleAddress: String? = nil,
        status: String? = nil
    ) {
        self.id              = id
        self.name            = name
        self.boardType       = boardType
        self.firmwareVersion = firmwareVersion
        self.isConnected     = isConnected
        self.lastSeen        = lastSeen
        self.ipAddress       = ipAddress
        self.macAddress      = macAddress
        self.boardSku        = boardSku
        self.bleAddress      = bleAddress
        self.status          = status
    }

    static let preview = Device(
        id: "dev-001",
        name: "Parakram Dev Board",
        boardType: "esp32",
        firmwareVersion: "1.2.0",
        isConnected: true,
        lastSeen: "2025-04-20T10:30:00Z",
        ipAddress: "192.168.1.100",
        macAddress: "AA:BB:CC:DD:EE:FF"
    )
}

// MARK: - Project

struct Project: Codable, Identifiable {
    let id: String
    let name: String
    let description: String
    let deviceId: String?
    let status: String
    let createdAt: String
    let updatedAt: String?

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case description
        case deviceId  = "device_id"
        case status
        case createdAt = "created_at"
        case updatedAt = "updated_at"
    }

    static let preview = Project(
        id: "proj-001",
        name: "Blink LED on button press",
        description: "Toggle LED when button is pressed",
        deviceId: "dev-001",
        status: "deployed",
        createdAt: "2025-04-20T09:00:00Z",
        updatedAt: "2025-04-20T10:00:00Z"
    )
}

// MARK: - Intent Request / Response

struct IntentRequest: Codable {
    let description: String
    let boardId: String
    let userId: String?

    enum CodingKeys: String, CodingKey {
        case description
        case boardId  = "board_id"
        case userId   = "user_id"
    }
}

struct IntentResponse: Codable {
    let intentId: String
    let preview: IRPreview
    let validated: ValidationResult
    let rawIr: String?

    enum CodingKeys: String, CodingKey {
        case intentId  = "intent_id"
        case preview
        case validated
        case rawIr     = "raw_ir"
    }
}

// MARK: - IR Preview

struct IRPreview: Codable {
    let sensors: [String]
    let actuators: [String]
    let logic: [String]
    let estimatedLines: Int?
    let warnings: [String]

    enum CodingKeys: String, CodingKey {
        case sensors
        case actuators
        case logic
        case estimatedLines = "estimated_lines"
        case warnings
    }

    static let preview = IRPreview(
        sensors: ["button", "temperature"],
        actuators: ["led", "buzzer"],
        logic: ["on button press", "toggle led", "beep buzzer"],
        estimatedLines: 48,
        warnings: []
    )
}

// MARK: - Validation Result

struct ValidationResult: Codable {
    let isValid: Bool
    let errors: [String]
    let warnings: [String]
    let score: Double?

    enum CodingKeys: String, CodingKey {
        case isValid   = "is_valid"
        case errors
        case warnings
        case score
    }
}

// MARK: - Compile Result

struct CompileResult: Codable {
    let success: Bool
    let binarySize: Int?
    let duration: Double?
    let errors: [String]
    let warnings: [String]
    let firmwarePath: String?

    enum CodingKeys: String, CodingKey {
        case success
        case binarySize   = "binary_size"
        case duration
        case errors
        case warnings
        case firmwarePath = "firmware_path"
    }
}

// MARK: - Deploy Request

struct DeployRequest: Codable {
    let intentId: String
    let deviceId: String
    let method: String

    enum CodingKeys: String, CodingKey {
        case intentId  = "intent_id"
        case deviceId  = "device_id"
        case method
    }
}

// MARK: - Auth

struct LoginRequest: Codable {
    let email: String
    let password: String
}

struct RegisterRequest: Encodable {
    let username: String
    let email: String
    let password: String
}

// MARK: - Pair Device

struct PairDeviceRequest: Encodable {
    let bleAddress: String
    let name: String
    let boardSku: String

    enum CodingKeys: String, CodingKey {
        case bleAddress = "ble_address"
        case name
        case boardSku   = "board_sku"
    }
}

struct LoginResponse: Codable {
    let token: String
    let userId: String
    let email: String
    let name: String?
    let plan: String?
    let expiresAt: String?

    enum CodingKeys: String, CodingKey {
        case token
        case userId    = "user_id"
        case email
        case name
        case plan
        case expiresAt = "expires_at"
    }
}

// MARK: - Health Check

struct HealthResponse: Codable {
    let status: String
    let version: String?
    let uptime: Double?
    let services: [String: String]?
}

// MARK: - Community Driver

struct CommunityDriver: Codable, Identifiable {
    let id: String
    let name: String
    let displayName: String
    let description: String
    let driverType: String
    let busTypes: [String]
    let capabilities: [String]
    let status: String
    let downloads: Int
    let starsTotal: Int
    let starsCount: Int

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case displayName  = "display_name"
        case description
        case driverType   = "driver_type"
        case busTypes     = "bus_types"
        case capabilities
        case status
        case downloads
        case starsTotal   = "stars_total"
        case starsCount   = "stars_count"
    }

    var averageRating: Double {
        guard starsCount > 0 else { return 0 }
        return Double(starsTotal) / Double(starsCount)
    }

    static let preview = CommunityDriver(
        id: "drv-001",
        name: "dht22",
        displayName: "DHT22 Temperature Sensor",
        description: "High-accuracy temperature and humidity sensor driver with I2C support",
        driverType: "sensor",
        busTypes: ["i2c", "gpio"],
        capabilities: ["temperature", "humidity"],
        status: "verified",
        downloads: 1240,
        starsTotal: 456,
        starsCount: 100
    )

    static let previews: [CommunityDriver] = [
        CommunityDriver(
            id: "drv-001",
            name: "dht22",
            displayName: "DHT22 Temp Sensor",
            description: "Temperature and humidity sensor",
            driverType: "sensor",
            busTypes: ["i2c"],
            capabilities: ["temperature", "humidity"],
            status: "verified",
            downloads: 1240,
            starsTotal: 456,
            starsCount: 100
        ),
        CommunityDriver(
            id: "drv-002",
            name: "neopixel",
            displayName: "NeoPixel LED Strip",
            description: "RGB LED strip controller",
            driverType: "actuator",
            busTypes: ["spi", "gpio"],
            capabilities: ["rgb", "animation"],
            status: "verified",
            downloads: 3450,
            starsTotal: 980,
            starsCount: 200
        ),
        CommunityDriver(
            id: "drv-003",
            name: "l298n",
            displayName: "L298N Motor Driver",
            description: "Dual H-bridge motor controller",
            driverType: "actuator",
            busTypes: ["gpio", "pwm"],
            capabilities: ["motor", "direction", "speed"],
            status: "community",
            downloads: 890,
            starsTotal: 320,
            starsCount: 80
        ),
        CommunityDriver(
            id: "drv-004",
            name: "hcsr04",
            displayName: "HC-SR04 Ultrasonic",
            description: "Ultrasonic distance sensor",
            driverType: "sensor",
            busTypes: ["gpio"],
            capabilities: ["distance"],
            status: "verified",
            downloads: 2100,
            starsTotal: 640,
            starsCount: 150
        )
    ]
}

// MARK: - Billing Plan

struct BillingPlan: Codable, Identifiable {
    var id: String { tier }
    let tier: String
    let displayName: String
    let monthlyPriceUsd: Double
    let llmIntentsPerMonth: Int
    let compilesPerMonth: Int
    let maxDevices: Int
    let features: [String]

    enum CodingKeys: String, CodingKey {
        case tier
        case displayName       = "display_name"
        case monthlyPriceUsd   = "monthly_price_usd"
        case llmIntentsPerMonth = "llm_intents_per_month"
        case compilesPerMonth  = "compiles_per_month"
        case maxDevices        = "max_devices"
        case features
    }

    static let previews: [BillingPlan] = [
        BillingPlan(
            tier: "free",
            displayName: "Free",
            monthlyPriceUsd: 0,
            llmIntentsPerMonth: 50,
            compilesPerMonth: 20,
            maxDevices: 2,
            features: ["2 devices", "50 AI requests/mo", "20 compiles/mo", "Community drivers"]
        ),
        BillingPlan(
            tier: "pro",
            displayName: "Pro",
            monthlyPriceUsd: 19,
            llmIntentsPerMonth: 500,
            compilesPerMonth: 200,
            maxDevices: 10,
            features: ["10 devices", "500 AI requests/mo", "200 compiles/mo", "Priority support", "Private drivers"]
        ),
        BillingPlan(
            tier: "team",
            displayName: "Team",
            monthlyPriceUsd: 79,
            llmIntentsPerMonth: 2000,
            compilesPerMonth: 1000,
            maxDevices: 50,
            features: ["50 devices", "2000 AI requests/mo", "1000 compiles/mo", "Team workspace", "SLA guarantee", "Dedicated support"]
        )
    ]
}

// MARK: - Usage Counter

struct UsageCounter: Codable {
    let used: Int
    let limit: Int

    var fraction: Double {
        guard limit > 0 else { return 0 }
        return min(Double(used) / Double(limit), 1.0)
    }

    var isNearLimit: Bool { fraction >= 0.80 }
    var isAtLimit: Bool   { fraction >= 1.0 }
}

// MARK: - Billing Usage

struct BillingUsage: Codable {
    let periodStart: String
    let llmIntents: UsageCounter
    let compiles: UsageCounter
    let deploys: UsageCounter
    let devicesActive: UsageCounter

    enum CodingKeys: String, CodingKey {
        case periodStart    = "period_start"
        case llmIntents     = "llm_intents"
        case compiles
        case deploys
        case devicesActive  = "devices_active"
    }

    static let preview = BillingUsage(
        periodStart: "2025-04-01",
        llmIntents: UsageCounter(used: 23, limit: 50),
        compiles: UsageCounter(used: 8, limit: 20),
        deploys: UsageCounter(used: 5, limit: 20),
        devicesActive: UsageCounter(used: 1, limit: 2)
    )
}

// MARK: - Deployment State

enum DeploymentState: Equatable {
    case idle
    case compiling
    case compiled
    case transferring
    case verifying
    case running
    case error(String)

    var displayName: String {
        switch self {
        case .idle:         return "Ready"
        case .compiling:    return "Compiling"
        case .compiled:     return "Compiled"
        case .transferring: return "Transferring"
        case .verifying:    return "Verifying"
        case .running:      return "Running"
        case .error(let m): return "Error: \(m)"
        }
    }

    var icon: String {
        switch self {
        case .idle:         return "circle"
        case .compiling:    return "hammer.fill"
        case .compiled:     return "checkmark.circle.fill"
        case .transferring: return "arrow.up.circle.fill"
        case .verifying:    return "magnifyingglass.circle.fill"
        case .running:      return "play.circle.fill"
        case .error:        return "exclamationmark.circle.fill"
        }
    }

    var isActive: Bool {
        switch self {
        case .idle, .error: return false
        default:            return true
        }
    }
}

// MARK: - Recent Activity Item

struct ActivityItem: Identifiable {
    let id = UUID()
    let icon: String
    let title: String
    let subtitle: String
    let time: String
    let color: String

    static let previews: [ActivityItem] = [
        ActivityItem(icon: "bolt.fill",       title: "Firmware deployed",    subtitle: "esp32-dev-01",  time: "2m ago",  color: "#00E676"),
        ActivityItem(icon: "hammer.fill",     title: "Build completed",      subtitle: "blink_led",     time: "15m ago", color: "#6C63FF"),
        ActivityItem(icon: "antenna.radiowaves.left.and.right", title: "Device connected", subtitle: "BLE scan",   time: "1h ago",  color: "#00D9FF"),
        ActivityItem(icon: "wand.and.stars",  title: "AI generated code",    subtitle: "motor control", time: "3h ago",  color: "#FFAB00")
    ]
}
