import Foundation

@MainActor
class VerifyEmailViewModel: ObservableObject {
    @Published var code: String = ""
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var isVerified: Bool = false

    private let username: String

    init(username: String) {
        self.username = username
    }

    var canVerify: Bool {
        code.count == 6 && code.allSatisfy(\.isNumber) && !isLoading
    }

    func verify() async {
        guard canVerify else { return }
        isLoading = true
        errorMessage = nil
        do {
            try await ParakramAPI.shared.verifyEmail(username: username, code: code)
            isVerified = true
        } catch {
            errorMessage = "Invalid or expired code. Check your email and try again."
        }
        isLoading = false
    }
}
