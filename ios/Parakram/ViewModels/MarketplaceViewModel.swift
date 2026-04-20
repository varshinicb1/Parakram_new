import Foundation
import Combine

class MarketplaceViewModel: ObservableObject {
    @Published var drivers: [CommunityDriver] = []
    @Published var searchText: String = ""
    @Published var selectedType: String? = nil
    @Published var selectedBus: String? = nil
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var installedIds: Set<String> = []
    @Published var installingId: String? = nil

    private var searchTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    let typeOptions: [String] = ["sensor", "actuator", "display", "comms"]
    let busOptions: [String]  = ["i2c", "spi", "gpio", "uart", "pwm"]

    init() {
        $searchText
            .debounce(for: .milliseconds(400), scheduler: DispatchQueue.main)
            .removeDuplicates()
            .sink { [weak self] _ in self?.scheduleSearch() }
            .store(in: &cancellables)
    }

    private func scheduleSearch() {
        searchTask?.cancel()
        searchTask = Task { await self.loadDrivers() }
    }

    @MainActor
    func loadDrivers() async {
        isLoading = true
        errorMessage = nil
        do {
            let result = try await ParakramAPI.shared.listDrivers()
            drivers = result.filter { driver in
                let matchesSearch = searchText.isEmpty ||
                    driver.displayName.localizedCaseInsensitiveContains(searchText) ||
                    driver.description.localizedCaseInsensitiveContains(searchText)
                let matchesType = selectedType == nil || driver.driverType == selectedType
                let matchesBus  = selectedBus == nil  || driver.busTypes.contains(selectedBus!)
                return matchesSearch && matchesType && matchesBus
            }
        } catch {
            errorMessage = error.localizedDescription
            drivers = CommunityDriver.previews
        }
        isLoading = false
    }

    @MainActor
    func install(driver: CommunityDriver, token: String) async {
        guard !installedIds.contains(driver.id) else { return }
        installingId = driver.id
        do {
            _ = try await ParakramAPI.shared.installDriver(driverId: driver.id, token: token)
            installedIds.insert(driver.id)
        } catch {
            errorMessage = error.localizedDescription
        }
        installingId = nil
    }

    func clearFilters() {
        selectedType = nil
        selectedBus  = nil
        searchText   = ""
    }

    var hasActiveFilters: Bool {
        selectedType != nil || selectedBus != nil || !searchText.isEmpty
    }
}
