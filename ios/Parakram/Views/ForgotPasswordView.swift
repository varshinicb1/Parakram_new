import SwiftUI

struct ForgotPasswordView: View {
    var onNavigateToLogin: () -> Void = {}
    @StateObject private var vm = ForgotPasswordViewModel()
    @State private var showNewPass = false
    @State private var showConfirmPass = false

    var body: some View {
        ZStack {
            Color.pkBackground.ignoresSafeArea()

            // Ambient orbs
            Circle()
                .fill(RadialGradient.pkGlow(color: .pkPrimary))
                .frame(width: 380, height: 380)
                .offset(x: -100, y: -180)
                .opacity(0.5)

            Circle()
                .fill(RadialGradient.pkGlow(color: .pkSecondary))
                .frame(width: 280, height: 280)
                .offset(x: 130, y: 200)
                .opacity(0.3)

            ScrollView {
                VStack(spacing: 0) {
                    Spacer().frame(height: 60)

                    // Logo
                    ZStack {
                        RoundedRectangle(cornerRadius: 18)
                            .fill(LinearGradient.pkPrimary)
                            .frame(width: 64, height: 64)
                        Image(systemName: "lock.rotation")
                            .font(.system(size: 30, weight: .bold))
                            .foregroundColor(.white)
                    }

                    Spacer().frame(height: 20)

                    Text("Reset Password")
                        .font(.system(size: 28, weight: .bold))
                        .foregroundColor(.pkTextPrimary)

                    Text("Verify your identity with a 6-digit code")
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                        .multilineTextAlignment(.center)
                        .padding(.top, 6)
                        .padding(.horizontal, 32)

                    Spacer().frame(height: 36)

                    // Glass card
                    VStack(spacing: 0) {
                        // Step indicator
                        HStack(spacing: 0) {
                            ForEach(0..<3) { idx in
                                let active = stepIndex >= idx
                                Circle()
                                    .fill(active ? Color.pkPrimary : Color.white.opacity(0.1))
                                    .frame(width: 10, height: 10)
                                if idx < 2 {
                                    Rectangle()
                                        .fill(stepIndex > idx ? Color.pkPrimary : Color.white.opacity(0.1))
                                        .frame(height: 2)
                                        .frame(maxWidth: .infinity)
                                }
                            }
                        }
                        .padding(.horizontal, 24)
                        .padding(.top, 24)
                        .padding(.bottom, 20)

                        Divider().opacity(0.08)

                        // Step content
                        Group {
                            switch vm.step {
                            case .enterUsername:
                                usernameStep
                            case .enterCode:
                                codeStep
                            case .success:
                                successStep
                            }
                        }
                        .padding(24)
                    }
                    .background(.white.opacity(0.04))
                    .overlay(
                        RoundedRectangle(cornerRadius: 20)
                            .stroke(Color.white.opacity(0.07), lineWidth: 1)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 20))
                    .padding(.horizontal, 24)

                    // Error
                    if let err = vm.errorMessage {
                        HStack(spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                            Text(err).font(.footnote)
                        }
                        .foregroundColor(.pkError)
                        .padding(.top, 12)
                        .padding(.horizontal, 24)
                    }

                    Spacer().frame(height: 24)

                    Button("Back to Login") { onNavigateToLogin() }
                        .font(.subheadline.weight(.semibold))
                        .foregroundColor(.pkPrimary)

                    Spacer().frame(height: 40)
                }
            }
        }
    }

    private var stepIndex: Int {
        switch vm.step {
        case .enterUsername: return 0
        case .enterCode:     return 1
        case .success:       return 2
        }
    }

    // MARK: - Username Step

    private var usernameStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Enter your username")
                .font(.headline)
                .foregroundColor(.pkTextPrimary)

            pkTextField("Username", text: $vm.username, icon: "person")

            pkButton("Send Reset Code", disabled: !vm.canRequestReset || vm.isLoading) {
                vm.requestReset()
            }
        }
    }

    // MARK: - Code Step

    private var codeStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Check server logs for your 6-digit code")
                .font(.subheadline)
                .foregroundColor(.pkTextSecondary)

            pkTextField("6-digit Code", text: $vm.code, icon: "number", keyboard: .numberPad)

            pkSecureField("New Password", text: $vm.newPassword, show: $showNewPass, icon: "lock")

            pkSecureField("Confirm Password", text: $vm.confirmPassword, show: $showConfirmPass, icon: "lock.fill")

            pkButton("Reset Password", disabled: !vm.canSubmitReset || vm.isLoading) {
                vm.submitReset()
            }
        }
    }

    // MARK: - Success Step

    private var successStep: some View {
        VStack(spacing: 20) {
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 64))
                .foregroundColor(.pkSuccess)

            VStack(spacing: 8) {
                Text("Password Updated!")
                    .font(.title2.bold())
                    .foregroundColor(.pkTextPrimary)
                Text("You can now log in with your new password.")
                    .font(.subheadline)
                    .foregroundColor(.pkTextSecondary)
                    .multilineTextAlignment(.center)
            }

            pkButton("Go to Login", disabled: false) {
                onNavigateToLogin()
            }
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Helpers

    @ViewBuilder
    private func pkTextField(
        _ placeholder: String,
        text: Binding<String>,
        icon: String,
        keyboard: UIKeyboardType = .default
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .foregroundColor(.pkTextSecondary)
                .frame(width: 20)
            TextField(placeholder, text: text)
                .keyboardType(keyboard)
                .autocorrectionDisabled()
                .autocapitalization(.none)
                .foregroundColor(.pkTextPrimary)
        }
        .padding(14)
        .background(Color.white.opacity(0.05))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(Color.pkPrimary.opacity(0.3), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    @ViewBuilder
    private func pkSecureField(
        _ placeholder: String,
        text: Binding<String>,
        show: Binding<Bool>,
        icon: String
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .foregroundColor(.pkTextSecondary)
                .frame(width: 20)
            if show.wrappedValue {
                TextField(placeholder, text: text)
                    .autocorrectionDisabled()
                    .autocapitalization(.none)
                    .foregroundColor(.pkTextPrimary)
            } else {
                SecureField(placeholder, text: text)
                    .foregroundColor(.pkTextPrimary)
            }
            Button {
                show.wrappedValue.toggle()
            } label: {
                Image(systemName: show.wrappedValue ? "eye.slash" : "eye")
                    .foregroundColor(.pkTextSecondary)
            }
        }
        .padding(14)
        .background(Color.white.opacity(0.05))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(Color.pkPrimary.opacity(0.3), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    @ViewBuilder
    private func pkButton(_ title: String, disabled: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            ZStack {
                if vm.isLoading {
                    ProgressView().tint(.white)
                } else {
                    Text(title)
                        .font(.headline)
                        .foregroundColor(.white)
                }
            }
            .frame(maxWidth: .infinity)
            .frame(height: 52)
            .background(
                LinearGradient.pkPrimary
                    .opacity(disabled ? 0.4 : 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 14))
        }
        .disabled(disabled)
    }
}
