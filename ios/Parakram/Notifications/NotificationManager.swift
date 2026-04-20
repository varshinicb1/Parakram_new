import Foundation
import UserNotifications

// MARK: - NotificationManager

/// Manages APNs registration and push-token lifecycle for Parakram iOS.
///
/// Usage:
/// ```swift
/// // In root view onAppear:
/// Task { await NotificationManager.shared.requestPermission() }
///
/// // From AppDelegate / SceneDelegate:
/// NotificationManager.shared.registerDeviceToken(tokenData)
/// ```
///
/// The manager gracefully degrades when APNs is unavailable (e.g. simulator,
/// missing entitlements, or network error registering with the backend).
@MainActor
final class NotificationManager: NSObject, ObservableObject {

    // MARK: - Singleton

    static let shared = NotificationManager()

    // MARK: - Published state

    /// The most recently obtained APNs hex token.  `nil` if not yet registered.
    @Published private(set) var deviceToken: String?

    /// `true` once the user has granted notification permission.
    @Published private(set) var permissionGranted: Bool = false

    // MARK: - Private

    private override init() {}

    // MARK: - Permission

    /// Ask the user for notification permission and, if granted, register with
    /// APNs.  Safe to call multiple times — the OS caches the answer.
    func requestPermission() async {
        let center = UNUserNotificationCenter.current()
        do {
            let granted = try await center.requestAuthorization(
                options: [.alert, .sound, .badge]
            )
            permissionGranted = granted
            if granted {
                // Must be called on main thread.
                await MainActor.run {
                    UIApplication.shared.registerForRemoteNotifications()
                }
            }
        } catch {
            // Permission denied or an error occurred — degrade silently.
        }
    }

    // MARK: - Token handling

    /// Convert the raw APNs token `Data` to a lowercase hex string and register
    /// it with the Parakram backend.
    ///
    /// Call this from `application(_:didRegisterForRemoteNotificationsWithDeviceToken:)`.
    func registerDeviceToken(_ tokenData: Data) {
        let hexToken = tokenData.map { String(format: "%02x", $0) }.joined()
        deviceToken = hexToken

        // Register with backend asynchronously.
        Task {
            // Retrieve auth token from shared API singleton.
            let api = ParakramAPI.shared
            // The API stores the auth token in UserDefaults via the login flow.
            guard let authToken = UserDefaults.standard.string(forKey: "parakram_auth_token"),
                  !authToken.isEmpty else {
                // Not logged in yet — token will be registered after login via
                // NotificationManager.registerWithBackend(token:authToken:)
                return
            }
            await registerWithBackend(token: hexToken, authToken: authToken)
        }
    }

    /// POST `{ platform: "ios", token: hexToken }` to `/api/notifications/token`.
    ///
    /// Safe to call multiple times; the backend upserts on (user_id, platform).
    func registerWithBackend(token: String, authToken: String) async {
        guard !token.isEmpty, !authToken.isEmpty else { return }

        let api = ParakramAPI.shared
        do {
            try await api.registerPushToken(platform: "ios", token: token, authToken: authToken)
        } catch {
            // Network error or backend unavailable — will retry on next launch.
        }
    }
}
