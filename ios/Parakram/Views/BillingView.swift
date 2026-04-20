import SwiftUI

struct BillingView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = BillingViewModel()

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    // Current plan hero card
                    if let _ = vm.usage {
                        CurrentPlanCard(planName: vm.displayPlanName, plan: appState.currentPlan)
                    }

                    // Usage meters
                    if let usage = vm.usage {
                        UsageSection(usage: usage)
                    }

                    // Plan comparison
                    if !vm.plans.isEmpty {
                        PlansSection(plans: vm.plans, currentTier: appState.currentPlan) { tier in
                            Task { await vm.upgradePlan(tier: tier, token: appState.token) }
                        }
                    }

                    if let err = vm.errorMessage {
                        ErrorBanner(message: err).padding(.horizontal, 16)
                    }
                }
                .padding(.vertical, 16)
            }
            .background(Color.pkBackground.ignoresSafeArea())
            .navigationTitle("My Plan")
            .navigationBarTitleDisplayMode(.large)
            .overlay {
                if vm.isLoading {
                    ProgressView()
                        .scaleEffect(1.4)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(Color.black.opacity(0.3))
                }
            }
            .task { await vm.load(token: appState.token) }
        }
    }
}

// MARK: - Current Plan Card

private struct CurrentPlanCard: View {
    let planName: String
    let plan: String

    var planColor: Color {
        switch plan {
        case "pro":    return .pkPrimary
        case "team":   return .pkSecondary
        case "enterprise": return .pkTertiary
        default:       return Color.white.opacity(0.5)
        }
    }

    var body: some View {
        ZStack(alignment: .bottomTrailing) {
            RoundedRectangle(cornerRadius: 20)
                .fill(LinearGradient(colors: [planColor.opacity(0.8), planColor.opacity(0.4)], startPoint: .topLeading, endPoint: .bottomTrailing))

            Image(systemName: "creditcard.fill")
                .font(.system(size: 90))
                .foregroundColor(.white.opacity(0.07))
                .offset(x: 20, y: 20)

            VStack(alignment: .leading, spacing: 6) {
                Text("Current Plan")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundColor(.white.opacity(0.7))
                Text(planName)
                    .font(.system(size: 28, weight: .bold))
                    .foregroundColor(.white)
                HStack(spacing: 6) {
                    Circle()
                        .fill(Color.pkSuccess)
                        .frame(width: 7, height: 7)
                    Text("Active")
                        .font(.caption)
                        .foregroundColor(.white.opacity(0.85))
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(height: 130)
        .padding(.horizontal, 16)
    }
}

// MARK: - Usage Section

private struct UsageSection: View {
    let usage: BillingUsage

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            SectionHeader("This Month's Usage")
                .padding(.horizontal, 16)

            VStack(spacing: 10) {
                UsageRow(label: "AI Requests", icon: "wand.and.stars", counter: usage.llmIntents)
                UsageRow(label: "Compiles", icon: "hammer.fill", counter: usage.compiles)
                UsageRow(label: "Deploys", icon: "bolt.fill", counter: usage.deploys)
                UsageRow(label: "Active Devices", icon: "cpu", counter: usage.devicesActive)
            }
            .padding(.horizontal, 16)
        }
    }
}

private struct UsageRow: View {
    let label: String
    let icon: String
    let counter: UsageCounter

    var barColor: Color {
        if counter.fraction >= 0.9 { return .pkError }
        if counter.fraction >= 0.7 { return .pkWarning }
        return .pkSuccess
    }

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Image(systemName: icon)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(barColor)
                    .frame(width: 20)
                Text(label)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.pkTextPrimary)
                Spacer()
                Text("\(counter.used) / \(counter.limit == Int.max ? "∞" : "\(counter.limit)")")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(barColor)
            }
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3)
                        .fill(Color.white.opacity(0.08))
                        .frame(height: 6)
                    RoundedRectangle(cornerRadius: 3)
                        .fill(barColor)
                        .frame(width: geo.size.width * counter.fraction, height: 6)
                        .animation(.easeOut(duration: 0.6), value: counter.fraction)
                }
            }
            .frame(height: 6)
        }
        .padding(14)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - Plans Section

private struct PlansSection: View {
    let plans: [BillingPlan]
    let currentTier: String
    let onUpgrade: (String) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            SectionHeader("Plans")
                .padding(.horizontal, 16)

            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 12) {
                    ForEach(plans) { plan in
                        PlanCard(
                            plan: plan,
                            isCurrent: plan.tier == currentTier,
                            onUpgrade: { onUpgrade(plan.tier) }
                        )
                        .frame(width: 200)
                    }
                }
                .padding(.horizontal, 16)
            }
        }
    }
}

private struct PlanCard: View {
    let plan: BillingPlan
    let isCurrent: Bool
    let onUpgrade: () -> Void

    var planGradient: LinearGradient {
        switch plan.tier {
        case "pro":  return .pkPrimary
        case "team": return .pkAccent
        default:     return LinearGradient(colors: [Color.pkSurface, Color.pkSurface], startPoint: .top, endPoint: .bottom)
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(plan.displayName)
                    .font(.system(size: 16, weight: .bold))
                    .foregroundColor(.pkTextPrimary)
                Spacer()
                if isCurrent {
                    Text("Current")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.pkSuccess)
                        .padding(.horizontal, 7)
                        .padding(.vertical, 3)
                        .background(Color.pkSuccess.opacity(0.12))
                        .clipShape(Capsule())
                }
            }

            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(plan.monthlyPriceUsd == 0 ? "Free" : "$\(Int(plan.monthlyPriceUsd))")
                    .font(.system(size: 26, weight: .bold))
                    .foregroundColor(.pkTextPrimary)
                if plan.monthlyPriceUsd > 0 {
                    Text("/mo").font(.caption).foregroundColor(.pkTextTertiary)
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                ForEach(plan.features.prefix(4), id: \.self) { feature in
                    HStack(spacing: 6) {
                        Image(systemName: "checkmark.circle.fill")
                            .font(.system(size: 12))
                            .foregroundColor(.pkSuccess)
                        Text(feature)
                            .font(.caption)
                            .foregroundColor(.pkTextSecondary)
                    }
                }
            }

            Spacer()

            if !isCurrent {
                Button(action: onUpgrade) {
                    Text("Upgrade")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundColor(.white)
                        .frame(maxWidth: .infinity, minHeight: 36)
                        .background(planGradient)
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                }
            } else {
                Text("Your current plan")
                    .font(.system(size: 12))
                    .foregroundColor(.pkTextTertiary)
                    .frame(maxWidth: .infinity, alignment: .center)
            }
        }
        .padding(16)
        .frame(minHeight: 220)
        .background(Color.pkSurface)
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(isCurrent ? Color.pkPrimary.opacity(0.5) : Color.white.opacity(0.06), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

#Preview {
    BillingView().environmentObject({
        let s = AppState(); s.currentPlan = "free"; return s
    }())
}
