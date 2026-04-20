import SwiftUI

struct DevicesView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = DevicesViewModel()
    @State private var showPairDevice: Bool = false

    var body: some View {
        NavigationStack {
            ZStack {
                Color.pkBackground.ignoresSafeArea()

                VStack(spacing: 0) {
                    // Connection status bar
                    ConnectionStatusBar(state: vm.connectionState)

                    if vm.isScanning && vm.devices.isEmpty {
                        // Radar animation
                        Spacer()
                        RadarView()
                            .frame(width: 240, height: 240)
                        Text("Scanning for Parakram devices…")
                            .font(.subheadline)
                            .foregroundColor(.pkTextSecondary)
                            .padding(.top, 24)
                        Spacer()
                    } else if vm.devices.isEmpty {
                        // Empty state
                        Spacer()
                        EmptyDevicesState { vm.startScan() }
                        Spacer()
                    } else {
                        ScrollView {
                            VStack(spacing: 10) {
                                SectionHeader("Nearby Devices")
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .padding(.top, 16)

                                ForEach(vm.devices) { device in
                                    DeviceCard(
                                        device: device,
                                        isConnected: vm.isConnected && vm.connectedDevice?.id == device.id,
                                        onTap: {
                                            if vm.isConnected {
                                                // navigate to telemetry
                                            } else {
                                                vm.connect(to: device)
                                            }
                                        }
                                    )
                                }
                            }
                            .padding(.horizontal, 16)
                            .padding(.bottom, 32)
                        }
                    }
                }
            }
            .navigationTitle("Devices")
            .navigationBarTitleDisplayMode(.large)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    HStack(spacing: 12) {
                        Button(action: vm.toggleScan) {
                            Image(systemName: vm.isScanning ? "stop.circle.fill" : "antenna.radiowaves.left.and.right")
                                .font(.system(size: 20))
                                .foregroundColor(.pkPrimary)
                        }
                        Button(action: { showPairDevice = true }) {
                            Image(systemName: "plus.circle.fill")
                                .font(.system(size: 20))
                                .foregroundColor(.pkPrimary)
                        }
                    }
                }
            }
            .sheet(isPresented: $showPairDevice) {
                PairDeviceView()
                    .environmentObject(appState)
            }
        }
    }
}

// MARK: - Connection Status Bar

private struct ConnectionStatusBar: View {
    let state: BLEConnectionState

    var info: (icon: String, label: String, color: Color) {
        switch state {
        case .connected(let d):
            return ("checkmark.circle.fill", "Connected: \(d.name)", .pkSuccess)
        case .scanning:
            return ("antenna.radiowaves.left.and.right", "Scanning…", .pkSecondary)
        case .connecting:
            return ("arrow.triangle.2.circlepath", "Connecting…", .pkWarning)
        case .error(let msg):
            return ("exclamationmark.triangle.fill", msg, .pkError)
        case .idle:
            return ("circle", "Not connected", Color.white.opacity(0.4))
        }
    }

    var body: some View {
        let i = info
        HStack(spacing: 10) {
            Image(systemName: i.icon)
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(i.color)
            Text(i.label)
                .font(.system(size: 13, weight: .medium))
                .foregroundColor(i.color)
            Spacer()
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 10)
        .background(i.color.opacity(0.08))
    }
}

// MARK: - Radar View (sonar animation)

struct RadarView: View {
    @State private var scales: [CGFloat] = [0.3, 0.3, 0.3]
    @State private var alphas: [Double]  = [0.7, 0.0, 0.0]

    var body: some View {
        ZStack {
            ForEach(0..<3, id: \.self) { i in
                Circle()
                    .fill(Color.pkPrimary.opacity(0.25 - Double(i) * 0.05))
                    .frame(width: 220, height: 220)
                    .scaleEffect(scales[i])
                    .opacity(alphas[i])
            }
            ZStack {
                Circle()
                    .fill(LinearGradient.pkPrimary)
                    .frame(width: 72, height: 72)
                Image(systemName: "antenna.radiowaves.left.and.right")
                    .font(.system(size: 30))
                    .foregroundColor(.white)
            }
        }
        .onAppear { startAnimation() }
    }

    private func startAnimation() {
        for i in 0..<3 {
            let delay = Double(i) * 0.8
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
                animateRing(index: i)
            }
        }
    }

    private func animateRing(index: Int) {
        scales[index] = 0.3
        alphas[index]  = 0.7
        withAnimation(.linear(duration: 2.4).repeatForever(autoreverses: false)) {
            scales[index] = 1.6
            alphas[index]  = 0.0
        }
    }
}

// MARK: - Device Card

private struct DeviceCard: View {
    let device: BLEDevice
    let isConnected: Bool
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 14) {
                // Icon
                ZStack {
                    RoundedRectangle(cornerRadius: 12)
                        .fill(Color.pkPrimary.opacity(0.12))
                        .frame(width: 46, height: 46)
                    Image(systemName: "cpu")
                        .font(.system(size: 22))
                        .foregroundColor(.pkPrimary)
                }

                VStack(alignment: .leading, spacing: 2) {
                    Text(device.name)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundColor(.pkTextPrimary)
                    Text(device.address)
                        .font(.caption2)
                        .foregroundColor(.pkTextTertiary)
                }

                Spacer()

                // RSSI bars
                RSSIBars(rssi: device.rssi)

                // Status
                if isConnected {
                    Text("Connected")
                        .font(.caption2)
                        .fontWeight(.medium)
                        .foregroundColor(.pkSuccess)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(Color.pkSuccess.opacity(0.12))
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                } else {
                    Image(systemName: "chevron.right")
                        .font(.system(size: 13))
                        .foregroundColor(.pkTextTertiary)
                }
            }
            .padding(16)
            .background(Color.pkSurface)
            .clipShape(RoundedRectangle(cornerRadius: 16))
        }
        .buttonStyle(.plain)
    }
}

// MARK: - RSSI Bars

private struct RSSIBars: View {
    let rssi: Int

    var quality: Int {
        switch rssi {
        case -50...:       return 4
        case -65..<(-50): return 3
        case -75..<(-65): return 2
        case -85..<(-75): return 1
        default:           return 0
        }
    }

    var color: Color {
        switch quality {
        case 4, 3: return .pkSuccess
        case 2:    return .pkWarning
        case 1:    return .pkError
        default:   return Color.white.opacity(0.2)
        }
    }

    var body: some View {
        HStack(alignment: .bottom, spacing: 2) {
            ForEach(1...4, id: \.self) { bar in
                RoundedRectangle(cornerRadius: 2)
                    .fill(bar <= quality ? color : Color.white.opacity(0.15))
                    .frame(width: 4, height: CGFloat(bar) * 5)
            }
        }
    }
}

// MARK: - Empty Devices State

private struct EmptyDevicesState: View {
    let onScan: () -> Void

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .font(.system(size: 64))
                .foregroundColor(.pkTextTertiary)
            Text("No devices found")
                .font(.title3)
                .fontWeight(.semibold)
                .foregroundColor(.pkTextSecondary)
            Text("Make sure your Parakram device is powered on")
                .font(.subheadline)
                .foregroundColor(.pkTextTertiary)
                .multilineTextAlignment(.center)
            Button(action: onScan) {
                Label("Scan Now", systemImage: "antenna.radiowaves.left.and.right")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(.white)
                    .padding(.horizontal, 28)
                    .padding(.vertical, 14)
                    .background(LinearGradient.pkPrimary)
                    .clipShape(Capsule())
            }
        }
        .padding(32)
    }
}

#Preview {
    DevicesView().environmentObject(AppState())
}
