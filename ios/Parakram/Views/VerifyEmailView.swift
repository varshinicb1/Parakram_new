import SwiftUI

struct VerifyEmailView: View {
    let username: String
    var onVerified: () -> Void = {}
    var onNavigateToLogin: () -> Void = {}

    @StateObject private var vm: VerifyEmailViewModel

    init(username: String, onVerified: @escaping () -> Void = {}, onNavigateToLogin: @escaping () -> Void = {}) {
        self.username = username
        self.onVerified = onVerified
        self.onNavigateToLogin = onNavigateToLogin
        _vm = StateObject(wrappedValue: VerifyEmailViewModel(username: username))
    }

    var body: some View {
        ZStack {
            LinearGradient(
                colors: [Color(hex: "#0F0F23"), Color(hex: "#1A1040"), Color(hex: "#0F0F23")],
                startPoint: .top, endPoint: .bottom
            )
            .ignoresSafeArea()

            VStack(spacing: 28) {
                Spacer()

                // Icon
                ZStack {
                    Circle()
                        .fill(Color.pkPrimary.opacity(0.15))
                        .frame(width: 90, height: 90)
                    Image(systemName: vm.isVerified ? "checkmark.circle.fill" : "envelope.badge.fill")
                        .font(.system(size: 44))
                        .foregroundColor(vm.isVerified ? .green : Color.pkPrimary)
                        .animation(.spring(), value: vm.isVerified)
                }

                VStack(spacing: 8) {
                    Text("Verify your email")
                        .font(.title.bold())
                        .foregroundColor(.white)
                    Text("Enter the 6-digit code sent to your email address.")
                        .font(.subheadline)
                        .foregroundColor(.white.opacity(0.6))
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)
                }

                // Card
                VStack(spacing: 20) {
                    if let err = vm.errorMessage {
                        Text(err)
                            .font(.caption)
                            .foregroundColor(Color(hex: "#FF6B6B"))
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 8)
                    }

                    pkTextField(
                        title: "6-digit code",
                        text: $vm.code,
                        keyboardType: .numberPad
                    )
                    .onChange(of: vm.code) { newVal in
                        vm.code = String(newVal.filter(\.isNumber).prefix(6))
                    }

                    Button {
                        Task { await vm.verify() }
                    } label: {
                        Group {
                            if vm.isLoading {
                                ProgressView().tint(.white)
                            } else {
                                Text("Verify Email").fontWeight(.semibold)
                            }
                        }
                        .frame(maxWidth: .infinity)
                        .frame(height: 52)
                    }
                    .background(vm.canVerify ? Color.pkPrimary : Color.pkPrimary.opacity(0.4))
                    .foregroundColor(.white)
                    .cornerRadius(14)
                    .disabled(!vm.canVerify)
                    .animation(.easeInOut(duration: 0.2), value: vm.canVerify)
                }
                .padding(24)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 20))
                .padding(.horizontal, 24)

                Button("Back to sign in", action: onNavigateToLogin)
                    .font(.subheadline)
                    .foregroundColor(Color.pkSecondary)

                Spacer()
            }
        }
        .onChange(of: vm.isVerified) { verified in
            if verified { onVerified() }
        }
    }

    @ViewBuilder
    private func pkTextField(title: String, text: Binding<String>, keyboardType: UIKeyboardType = .default) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.caption)
                .foregroundColor(.white.opacity(0.6))
            TextField(title, text: text)
                .keyboardType(keyboardType)
                .padding(12)
                .background(Color.white.opacity(0.08))
                .cornerRadius(10)
                .foregroundColor(.white)
                .tint(Color.pkPrimary)
        }
    }
}

// Hex color helper (same pattern as other views)
private extension Color {
    init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: hex).scanHexInt64(&int)
        let r = Double((int >> 16) & 0xFF) / 255
        let g = Double((int >> 8) & 0xFF) / 255
        let b = Double(int & 0xFF) / 255
        self.init(red: r, green: g, blue: b)
    }
}

#Preview {
    VerifyEmailView(username: "testuser")
}
