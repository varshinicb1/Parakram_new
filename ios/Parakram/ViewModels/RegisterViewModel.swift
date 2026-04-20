import Foundation

@MainActor
class RegisterViewModel: ObservableObject {
    @Published var username: String = ""
    @Published var email: String = ""
    @Published var password: String = ""
    @Published var confirmPassword: String = ""
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var success: Bool = false
    @Published var needsEmailVerification: Bool = false
    @Published var registrationResponse: LoginResponse? = nil

    // MARK: - Validation

    var usernameError: String? {
        guard !username.isEmpty else { return nil }
        let valid = username.count >= 3
            && username.count <= 32
            && username.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "_" })
        return valid ? nil : "3-32 chars, letters/numbers/underscore only"
    }

    var emailError: String? {
        guard !email.isEmpty else { return nil }
        return email.contains("@") ? nil : "Invalid email address"
    }

    var passwordError: String? {
        guard !password.isEmpty else { return nil }
        return password.count >= 8 ? nil : "At least 8 characters"
    }

    var confirmError: String? {
        guard !confirmPassword.isEmpty else { return nil }
        return confirmPassword == password ? nil : "Passwords don't match"
    }

    var canRegister: Bool {
        let usernameValid = username.count >= 3
            && username.count <= 32
            && username.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "_" })
        let passwordValid = password.count >= 8
        let emailOK = email.isEmpty || email.contains("@")
        let passwordsMatch = confirmPassword == password && !confirmPassword.isEmpty
        return usernameValid && passwordValid && emailOK && passwordsMatch && !isLoading
    }

    // MARK: - Register

    func register() async {
        guard canRegister else { return }
        isLoading = true
        errorMessage = nil
        do {
            let resp = try await ParakramAPI.shared.register(
                username: username,
                email: email,
                password: password
            )
            registrationResponse = resp
            if !email.isEmpty {
                needsEmailVerification = true
            } else {
                success = true
            }
        } catch {
            errorMessage = error.localizedDescription
        }
        isLoading = false
    }
}
