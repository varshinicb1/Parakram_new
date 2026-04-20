import Foundation
import Combine

enum ForgotPasswordStep {
    case enterUsername
    case enterCode(username: String)
    case success
}

@MainActor
class ForgotPasswordViewModel: ObservableObject {
    @Published var step: ForgotPasswordStep = .enterUsername
    @Published var username: String = ""
    @Published var code: String = ""
    @Published var newPassword: String = ""
    @Published var confirmPassword: String = ""
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil

    var canRequestReset: Bool { username.trimmingCharacters(in: .whitespaces).count >= 3 }
    var canSubmitReset: Bool { code.count == 6 && newPassword.count >= 8 && newPassword == confirmPassword }

    private let baseURL: String = UserDefaults.standard.string(forKey: "baseURL") ?? "http://localhost:8400"

    func requestReset() {
        let trimmed = username.trimmingCharacters(in: .whitespaces)
        guard trimmed.count >= 3 else {
            errorMessage = "Enter your username"
            return
        }
        isLoading = true
        errorMessage = nil

        Task {
            do {
                guard let url = URL(string: "\(baseURL)/api/auth/forgot-password") else { return }
                var req = URLRequest(url: url)
                req.httpMethod = "POST"
                req.setValue("application/json", forHTTPHeaderField: "Content-Type")
                req.httpBody = try JSONSerialization.data(withJSONObject: ["username": trimmed])

                let (_, resp) = try await URLSession.shared.data(for: req)
                let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
                if code == 200 {
                    step = .enterCode(username: trimmed)
                } else {
                    errorMessage = "Request failed (\(code))"
                }
            } catch {
                errorMessage = error.localizedDescription
            }
            isLoading = false
        }
    }

    func submitReset() {
        guard case .enterCode(let uname) = step else { return }
        guard newPassword == confirmPassword else {
            errorMessage = "Passwords do not match"
            return
        }
        guard newPassword.count >= 8 else {
            errorMessage = "Password must be at least 8 characters"
            return
        }
        isLoading = true
        errorMessage = nil

        Task {
            do {
                guard let url = URL(string: "\(baseURL)/api/auth/reset-password") else { return }
                var req = URLRequest(url: url)
                req.httpMethod = "POST"
                req.setValue("application/json", forHTTPHeaderField: "Content-Type")
                req.httpBody = try JSONSerialization.data(withJSONObject: [
                    "username": uname,
                    "code": code,
                    "new_password": newPassword,
                ])

                let (_, resp) = try await URLSession.shared.data(for: req)
                let statusCode = (resp as? HTTPURLResponse)?.statusCode ?? 0
                if statusCode == 200 {
                    step = .success
                } else {
                    errorMessage = "Invalid or expired code"
                }
            } catch {
                errorMessage = error.localizedDescription
            }
            isLoading = false
        }
    }
}
