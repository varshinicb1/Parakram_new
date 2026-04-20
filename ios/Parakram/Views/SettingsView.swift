import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var appState: AppState
    @State private var showLogoutConfirm: Bool = false
    @State private var apiURL: String = "https://api.parakram.io"
    @State private var notificationsEnabled: Bool = true
    @State private var darkMode: Bool = true
    @State private var llmApiKey: String = ""
    @State private var llmKeyVisible: Bool = false
    @State private var isSavingKey: Bool = false
    @State private var saveKeyMessage: String? = nil

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Profile card
                    ProfileCard(email: appState.userEmail, name: appState.userName, plan: appState.currentPlan)
                        .padding(.horizontal, 16)

                    // App settings
                    SettingsSection(title: "Preferences") {
                        SettingsToggle(
                            icon: "bell.badge.fill",
                            iconColor: .pkTertiary,
                            label: "Notifications",
                            isOn: $notificationsEnabled
                        )
                        SettingsToggle(
                            icon: "moon.fill",
                            iconColor: .pkSecondary,
                            label: "Dark Mode",
                            isOn: $darkMode
                        )
                    }

                    // Connection settings
                    SettingsSection(title: "Connection") {
                        SettingsTextField(
                            icon: "network",
                            iconColor: .pkPrimary,
                            label: "API URL",
                            text: $apiURL
                        )
                    }

                    // AI / OpenRouter BYOK
                    SettingsSection(title: "AI / OpenRouter") {
                        VStack(spacing: 12) {
                            HStack(spacing: 14) {
                                ZStack {
                                    RoundedRectangle(cornerRadius: 8)
                                        .fill(Color.pkPrimary.opacity(0.15))
                                        .frame(width: 32, height: 32)
                                    Image(systemName: "key.fill")
                                        .font(.system(size: 14, weight: .semibold))
                                        .foregroundColor(.pkPrimary)
                                }
                                VStack(alignment: .leading, spacing: 2) {
                                    Text("OpenRouter API Key")
                                        .font(.system(size: 12))
                                        .foregroundColor(.pkTextTertiary)
                                    Group {
                                        if llmKeyVisible {
                                            TextField("sk-or-v1-...", text: $llmApiKey)
                                        } else {
                                            SecureField("sk-or-v1-...", text: $llmApiKey)
                                        }
                                    }
                                    .font(.system(size: 14))
                                    .foregroundColor(.pkTextPrimary)
                                    .accentColor(.pkPrimary)
                                }
                                Button {
                                    llmKeyVisible.toggle()
                                } label: {
                                    Image(systemName: llmKeyVisible ? "eye.slash.fill" : "eye.fill")
                                        .foregroundColor(.pkTextTertiary)
                                }
                            }
                            .padding(.horizontal, 14)
                            .padding(.top, 12)

                            Text("Your key is used for AI intent processing. Get a free key at openrouter.ai")
                                .font(.caption)
                                .foregroundColor(.pkTextTertiary)
                                .padding(.horizontal, 14)

                            if let msg = saveKeyMessage {
                                Text(msg)
                                    .font(.caption)
                                    .foregroundColor(.pkPrimary)
                                    .padding(.horizontal, 14)
                            }

                            Button {
                                Task {
                                    isSavingKey = true
                                    saveKeyMessage = nil
                                    do {
                                        try await ParakramAPI.shared.setLlmKey(
                                            apiKey: llmApiKey,
                                            token: appState.token
                                        )
                                        saveKeyMessage = "API key saved"
                                    } catch {
                                        saveKeyMessage = "Save failed: \(error.localizedDescription)"
                                    }
                                    isSavingKey = false
                                }
                            } label: {
                                HStack {
                                    if isSavingKey {
                                        ProgressView()
                                            .tint(.white)
                                            .scaleEffect(0.8)
                                    }
                                    Text("Save API Key")
                                        .fontWeight(.semibold)
                                }
                                .frame(maxWidth: .infinity, minHeight: 44)
                                .background(Color.pkPrimary)
                                .foregroundColor(.white)
                                .clipShape(RoundedRectangle(cornerRadius: 10))
                            }
                            .disabled(isSavingKey)
                            .padding(.horizontal, 14)
                            .padding(.bottom, 12)
                        }
                    }

                    // About
                    SettingsSection(title: "About") {
                        SettingsRow(icon: "info.circle.fill", iconColor: .pkSecondary, label: "Version", detail: "1.0.0")
                        SettingsRow(icon: "building.2.fill", iconColor: .pkTertiary, label: "Company", detail: "Vidyuthlabs")
                        SettingsRow(icon: "globe", iconColor: .pkPrimary, label: "Website", detail: "vidyuthlabs.co.in")
                        SettingsRow(icon: "doc.text.fill", iconColor: Color.pkWarning, label: "License", detail: "MIT")
                    }

                    // Logout
                    Button(action: { showLogoutConfirm = true }) {
                        HStack(spacing: 10) {
                            Image(systemName: "rectangle.portrait.and.arrow.right.fill")
                                .font(.system(size: 16, weight: .semibold))
                            Text("Sign Out")
                                .font(.system(size: 16, weight: .semibold))
                        }
                        .foregroundColor(.pkError)
                        .frame(maxWidth: .infinity, minHeight: 52)
                        .background(Color.pkError.opacity(0.10))
                        .clipShape(RoundedRectangle(cornerRadius: 14))
                    }
                    .padding(.horizontal, 16)

                    Text("Parakram · Vidyuthlabs · MIT License")
                        .font(.caption2)
                        .foregroundColor(.pkTextTertiary)
                        .padding(.bottom, 32)
                }
                .padding(.vertical, 16)
            }
            .background(Color.pkBackground.ignoresSafeArea())
            .navigationTitle("Profile")
            .navigationBarTitleDisplayMode(.large)
            .alert("Sign Out?", isPresented: $showLogoutConfirm) {
                Button("Cancel", role: .cancel) {}
                Button("Sign Out", role: .destructive) { appState.logout() }
            } message: {
                Text("You'll need to sign in again to access your devices and programs.")
            }
        }
    }
}

