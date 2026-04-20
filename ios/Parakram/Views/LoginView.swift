import SwiftUI

struct LoginView: View {
    @EnvironmentObject var appState: AppState
    var onNavigateToRegister: () -> Void = {}
    var onNavigateToForgotPassword: () -> Void = {}
    @State private var email: String = ""
    @State private var password: String = ""
    @State private var isLoading: Bool = false
    @State private var errorMessage: String? = nil
    @State private var showPassword: Bool = false
    @State private var glowOpacity: Double = 0.4

    var body: some View {
        ZStack {
            // Animated background
            Color.pkBackground.ignoresSafeArea()

            // Ambient glow circles
            Circle()
                .fill(RadialGradient.pkGlow(color: .pkPrimary))
                .frame(width: 400, height: 400)
                .offset(x: -100, y: -200)
                .opacity(glowOpacity)

            Circle()
                .fill(RadialGradient.pkGlow(color: .pkSecondary))
                .frame(width: 300, height: 300)
                .offset(x: 120, y: 180)
                .opacity(glowOpacity * 0.6)

            VStack(spacing: 0) {
                Spacer()

                // Logo
                VStack(spacing: 12) {
                    ZStack {
                        Circle()
                            .fill(LinearGradient.pkPrimary)
                            .frame(width: 80, height: 80)
                        Image(systemName: "bolt.fill")
                            .font(.system(size: 36, weight: .bold))
                            .foregroundColor(.white)
                    }
                    Text("Parakram")
                        .font(.system(size: 34, weight: .bold))
                        .foregroundColor(.pkTextPrimary)
                    Text("Zero-code hardware programming")
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                }
                .padding(.bottom, 48)

                // Glass card
                VStack(spacing: 20) {
                    // Email field
                    VStack(alignment: .leading, spacing: 6) {
                        Label("Email", systemImage: "envelope.fill")
                            .font(.caption)
                            .foregroundColor(.pkTextSecondary)
                        HStack {
                            TextField("", text: $email)
                                .keyboardType(.emailAddress)
                                .autocapitalization(.none)
                                .foregroundColor(.pkTextPrimary)
                                .accentColor(.pkPrimary)
                        }
                        .padding(.horizontal, 14)
                        .padding(.vertical, 12)
                        .background(Color.white.opacity(0.06))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .stroke(email.isEmpty ? Color.white.opacity(0.10) : Color.pkPrimary.opacity(0.5), lineWidth: 1)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    }

                    // Password field
                    VStack(alignment: .leading, spacing: 6) {
                        Label("Password", systemImage: "lock.fill")
                            .font(.caption)
                            .foregroundColor(.pkTextSecondary)
                        HStack {
                            Group {
                                if showPassword {
                                    TextField("", text: $password)
                                } else {
                                    SecureField("", text: $password)
                                }
                            }
                            .foregroundColor(.pkTextPrimary)
                            .accentColor(.pkPrimary)
                            Button(action: { showPassword.toggle() }) {
                                Image(systemName: showPassword ? "eye.slash.fill" : "eye.fill")
                                    .foregroundColor(.pkTextTertiary)
                                    .font(.system(size: 14))
                            }
                        }
                        .padding(.horizontal, 14)
                        .padding(.vertical, 12)
                        .background(Color.white.opacity(0.06))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .stroke(password.isEmpty ? Color.white.opacity(0.10) : Color.pkPrimary.opacity(0.5), lineWidth: 1)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    }

                    // Error
                    if let error = errorMessage {
                        HStack(spacing: 6) {
                            Image(systemName: "exclamationmark.triangle.fill")
                            Text(error)
                        }
                        .font(.caption)
                        .foregroundColor(.pkError)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 10)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color.pkError.opacity(0.10))
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                    }

                    // Sign in button
                    Button(action: signIn) {
                        ZStack {
                            if isLoading {
                                ProgressView()
                                    .progressViewStyle(CircularProgressViewStyle(tint: .white))
                            } else {
                                Label("Sign In", systemImage: "arrow.right.circle.fill")
                                    .font(.system(size: 16, weight: .semibold))
                                    .foregroundColor(.white)
                            }
                        }
                        .frame(maxWidth: .infinity)
                        .frame(height: 52)
                        .background(
                            canSignIn
                                ? LinearGradient.pkPrimary
                                : LinearGradient(colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)], startPoint: .leading, endPoint: .trailing)
                        )
                        .clipShape(Capsule())
                    }
                    .disabled(!canSignIn || isLoading)
                }
                .padding(24)
                .glassCard(cornerRadius: 20)
                .padding(.horizontal, 24)

                Spacer()

                // Forgot password link
                Button("Forgot password?") {
                    onNavigateToForgotPassword()
                }
                .font(.subheadline.weight(.semibold))
                .foregroundColor(.pkPrimary)
                .padding(.bottom, 8)

                // Register link
                HStack(spacing: 4) {
                    Text("Don't have an account?")
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                    Button("Create account") {
                        onNavigateToRegister()
                    }
                    .font(.subheadline.weight(.semibold))
                    .foregroundColor(.pkPrimary)
                }
                .padding(.bottom, 12)

                // Footer
                Text("Vidyuthlabs · vidyuthlabs.co.in")
                    .font(.caption2)
                    .foregroundColor(.pkTextTertiary)
                    .padding(.bottom, 32)
            }
        }
        .onAppear {
            withAnimation(.easeInOut(duration: 3).repeatForever(autoreverses: true)) {
                glowOpacity = 0.7
            }
        }
    }

    private var canSignIn: Bool {
        !email.isEmpty && password.count >= 6
    }

    private func signIn() {
        guard canSignIn else { return }
        isLoading = true
        errorMessage = nil
        Task {
            do {
                let resp = try await ParakramAPI.shared.login(email: email, password: password)
                await MainActor.run {
                    appState.token = resp.token
                    appState.userEmail = resp.email
                    appState.userName = resp.name ?? email
                    appState.currentPlan = resp.plan ?? "free"
                    appState.isLoggedIn = true
                    isLoading = false
                }
            } catch {
                await MainActor.run {
                    errorMessage = error.localizedDescription
                    isLoading = false
                }
            }
        }
    }
}

#Preview {
    LoginView()
        .environmentObject(AppState())
}
