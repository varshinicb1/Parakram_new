import Foundation

@MainActor
class FleetViewModel: ObservableObject {
    @Published var overview: FleetOverview? = nil
    @Published var devices: [FleetDevice] = []
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil

    func load(token: String) async {
        guard !isLoading else { return }
        isLoading = true
        errorMessage = nil

        do {
            async let overviewTask = ParakramAPI.shared.getFleetOverview(token: token)
            async let devicesTask  = ParakramAPI.shared.getFleetDevices(token: token)

            let (ov, devs) = try await (overviewTask, devicesTask)
            overview = ov
            devices  = devs
        } catch {
            errorMessage = error.localizedDescription
        }

        isLoading = false
    }
}