// MARK: - Profile Card

private struct ProfileCard: View {
    let email: String
    let name: String
    let plan: String

    var planColor: Color {
        switch plan {
        case "pro":  return .pkPrimary
        case "team": return .pkSecondary
        default:     return Color.white.opacity(0.4)
        }
    }

    var body: some View {
        HStack(spacing: 16) {
            ZStack {
                Circle()
                    .fill(LinearGradient.pkPrimary)
                    .frame(width: 60, height: 60)
                Text(initials)
                    .font(.system(size: 22, weight: .bold))
                    .foregroundColor(.white)
            }
            VStack(alignment: .leading, spacing: 3) {
                Text(name.isEmpty ? "Parakram User" : name)
                    .font(.system(size: 17, weight: .bold))
                    .foregroundColor(.pkTextPrimary)
                Text(email)
                    .font(.subheadline)
                    .foregroundColor(.pkTextSecondary)
                Text(plan.capitalized + " Plan")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundColor(planColor)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(planColor.opacity(0.12))
                    .clipShape(Capsule())
            }
            Spacer()
        }
        .padding(20)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 18))
    }

    var initials: String {
        let parts = name.split(separator: " ")
        if parts.count >= 2 {
            return String((parts[0].prefix(1) + parts[1].prefix(1)).uppercased())
        }
        return String(name.prefix(2).uppercased())
    }
}

// MARK: - Settings Section

private struct SettingsSection<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(.pkTextTertiary)
                .padding(.horizontal, 20)

            VStack(spacing: 0) {
                content()
            }
            .background(Color.pkSurface)
            .clipShape(RoundedRectangle(cornerRadius: 14))
            .padding(.horizontal, 16)
        }
    }
}

// MARK: - Settings Row

private struct SettingsRow: View {
    let icon: String
    let iconColor: Color
    let label: String
    let detail: String

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(iconColor.opacity(0.15))
                    .frame(width: 32, height: 32)
                Image(systemName: icon)
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(iconColor)
            }
            Text(label)
                .font(.system(size: 15))
                .foregroundColor(.pkTextPrimary)
            Spacer()
            Text(detail)
                .font(.system(size: 14))
                .foregroundColor(.pkTextSecondary)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .overlay(alignment: .bottom) {
            Rectangle()
                .fill(Color.white.opacity(0.05))
                .frame(height: 0.5)
                .padding(.leading, 60)
        }
    }
}

// MARK: - Settings Toggle

private struct SettingsToggle: View {
    let icon: String
    let iconColor: Color
    let label: String
    @Binding var isOn: Bool

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(iconColor.opacity(0.15))
                    .frame(width: 32, height: 32)
                Image(systemName: icon)
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(iconColor)
            }
            Text(label)
                .font(.system(size: 15))
                .foregroundColor(.pkTextPrimary)
            Spacer()
            Toggle("", isOn: $isOn)
                .labelsHidden()
                .tint(.pkPrimary)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .overlay(alignment: .bottom) {
            Rectangle()
                .fill(Color.white.opacity(0.05))
                .frame(height: 0.5)
                .padding(.leading, 60)
        }
    }
}

// MARK: - Settings TextField

private struct SettingsTextField: View {
    let icon: String
    let iconColor: Color
    let label: String
    @Binding var text: String

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(iconColor.opacity(0.15))
                    .frame(width: 32, height: 32)
                Image(systemName: icon)
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(iconColor)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(label)
                    .font(.system(size: 12))
                    .foregroundColor(.pkTextTertiary)
                TextField("", text: $text)
                    .font(.system(size: 14))
                    .foregroundColor(.pkTextPrimary)
                    .accentColor(.pkPrimary)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
    }
}

#Preview {
    SettingsView().environmentObject({
        let s = AppState()
        s.userEmail = "demo@parakram.com"
        s.userName = "Demo User"
        s.currentPlan = "pro"
        s.isLoggedIn = true
        return s
    }())
}
