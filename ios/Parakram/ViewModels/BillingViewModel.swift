import Foundation

class BillingViewModel: ObservableObject {
    @Published var usage: BillingUsage? = nil
    @Published var plans: [BillingPlan] = []
    @Published var currentPlan: String = "free"
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil
    @Published var isUpgrading: Bool = false
    @Published var upgradeSuccess: Bool = false

    @MainActor
    func load(token: String) async {
        isLoading = true
        errorMessage = nil
        async let usageResult = ParakramAPI.shared.getBilling(token: token)
        async let plansResult = ParakramAPI.shared.getPlans()
        do {
            usage = try await usageResult
        } catch {
            usage = BillingUsage.preview
        }
        do {
            plans = try await plansResult
        } catch {
            plans = BillingPlan.previews
        }
        isLoading = false
    }

    @MainActor
    func upgradePlan(tier: String, token: String) async {
        isUpgrading = true
        do {
            _ = try await ParakramAPI.shared.upgradePlan(tier: tier, token: token)
            currentPlan = tier
            upgradeSuccess = true
        } catch {
            errorMessage = error.localizedDescription
        }
        isUpgrading = false
    }

    var displayPlanName: String {
        plans.first(where: { $0.tier == currentPlan })?.displayName ?? currentPlan.capitalized
    }
}
