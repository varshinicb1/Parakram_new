import SwiftUI

struct HomeView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = HomeViewModel()

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 0) {
                    // Custom header
                    HomeHeader(greeting: vm.greetingText, plan: appState.currentPlan)
                        .padding(.horizontal, 20)
                        .padding(.top, 8)
                        .padding(.bottom, 20)

                    // Hero card
                    HeroCard(
                        isOnline: vm.backendOnline,
                        deviceCount: vm.deviceCount,
                        programCount: vm.programCount
                    )
                    .padding(.horizontal, 16)

                    // Quick actions
                    VStack(alignment: .leading, spacing: 12) {
                        SectionHeader("Quick Actions")
                            .padding(.horizontal, 16)

                        LazyVGrid(
                            columns: [GridItem(.flexible()), GridItem(.flexible())],
                            spacing: 12
                        ) {
                            QuickActionCard(
                                icon: "bolt.fill",
                                label: "New Program",
                                gradient: LinearGradient.pkPrimary
                            )
                            QuickActionCard(
                                icon: "antenna.radiowaves.left.and.right",
                                label: "My Devices",
                                gradient: LinearGradient.pkAccent
                            )
                            QuickActionCard(
                                icon: "storefront.fill",
                                label: "Marketplace",
                                gradient: LinearGradient.pkTertiary
                            )
                            QuickActionCard(
                                icon: "creditcard.fill",
                                label: "My Plan",
                                gradient: LinearGradient.pkWarning
                            )
                        }
                        .padding(.horizontal, 16)
                    }
                    .padding(.top, 28)

                    // Recent activity
                    VStack(alignment: .leading, spacing: 12) {
                        SectionHeader("Recent")
                            .padding(.horizontal, 16)

                        if vm.recentActivity.isEmpty {
                            EmptyActivity()
                                .padding(.horizontal, 16)
                        } else {
                            ForEach(vm.recentActivity) { item in
                                ActivityRowView(item: item)
                                    .padding(.horizontal, 16)
                            }
                        }
                    }
                    .padding(.top, 28)
                    .padding(.bottom, 32)
                }
            }
            .background(Color.pkBackground.ignoresSafeArea())
            .navigationBarHidden(true)
            .task { await vm.refresh(token: appState.token) }
        }
    }
}

// MARK: - Home Header

private struct HomeHeader: View {
    let greeting: String
    let plan: String

    var body: some View {
        HStack(alignment: .top) {
            VStack(alignment: .leading, spacing: 2) {
                Text("\(greeting) ⚡")
                    .font(.subheadline)
                    .foregroundColor(.pkTextSecondary)
                Text("Parakram")
                    .font(.system(size: 30, weight: .bold))
                    .foregroundColor(.pkTextPrimary)
            }
            Spacer()
            ZStack {
                Circle()
                    .fill(LinearGradient.pkPrimary)
                    .frame(width: 44, height: 44)
                Image(systemName: "person.fill")
                    .foregroundColor(.white)
                    .font(.system(size: 20))
            }
        }
    }
}

// MARK: - Hero Card

private struct HeroCard: View {
    let isOnline: Bool
    let deviceCount: Int
    let programCount: Int

    @State private var dotScale: CGFloat = 1.0

    var body: some View {
        ZStack(alignment: .bottomTrailing) {
            RoundedRectangle(cornerRadius: 20)
                .fill(LinearGradient.pkHero)

            // Background bolt
            Image(systemName: "bolt.fill")
                .font(.system(size: 120))
                .foregroundColor(.white.opacity(0.06))
                .offset(x: 30, y: 30)

            VStack(alignment: .leading, spacing: 12) {
                Text("Zero-Code Hardware")
                    .font(.system(size: 22, weight: .bold))
                    .foregroundColor(.white)
                Text("Describe it. We deploy it.")
                    .font(.subheadline)
                    .foregroundColor(.white.opacity(0.7))

                Spacer().frame(height: 4)

                HStack(spacing: 16) {
                    // Online badge
                    HStack(spacing: 6) {
                        Circle()
                            .fill(isOnline ? Color.pkSuccess : Color.pkError)
                            .frame(width: 8, height: 8)
                            .scaleEffect(isOnline ? dotScale : 1.0)
                        Text(isOnline ? "Backend Online" : "Offline")
                            .font(.caption)
                            .fontWeight(.medium)
                            .foregroundColor(.white)
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Color.white.opacity(0.18))
                    .clipShape(Capsule())

                    HeroStat(count: deviceCount, label: "devices")
                    HeroStat(count: programCount, label: "programs")
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(height: 160)
        .onAppear {
            withAnimation(.easeInOut(duration: 0.9).repeatForever(autoreverses: true)) {
                dotScale = 1.4
            }
        }
    }
}

private struct HeroStat: View {
    let count: Int
    let label: String
    var body: some View {
        HStack(spacing: 4) {
            Text("\(count)")
                .font(.system(size: 16, weight: .bold))
                .foregroundColor(.white)
            Text(label)
                .font(.system(size: 13))
                .foregroundColor(.white.opacity(0.65))
        }
    }
}

// MARK: - Quick Action Card

private struct QuickActionCard: View {
    let icon: String
    let label: String
    let gradient: LinearGradient

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: icon)
                .font(.system(size: 22, weight: .semibold))
                .foregroundColor(.white)
            Text(label)
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(.white)
                .lineLimit(2)
            Spacer()
        }
        .padding(16)
        .frame(height: 72)
        .background(gradient)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

// MARK: - Activity Row

private struct ActivityRowView: View {
    let item: ActivityItem

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                Circle()
                    .fill(Color(hex: item.color).opacity(0.15))
                    .frame(width: 40, height: 40)
                Image(systemName: item.icon)
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundColor(Color(hex: item.color))
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(item.title)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.pkTextPrimary)
                Text(item.subtitle)
                    .font(.caption)
                    .foregroundColor(.pkTextSecondary)
            }
            Spacer()
            Text(item.time)
                .font(.caption2)
                .foregroundColor(.pkTextTertiary)
        }
        .padding(14)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }
}

// MARK: - Empty Activity

private struct EmptyActivity: View {
    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: "bolt.slash.fill")
                .font(.system(size: 40))
                .foregroundColor(.pkTextTertiary)
            Text("No activity yet")
                .font(.subheadline)
                .foregroundColor(.pkTextSecondary)
            Text("Tap 'New Program' to get started")
                .font(.caption)
                .foregroundColor(.pkTextTertiary)
        }
        .frame(maxWidth: .infinity)
        .padding(40)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

// MARK: - Section Header

struct SectionHeader: View {
    let title: String
    init(_ title: String) { self.title = title }
    var body: some View {
        Text(title)
            .font(.system(size: 17, weight: .bold))
            .foregroundColor(.pkTextPrimary)
    }
}

#Preview {
    HomeView().environmentObject(AppState())
}
