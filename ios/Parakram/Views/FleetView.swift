import SwiftUI

// MARK: - FleetView

struct FleetView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = FleetViewModel()

    var body: some View {
        NavigationStack {
            ZStack {
                Color.pkBackground.ignoresSafeArea()

                if vm.isLoading && vm.devices.isEmpty {
                    FleetLoadingView()
                } else {
                    ScrollView {
                        VStack(spacing: 20) {
                            // Error banner
                            if let err = vm.errorMessage {
                                ErrorBanner(message: err) {
                                    Task { await vm.load(token: appState.token) }
                                }
                            }

                            // Overview stat cards
                            if let overview = vm.overview {
                                OverviewSection(overview: overview)
                            }

                            // Device list
                            DeviceListSection(devices: vm.devices)
                        }
                        .padding(.horizontal, 16)
                        .padding(.top, 16)
                        .padding(.bottom, 32)
                    }
                    .refreshable {
                        await vm.load(token: appState.token)
                    }
                }
            }
            .navigationTitle("Fleet")
            .navigationBarTitleDisplayMode(.large)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        Task { await vm.load(token: appState.token) }
                    } label: {
                        Image(systemName: "arrow.clockwise")
                            .foregroundColor(.pkPrimary)
                    }
                    .disabled(vm.isLoading)
                }
            }
            .task {
                await vm.load(token: appState.token)
            }
        }
    }
}

// MARK: - Loading View

private struct FleetLoadingView: View {
    @State private var rotation: Double = 0

    var body: some View {
        VStack(spacing: 20) {
            ZStack {
                Circle()
                    .fill(LinearGradient.pkPrimary)
                    .frame(width: 64, height: 64)
                Image(systemName: "bolt.fill")
                    .font(.system(size: 28))
                    .foregroundColor(.white)
            }
            .rotationEffect(.degrees(rotation))
            .onAppear {
                withAnimation(.linear(duration: 1.5).repeatForever(autoreverses: false)) {
                    rotation = 360
                }
            }

            Text("Loading fleet…")
                .font(.subheadline)
                .foregroundColor(.pkTextSecondary)
        }
    }
}

// MARK: - Error Banner

private struct ErrorBanner: View {
    let message: String
    let onRetry: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(.pkError)
            Text(message)
                .font(.caption)
                .foregroundColor(.pkTextSecondary)
                .lineLimit(2)
            Spacer()
            Button("Retry", action: onRetry)
                .font(.caption)
                .fontWeight(.semibold)
                .foregroundColor(.pkError)
        }
        .padding(14)
        .background(Color.pkError.opacity(0.10))
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(Color.pkError.opacity(0.25), lineWidth: 0.5)
        )
    }
}

// MARK: - Overview Section

private struct OverviewSection: View {
    let overview: FleetOverview

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionHeader("Overview")

            HStack(spacing: 12) {
                FleetStatCard(
                    icon: "circle.fill",
                    iconColor: .pkSuccess,
                    value: "\(overview.onlineDevices)/\(overview.totalDevices)",
                    label: "Online",
                    animateDot: true
                )
                FleetStatCard(
                    icon: "bolt.fill",
                    iconColor: .pkPrimary,
                    value: "\(overview.deployedProjects)/\(overview.totalProjects)",
                    label: "Deployed",
                    animateDot: false
                )
            }
        }
    }
}

// MARK: - Fleet Stat Card

private struct FleetStatCard: View {
    let icon: String
    let iconColor: Color
    let value: String
    let label: String
    let animateDot: Bool

    @State private var dotScale: CGFloat = 1.0

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                Circle()
                    .fill(iconColor.opacity(0.16))
                    .frame(width: 44, height: 44)

                if animateDot {
                    Circle()
                        .fill(iconColor.opacity(0.20))
                        .frame(width: 44, height: 44)
                        .scaleEffect(dotScale)
                        .onAppear {
                            withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: true)) {
                                dotScale = 1.35
                            }
                        }
                }

                Image(systemName: icon)
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundColor(iconColor)
            }

            VStack(alignment: .leading, spacing: 2) {
                Text(value)
                    .font(.system(size: 22, weight: .bold, design: .rounded))
                    .foregroundColor(.pkTextPrimary)
                Text(label)
                    .font(.caption)
                    .foregroundColor(.pkTextSecondary)
            }

            Spacer()
        }
        .padding(16)
        .frame(maxWidth: .infinity)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(Color.white.opacity(0.06), lineWidth: 0.5)
        )
    }
}

// MARK: - Device List Section

private struct DeviceListSection: View {
    let devices: [FleetDevice]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionHeader("Devices")

            if devices.isEmpty {
                FleetEmptyState()
            } else {
                LazyVStack(spacing: 10) {
                    ForEach(devices) { device in
                        NavigationLink(destination: TelemetryView(deviceId: device.id)) {
                            FleetDeviceRow(device: device)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
    }
}

// MARK: - Fleet Device Row

private struct FleetDeviceRow: View {
    let device: FleetDevice

    private var statusColor: Color {
        switch device.status {
        case "online":  return .pkSuccess
        case "error":   return .pkError
        default:        return Color.white.opacity(0.30)
        }
    }

    var body: some View {
        HStack(spacing: 14) {
            // Status dot
            ZStack {
                Circle()
                    .fill(statusColor.opacity(0.20))
                    .frame(width: 36, height: 36)
                Circle()
                    .fill(statusColor)
                    .frame(width: 12, height: 12)
            }

            VStack(alignment: .leading, spacing: 4) {
                // Name + board SKU chip
                HStack(spacing: 8) {
                    Text(device.name)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundColor(.pkTextPrimary)

                    Text(device.boardSku)
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.pkSecondary)
                        .padding(.horizontal, 7)
                        .padding(.vertical, 3)
                        .background(Color.pkSecondary.opacity(0.14))
                        .clipShape(Capsule())
                }

                // Active project or placeholder
                Text(device.activeProjectName ?? "No program")
                    .font(.caption)
                    .foregroundColor(
                        device.activeProjectName != nil ? .pkTextSecondary : .pkTextTertiary
                    )
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                // Last seen
                Text(device.lastSeenRelative)
                    .font(.caption2)
                    .foregroundColor(.pkTextTertiary)

                Image(systemName: "chevron.right")
                    .font(.system(size: 12, weight: .medium))
                    .foregroundColor(.pkTextTertiary)
            }
        }
        .padding(16)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(Color.white.opacity(0.06), lineWidth: 0.5)
        )
    }
}

// MARK: - Empty State

private struct FleetEmptyState: View {
    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "cpu.fill")
                .font(.system(size: 60))
                .foregroundColor(.pkTextTertiary)

            Text("No devices paired")
                .font(.title3)
                .fontWeight(.semibold)
                .foregroundColor(.pkTextSecondary)

            Text("Pair your first Parakram device via the Devices tab to see it here.")
                .font(.subheadline)
                .foregroundColor(.pkTextTertiary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(40)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

// MARK: - Preview

#Preview {
    FleetView()
        .environmentObject(AppState())
}
