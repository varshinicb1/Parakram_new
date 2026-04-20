import Foundation
import Combine

class HomeViewModel: ObservableObject {
    @Published var backendOnline: Bool = false
    @Published var deviceCount: Int = 0
    @Published var programCount: Int = 0
    @Published var recentProjects: [Project] = []
    @Published var recentActivity: [ActivityItem] = []
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil

    private var refreshTask: Task<Void, Never>?

    init() {
        // Load cached/preview data immediately for snappy UI
        recentActivity = ActivityItem.previews
    }

    @MainActor
    func refresh(token: String = "") async {
        isLoading = true
        errorMessage = nil

        // Check backend health
        do {
            let health = try await ParakramAPI.shared.healthCheck()
            backendOnline = health.status.lowercased() == "ok" || health.status.lowercased() == "healthy"
        } catch {
            print("Health check error: \(error)")
            backendOnline = false
        }

        // Load devices if token available
        if !token.isEmpty {
            do {
                let devices = try await ParakramAPI.shared.listDevices(token: token)
                deviceCount = devices.count
            } catch {
                print("Device list error: \(error)")
            }
        }

        isLoading = false
    }

    @MainActor
    func startPeriodicRefresh(token: String) {
        refreshTask?.cancel()
        refreshTask = Task {
            while !Task.isCancelled {
                await refresh(token: token)
                try? await Task.sleep(nanoseconds: 30_000_000_000) // 30 seconds
            }
        }
    }

    func stopPeriodicRefresh() {
        refreshTask?.cancel()
        refreshTask = nil
    }

    var greetingText: String {
        let hour = Calendar.current.component(.hour, from: Date())
        switch hour {
        case 5..<12:  return "Good morning"
        case 12..<17: return "Good afternoon"
        case 17..<21: return "Good evening"
        default:      return "Good night"
        }
    }

    var statusColor: String {
        backendOnline ? "#00E676" : "#FF5252"
    }

    var statusLabel: String {
        backendOnline ? "Online" : "Offline"
    }
}
