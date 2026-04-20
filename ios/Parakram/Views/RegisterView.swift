import SwiftUI

struct RegisterView: View {
    @EnvironmentObject var appState: AppState
    var onNavigateToLogin: () -> Void = {}
    var onNeedsVerification: (String) -> Void = { _ in }

    @StateObject private var vm = RegisterViewModel()
    @State private var showPassword: Bool = false
    @State private var showConfirmPassword: Bool = false
    @State private var glowOpacity: Double = 0.4

    var body: some View {
        ZStack {
            // Background
            Color.pkBackground.ignoresSafeArea()

            // Ambient glow circles
            Circle()
                .fill(RadialGradient.pkGlow(color: .pkSecondary))
                .frame(width: 400, height: 400)
                .offset(x: 100, y: -200)
                .opacity(glowOpacity)

            Circle()
                .fill(RadialGradient.pkGlow(color: .pkPrimary))
                .frame(width: 300, height: 300)
                .offset(x: -120, y: 200)
                .opacity(glowOpacity * 0.6)

            VStack(spacing: 0) {
                Spacer()

                if vm.success {
                    SuccessCard()
                        .padding(.horizontal, 24)
                        .onAppear {
                            // Navigate after 1.5s
                            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                                if let resp = vm.registrationResponse {
                                    appState.token       = resp.token
                                    appState.userEmail   = resp.email
                                    appState.userName    = resp.name ?? vm.username
                                    appState.currentPlan = resp.plan ?? "free"
                                }
                                appState.isLoggedIn = true
                            }
                        }
                } else {
                    // Glass card
                    VStack(spacing: 20) {
                        // Title
                        VStack(spacing: 6) {
                            Text("Create Account")
                                .font(.system(size: 26, weight: .bold))
                                .foregroundColor(.pkTextPrimary)
                            Text("Join Vidyuthlabs")
                                .font(.subheadline)
                                .foregroundColor(.pkTextSecondary)
                        }
                        .padding(.top, 4)

                        // Username field
                        FieldGroup(label: "Username", icon: "person.fill") {
                            TextField("", text: $vm.username)
                                .autocapitalization(.none)
                                .disableAutocorrection(true)
                                .foregroundColor(.pkTextPrimary)
                                .accentColor(.pkPrimary)
                        }
                        .fieldBorder(isEmpty: vm.username.isEmpty, hasError: vm.usernameError != nil)
                        if let err = vm.usernameError {
                            ValidationError(message: err)
                        }

                        // Email field
                        FieldGroup(label: "Email (optional)", icon: "envelope.fill") {
                            TextField("", text: $vm.email)
                                .keyboardType(.emailAddress)
                                .autocapitalization(.none)
                                .foregroundColor(.pkTextPrimary)
                                .accentColor(.pkPrimary)
                        }
                        .fieldBorder(isEmpty: vm.email.isEmpty, hasError: vm.emailError != nil)
                        if let err = vm.emailError {
                            ValidationError(message: err)
                        }

                        // Password field
                        FieldGroup(label: "Password", icon: "lock.fill") {
                            Group {
                                if showPassword {
                                    TextField("", text: $vm.password)
                                } else {
                                    SecureField("", text: $vm.password)
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
                        .fieldBorder(isEmpty: vm.password.isEmpty, hasError: vm.passwordError != nil)
                        if let err = vm.passwordError {
                            ValidationError(message: err)
                        }

                        // Confirm password field
                        FieldGroup(label: "Confirm Password", icon: "lock.fill") {
                            Group {
                                if showConfirmPassword {
                                    TextField("", text: $vm.confirmPassword)
                                } else {
                                    SecureField("", text: $vm.confirmPassword)
                                }
                            }
                            .foregroundColor(.pkTextPrimary)
                            .accentColor(.pkPrimary)
                            Button(action: { showConfirmPassword.toggle() }) {
                                Image(systemName: showConfirmPassword ? "eye.slash.fill" : "eye.fill")
                                    .foregroundColor(.pkTextTertiary)
                                    .font(.system(size: 14))
                            }
                        }
                        .fieldBorder(isEmpty: vm.confirmPassword.isEmpty, hasError: vm.confirmError != nil)
                        if let err = vm.confirmError {
                            ValidationError(message: err)
                        }

                        // Error banner
                        if let error = vm.errorMessage {
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

                        // Create Account button
                        Button(action: {
                            Task { await vm.register() }
                        }) {
                            ZStack {
                                if vm.isLoading {
                                    ProgressView()
                                        .progressViewStyle(CircularProgressViewStyle(tint: .white))
                                } else {
                                    Label("Create Account", systemImage: "person.badge.plus")
                                        .font(.system(size: 16, weight: .semibold))
                                        .foregroundColor(.white)
                                }
                            }
                            .frame(maxWidth: .infinity)
                            .frame(height: 52)
                            .background(
                                vm.canRegister
                                    ? LinearGradient.pkPrimary
                                    : LinearGradient(
                                        colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)],
                                        startPoint: .leading,
                                        endPoint: .trailing
                                    )
                            )
                            .clipShape(Capsule())
                        }
                        .disabled(!vm.canRegister || vm.isLoading)
                    }
                    .padding(24)
                    .glassCard(cornerRadius: 20)
                    .padding(.horizontal, 24)
                }

                Spacer()

                // Footer link
                HStack(spacing: 4) {
                    Text("Already have an account?")
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                    Button("Sign in") {
                        onNavigateToLogin()
                    }
                    .font(.subheadline.weight(.semibold))
                    .foregroundColor(.pkPrimary)
                }
                .padding(.bottom, 32)
            }
        }
        .onAppear {
            withAnimation(.easeInOut(duration: 3).repeatForever(autoreverses: true)) {
                glowOpacity = 0.7
            }
        }
        .onChange(of: vm.needsEmailVerification) { needs in
            if needs { onNeedsVerification(vm.username) }
        }
    }
}

// MARK: - Success Card

private struct SuccessCard: View {
    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 72))
                .foregroundColor(.pkSuccess)

            Text("Account Created!")
                .font(.system(size: 26, weight: .bold))
                .foregroundColor(.pkTextPrimary)

            Text("Signing you in…")
                .font(.subheadline)
                .foregroundColor(.pkTextSecondary)

            ProgressView()
                .progressViewStyle(CircularProgressViewStyle(tint: .pkPrimary))
        }
        .padding(40)
        .glassCard(cornerRadius: 20)
    }
}

// MARK: - Field Group helper

private struct FieldGroup<Content: View>: View {
    let label: String
    let icon: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(label, systemImage: icon)
                .font(.caption)
                .foregroundColor(.pkTextSecondary)
            HStack {
                content()
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 12)
            .background(Color.white.opacity(0.06))
        }
    }
}

// MARK: - Field border modifier

private extension View {
    func fieldBorder(isEmpty: Bool, hasError: Bool) -> some View {
        self.overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(
                    hasError
                        ? Color.pkError.opacity(0.7)
                        : (isEmpty ? Color.white.opacity(0.10) : Color.pkPrimary.opacity(0.5)),
                    lineWidth: 1
                )
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - Validation Error label

private struct ValidationError: View {
    let message: String

    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: "exclamationmark.circle")
                .font(.system(size: 11))
            Text(message)
                .font(.caption2)
        }
        .foregroundColor(.pkError)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.top, -8)
    }
}

#Preview {
    RegisterView(onNavigateToLogin: {})
        .environmentObject(AppState())
}
