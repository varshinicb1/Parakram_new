import SwiftUI
import UIKit

// MARK: - App Delegate

/// Handles APNs device token callbacks and delegates to NotificationManager.
class AppDelegate: NSObject, UIApplicationDelegate {

    func application(
        _ application: UIApplication,
        didRegisterForRemoteNotificationsWithDeviceToken deviceToken: Data
    ) {
        Task { @MainActor in
            NotificationManager.shared.registerDeviceToken(deviceToken)
        }
    }

    func application(
        _ application: UIApplication,
        didFailToRegisterForRemoteNotificationsWithError error: Error
    ) {
        // APNs unavailable (simulator, entitlement missing, etc.) — degrade silently.
    }
}

// MARK: - App State

class AppState: ObservableObject {
    @Published var isLoggedIn: Bool = false
    @Published var token: String = ""
    @Published var userEmail: String = ""
    @Published var userName: String = ""
    @Published var currentPlan: String = "free"

    func logout() {
        isLoggedIn = false
        token = ""
        userEmail = ""
        userName = ""
        // Clear stored token so push registration won't re-use stale credentials.
        UserDefaults.standard.removeObject(forKey: "parakram_auth_token")
    }

    func didLogin(token newToken: String) {
        isLoggedIn = true
        token = newToken
        // Persist for use in background push-token registration.
        UserDefaults.standard.set(newToken, forKey: "parakram_auth_token")
    }
}

// MARK: - Main App

@main
struct ParakramApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject var appState = AppState()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(appState)
                .preferredColorScheme(.dark)
                .onAppear {
                    // Request push notification permission on first launch.
                    Task {
                        await NotificationManager.shared.requestPermission()
                    }
                }
        }
    }
}

// MARK: - Root View

struct RootView: View {
    @EnvironmentObject var appState: AppState
    @State private var showRegister: Bool = false
    @State private var showForgotPassword: Bool = false
    @State private var showVerifyEmail: Bool = false
    @State private var pendingVerifyUsername: String = ""

    var body: some View {
        if appState.isLoggedIn {
            ContentView()
        } else if showVerifyEmail {
            VerifyEmailView(
                username: pendingVerifyUsername,
                onVerified: {
                    withAnimation {
                        showVerifyEmail = false
                        showRegister = false
                    }
                },
                onNavigateToLogin: {
                    withAnimation {
                        showVerifyEmail = false
                        showRegister = false
                    }
                }
            )
        } else if showRegister {
            RegisterView(
                onNavigateToLogin: {
                    withAnimation { showRegister = false }
                },
                onNeedsVerification: { username in
                    pendingVerifyUsername = username
                    withAnimation { showVerifyEmail = true }
                }
            )
            .environmentObject(appState)
        } else if showForgotPassword {
            ForgotPasswordView(onNavigateToLogin: {
                withAnimation { showForgotPassword = false }
            })
        } else {
            LoginView(
                onNavigateToRegister: {
                    withAnimation { showRegister = true }
                },
                onNavigateToForgotPassword: {
                    withAnimation { showForgotPassword = true }
                }
            )
        }
    }
}

// MARK: - Content View (Tab Bar)

struct ContentView: View {
    @State private var selectedTab: Int = 0
    @EnvironmentObject var appState: AppState

    var body: some View {
        TabView(selection: $selectedTab) {
            HomeView()
                .tabItem {
                    Label("Home", systemImage: "house.fill")
                }
                .tag(0)

            DevicesView()
                .tabItem {
                    Label("Devices", systemImage: "antenna.radiowaves.left.and.right")
                }
                .tag(1)

            ProgramView()
                .tabItem {
                    Label("Build", systemImage: "bolt.fill")
                }
                .tag(2)

            MarketplaceView()
                .tabItem {
                    Label("Market", systemImage: "storefront.fill")
                }
                .tag(3)

            FleetView()
                .tabItem {
                    Label("Fleet", systemImage: "server.rack")
                }
                .tag(4)

            SettingsView()
                .tabItem {
                    Label("Profile", systemImage: "person.crop.circle.fill")
                }
                .tag(5)
        }
        .accentColor(Color.pkPrimary)
        .onAppear {
            let tabBarAppearance = UITabBarAppearance()
            tabBarAppearance.configureWithOpaqueBackground()
            tabBarAppearance.backgroundColor = UIColor(Color.pkSurface)
            UITabBar.appearance().standardAppearance = tabBarAppearance
            UITabBar.appearance().scrollEdgeAppearance = tabBarAppearance
        }
    }
}

// MARK: - Preview

#Preview {
    let appState = AppState()
    appState.isLoggedIn = true
    appState.userEmail = "demo@parakram.com"
    appState.userName = "Demo User"
    return ContentView()
        .environmentObject(appState)
}
