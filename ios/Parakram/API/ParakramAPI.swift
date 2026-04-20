import Foundation

// MARK: - API Error

enum APIError: LocalizedError {
    case invalidURL
    case invalidResponse(statusCode: Int)
    case decodingError(Error)
    case networkError(Error)
    case unauthorized
    case serverError(String)

    var errorDescription: String? {
        switch self {
        case .invalidURL:
            return "Invalid URL"
        case .invalidResponse(let code):
            return "Server returned status \(code)"
        case .decodingError(let err):
            return "Failed to parse response: \(err.localizedDescription)"
        case .networkError(let err):
            return "Network error: \(err.localizedDescription)"
        case .unauthorized:
            return "Unauthorized — please sign in again"
        case .serverError(let msg):
            return "Server error: \(msg)"
        }
    }
}

// MARK: - Parakram API Client

class ParakramAPI: ObservableObject {
    static let shared = ParakramAPI()

    @Published var baseURL: String = "https://api.parakram.io"

    private var decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.keyDecodingStrategy = .convertFromSnakeCase
        return d
    }()

    private init() {}

    // MARK: - Generic Request Helper

    private func request<T: Decodable>(_ urlRequest: URLRequest) async throws -> T {
        do {
            let (data, response) = try await URLSession.shared.data(for: urlRequest)
            guard let http = response as? HTTPURLResponse else {
                throw APIError.invalidResponse(statusCode: -1)
            }
            switch http.statusCode {
            case 200...299:
                do {
                    return try decoder.decode(T.self, from: data)
                } catch {
                    throw APIError.decodingError(error)
                }
            case 401:
                throw APIError.unauthorized
            default:
                // Attempt to parse server error message
                if let errorBody = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                   let msg = errorBody["message"] as? String {
                    throw APIError.serverError(msg)
                }
                throw APIError.invalidResponse(statusCode: http.statusCode)
            }
        } catch let err as APIError {
            throw err
        } catch {
            throw APIError.networkError(error)
        }
    }

    // MARK: - Build URL Request

    private func buildRequest(
        path: String,
        method: String = "GET",
        token: String? = nil,
        body: (some Encodable)? = nil as String?
    ) throws -> URLRequest {
        guard let url = URL(string: baseURL + path) else {
            throw APIError.invalidURL
        }
        var req = URLRequest(url: url)
        req.httpMethod = method
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("Parakram-iOS/1.0", forHTTPHeaderField: "User-Agent")
        if let token = token {
            req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        if let body = body {
            req.httpBody = try JSONEncoder().encode(body)
        }
        return req
    }

    // MARK: - Auth

    func login(email: String, password: String) async throws -> LoginResponse {
        let body = LoginRequest(email: email, password: password)
        let req = try buildRequest(path: "/api/v1/auth/login", method: "POST", body: body)
        return try await request(req)
    }

    func register(username: String, email: String, password: String) async throws -> LoginResponse {
        let body = RegisterRequest(username: username, email: email, password: password)
        let req = try buildRequest(path: "/api/auth/register", method: "POST", body: body)
        return try await request(req)
    }

    func verifyEmail(username: String, code: String) async throws {
        struct Body: Encodable { let username: String; let code: String }
        let req = try buildRequest(path: "/api/auth/verify-email", method: "POST", body: Body(username: username, code: code))
        let (_, resp) = try await URLSession.shared.data(for: req)
        guard let http = resp as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw URLError(.badServerResponse)
        }
    }

    // MARK: - Device Pairing

    func pairDevice(token: String, request pairReq: PairDeviceRequest) async throws -> Device {
        let req = try buildRequest(
            path: "/api/devices/pair",
            method: "POST",
            token: token,
            body: pairReq
        )
        return try await request(req)
    }

    // MARK: - Health

    func healthCheck() async throws -> HealthResponse {
        let req = try buildRequest(path: "/health")
        return try await request(req)
    }

    // MARK: - Devices

    func listDevices(token: String) async throws -> [Device] {
        let req = try buildRequest(path: "/api/v1/devices", token: token)
        return try await request(req)
    }

    func getDevice(id: String, token: String) async throws -> Device {
        let req = try buildRequest(path: "/api/v1/devices/\(id)", token: token)
        return try await request(req)
    }

    // MARK: - Intent / Firmware Generation

    func processIntent(
        token: String,
        description: String,
        boardId: String
    ) async throws -> IntentResponse {
        let body = IntentRequest(description: description, boardId: boardId, userId: nil)
        let req = try buildRequest(
            path: "/api/v1/intent/process",
            method: "POST",
            token: token,
            body: body
        )
        return try await request(req)
    }

    func compileFirmware(
        token: String,
        intentId: String,
        boardId: String
    ) async throws -> CompileResult {
        struct CompileRequest: Encodable {
            let intentId: String
            let boardId: String
        }
        let body = CompileRequest(intentId: intentId, boardId: boardId)
        let req = try buildRequest(
            path: "/api/v1/firmware/compile",
            method: "POST",
            token: token,
            body: body
        )
        return try await request(req)
    }

    func deployFirmware(token: String, deploy: DeployRequest) async throws -> HealthResponse {
        let req = try buildRequest(
            path: "/api/v1/firmware/deploy",
            method: "POST",
            token: token,
            body: deploy
        )
        return try await request(req)
    }

    // MARK: - Marketplace / Community Drivers

    func listDrivers() async throws -> [CommunityDriver] {
        let req = try buildRequest(path: "/api/v1/marketplace/drivers")
        return try await request(req)
    }

    func searchDrivers(query: String, type: String? = nil) async throws -> [CommunityDriver] {
        var path = "/api/v1/marketplace/drivers?q=\(query.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? query)"
        if let type = type { path += "&type=\(type)" }
        let req = try buildRequest(path: path)
        return try await request(req)
    }

    func installDriver(driverId: String, token: String) async throws -> HealthResponse {
        struct InstallBody: Encodable { let driverId: String }
        let body = InstallBody(driverId: driverId)
        let req = try buildRequest(
            path: "/api/v1/marketplace/install",
            method: "POST",
            token: token,
            body: body
        )
        return try await request(req)
    }

    // MARK: - Billing

    func getBilling(token: String) async throws -> BillingUsage {
        let req = try buildRequest(path: "/api/v1/billing/usage", token: token)
        return try await request(req)
    }

    func getPlans() async throws -> [BillingPlan] {
        let req = try buildRequest(path: "/api/v1/billing/plans")
        return try await request(req)
    }

    func upgradePlan(tier: String, token: String) async throws -> HealthResponse {
        struct UpgradeBody: Encodable { let tier: String }
        let body = UpgradeBody(tier: tier)
        let req = try buildRequest(
            path: "/api/v1/billing/upgrade",
            method: "POST",
            token: token,
            body: body
        )
        return try await request(req)
    }

    // MARK: - Fleet

    func getFleetOverview(token: String) async throws -> FleetOverview {
        let req = try buildRequest(path: "/api/v1/fleet/overview", token: token)
        return try await request(req)
    }

    func getFleetDevices(token: String) async throws -> [FleetDevice] {
        let req = try buildRequest(path: "/api/v1/fleet/devices", token: token)
        return try await request(req)
    }

    // MARK: - Push token registration

    /// Register (or refresh) the device push token with the Parakram backend.
    ///
    /// - Parameters:
    ///   - platform: `"ios"` or `"android"`.
    ///   - token:    APNs hex device token or FCM registration token.
    ///   - authToken: Bearer auth token for the logged-in user.
    func setLlmKey(apiKey: String, token: String) async throws {
        struct LlmKeyBody: Encodable { let api_key: String }
        let req = try buildRequest(
            path: "/api/auth/me/llm-key",
            method: "PUT",
            token: token,
            body: LlmKeyBody(api_key: apiKey)
        )
        let (_, response) = try await URLSession.shared.data(for: req)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            let code = (response as? HTTPURLResponse)?.statusCode ?? -1
            throw APIError.invalidResponse(statusCode: code)
        }
    }

    func registerPushToken(platform: String, token: String, authToken: String) async throws {
        struct TokenBody: Encodable {
            let platform: String
            let token: String
        }
        let body = TokenBody(platform: platform, token: token)
        let req = try buildRequest(
            path: "/api/notifications/token",
            method: "POST",
            token: authToken,
            body: body
        )
        // We only care that the call succeeds; discard the response body.
        let (_, response) = try await URLSession.shared.data(for: req)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            let code = (response as? HTTPURLResponse)?.statusCode ?? -1
            throw APIError.invalidResponse(statusCode: code)
        }
    }
}
