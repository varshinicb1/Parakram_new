import Foundation

// MARK: - Fleet Overview

struct FleetOverview: Codable {
    let totalDevices: Int
    let onlineDevices: Int
    let totalProjects: Int
    let deployedProjects: Int

    enum CodingKeys: String, CodingKey {
        case totalDevices     = "total_devices"
        case onlineDevices    = "online_devices"
        case totalProjects    = "total_projects"
        case deployedProjects = "deployed_projects"
    }

    static let preview = FleetOverview(
        totalDevices: 5,
        onlineDevices: 3,
        totalProjects: 8,
        deployedProjects: 4
    )
}

// MARK: - Fleet Device

struct FleetDevice: Codable, Identifiable {
    let id: String               // maps to device_id on the wire
    let name: String
    let boardSku: String
    let status: String
    let activeProjectName: String?
    let lastSeenAt: String?

    enum CodingKeys: String, CodingKey {
        case id                = "device_id"
        case name
        case boardSku          = "board_sku"
        case status
        case activeProjectName = "active_project_name"
        case lastSeenAt        = "last_seen_at"
    }

    var isOnline: Bool { status == "online" }

    /// Human-readable relative time for lastSeenAt ISO-8601 string
    var lastSeenRelative: String {
        guard let raw = lastSeenAt,
              let date = ISO8601DateFormatter().date(from: raw) else {
            return "Unknown"
        }
        let diff = Date().timeIntervalSince(date)
        switch diff {
        case ..<10:       return "Just now"
        case ..<60:       return "\(Int(diff))s ago"
        case ..<3600:     return "\(Int(diff / 60))m ago"
        case ..<86400:    return "\(Int(diff / 3600))h ago"
        default:          return "\(Int(diff / 86400))d ago"
        }
    }

    static let previews: [FleetDevice] = [
        FleetDevice(
            id: "dev-001",
            name: "ESP32 Lab Board",
            boardSku: "ESP32-S3",
            status: "online",
            activeProjectName: "Blink LED",
            lastSeenAt: ISO8601DateFormatter().string(from: Date(timeIntervalSinceNow: -90))
        ),
        FleetDevice(
            id: "dev-002",
            name: "Rooftop Sensor",
            boardSku: "ESP32-S3",
            status: "offline",
            activeProjectName: "Weather Station",
            lastSeenAt: ISO8601DateFormatter().string(from: Date(timeIntervalSinceNow: -7200))
        ),
        FleetDevice(
            id: "dev-003",
            name: "Motor Controller",
            boardSku: "RP2040",
            status: "online",
            activeProjectName: nil,
            lastSeenAt: ISO8601DateFormatter().string(from: Date(timeIntervalSinceNow: -30))
        )
    ]
}
