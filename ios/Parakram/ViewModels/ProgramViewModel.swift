import Foundation
import Combine

// MARK: - Program Step

enum ProgramStep: Int, CaseIterable {
    case describe = 0
    case generate
    case compile
    case deploy

    var title: String {
        switch self {
        case .describe:  return "Describe"
        case .generate:  return "Generate"
        case .compile:   return "Compile"
        case .deploy:    return "Deploy"
        }
    }

    var icon: String {
        switch self {
        case .describe:  return "text.bubble.fill"
        case .generate:  return "wand.and.stars"
        case .compile:   return "hammer.fill"
        case .deploy:    return "bolt.fill"
        }
    }
}

// MARK: - ProgramViewModel

class ProgramViewModel: ObservableObject {
    @Published var currentStep: ProgramStep = .describe
    @Published var descriptionText: String = ""
    @Published var selectedBoard: String = "esp32s3"
    @Published var intentResponse: IntentResponse? = nil
    @Published var compileResult: CompileResult? = nil
    @Published var deploymentState: DeploymentState = .idle
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var successMessage: String? = nil
    @Published var progressFraction: Double = 0.0

    let boardOptions: [(id: String, name: String)] = [
        ("esp32s3", "ESP32-S3"),
        ("rp2040",  "Raspberry Pi Pico"),
        ("stm32f4", "STM32F4"),
        ("arduino", "Arduino Mega"),
    ]

    let examplePrompts: [String] = [
        "Blink an LED every 500ms",
        "Read temperature and send over BLE",
        "Motor forward on button press",
        "Log soil moisture every 5 minutes",
        "Turn servo 90° when ultrasonic < 20cm",
    ]

    var canGenerate: Bool {
        descriptionText.trimmingCharacters(in: .whitespaces).count >= 10
    }

    var irSummary: String? { intentResponse?.preview.sensors.first.map { "Uses \($0)" } }

    // MARK: - Actions

    @MainActor
    func generate(token: String) async {
        guard canGenerate else { return }
        isLoading = true
        errorMessage = nil
        do {
            let resp = try await ParakramAPI.shared.processIntent(
                token: token,
                description: descriptionText,
                boardId: selectedBoard
            )
            intentResponse = resp
            withAnimation { currentStep = .generate }
        } catch {
            errorMessage = error.localizedDescription
        }
        isLoading = false
    }

    @MainActor
    func compile(token: String) async {
        guard let intent = intentResponse else { return }
        isLoading = true
        errorMessage = nil
        deploymentState = .compiling
        progressFraction = 0.2
        do {
            let result = try await ParakramAPI.shared.compileFirmware(
                token: token,
                intentId: intent.intentId,
                boardId: selectedBoard
            )
            compileResult = result
            progressFraction = 0.5
            deploymentState = .compiled
            withAnimation { currentStep = .compile }
        } catch {
            errorMessage = error.localizedDescription
            deploymentState = .error(error.localizedDescription)
            progressFraction = 0.0
        }
        isLoading = false
    }

    @MainActor
    func deploy(token: String, deviceId: String) async {
        guard let intent = intentResponse else { return }
        isLoading = true
        errorMessage = nil
        deploymentState = .transferring
        progressFraction = 0.65
        do {
            let req = DeployRequest(intentId: intent.intentId, deviceId: deviceId, method: "wifi")
            _ = try await ParakramAPI.shared.deployFirmware(token: token, deploy: req)
            progressFraction = 0.9
            deploymentState = .verifying
            try? await Task.sleep(nanoseconds: 800_000_000)
            progressFraction = 1.0
            deploymentState = .running
            successMessage = "Program is running on your device!"
            withAnimation { currentStep = .deploy }
        } catch {
            errorMessage = error.localizedDescription
            deploymentState = .error(error.localizedDescription)
            progressFraction = 0.0
        }
        isLoading = false
    }

    func reset() {
        currentStep = .describe
        descriptionText = ""
        intentResponse = nil
        compileResult = nil
        deploymentState = .idle
        isLoading = false
        errorMessage = nil
        successMessage = nil
        progressFraction = 0.0
    }
}
