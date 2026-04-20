import SwiftUI

struct PairDeviceView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = PairDeviceViewModel()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ZStack {
                Color.pkBackground.ignoresSafeArea()

                VStack(spacing: 0) {
                    // Step indicator
                    PairStepIndicator(current: vm.step)
                        .padding(.horizontal, 24)
                        .padding(.top, 8)
                        .padding(.bottom, 20)

                    // Step content
                    Group {
                        switch vm.step {
                        case .scan:
                            ScanStep(vm: vm)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        case .name:
                            NameStep(vm: vm, token: appState.token)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        case .success:
                            SuccessStep(vm: vm) { dismiss() }
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        }
                    }
                    .animation(.spring(response: 0.4, dampingFraction: 0.85), value: vm.step)
                }
            }
            .navigationTitle("Pair New Device")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                        .foregroundColor(.pkTextSecondary)
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    if vm.step == .name {
                        Button(action: { withAnimation { vm.step = .scan } }) {
                            HStack(spacing: 4) {
                                Image(systemName: "chevron.left")
                                Text("Back")
                            }
                            .foregroundColor(.pkTextSecondary)
                        }
                    }
                }
            }
        }
    }
}

// MARK: - Step Indicator

private struct PairStepIndicator: View {
    let current: PairStep

    private struct StepInfo {
        let label: String
        let icon: String
        let index: Int
    }

    private let steps: [StepInfo] = [
        StepInfo(label: "Scan", icon: "antenna.radiowaves.left.and.right", index: 0),
        StepInfo(label: "Name", icon: "pencil",                            index: 1),
        StepInfo(label: "Done", icon: "checkmark",                         index: 2)
    ]

    private var currentIndex: Int {
        switch current {
        case .scan:    return 0
        case .name:    return 1
        case .success: return 2
        }
    }

    var body: some View {
        HStack(spacing: 0) {
            ForEach(steps, id: \.index) { step in
                HStack(spacing: 0) {
                    VStack(spacing: 4) {
                        ZStack {
                            Circle()
                                .fill(
                                    step.index <= currentIndex
                                        ? LinearGradient.pkPrimary
                                        : LinearGradient(
                                            colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)],
                                            startPoint: .top,
                                            endPoint: .bottom
                                        )
                                )
                                .frame(width: 32, height: 32)
                            if step.index < currentIndex {
                                Image(systemName: "checkmark")
                                    .font(.system(size: 12, weight: .bold))
                                    .foregroundColor(.white)
                            } else {
                                Image(systemName: step.icon)
                                    .font(.system(size: 12, weight: .semibold))
                                    .foregroundColor(
                                        step.index == currentIndex
                                            ? .white
                                            : Color.white.opacity(0.3)
                                    )
                            }
                        }
                        Text(step.label)
                            .font(.system(size: 10, weight: .medium))
                            .foregroundColor(
                                step.index <= currentIndex ? .pkTextPrimary : .pkTextTertiary
                            )
                    }

                    if step.index < steps.count - 1 {
                        Rectangle()
                            .fill(
                                step.index < currentIndex
                                    ? Color.pkPrimary
                                    : Color.white.opacity(0.1)
                            )
                            .frame(height: 2)
                            .frame(maxWidth: .infinity)
                            .padding(.bottom, 18)
                    }
                }
            }
        }
    }
}

// MARK: - Step 1: Scan

private struct ScanStep: View {
    @ObservedObject var vm: PairDeviceViewModel

    var body: some View {
        VStack(spacing: 0) {
            // Scan control button
            HStack {
                Spacer()
                Button(action: {
                    if vm.isScanning { vm.stopScan() } else { vm.startScan() }
                }) {
                    Label(
                        vm.isScanning ? "Stop" : "Scan",
                        systemImage: vm.isScanning
                            ? "stop.circle.fill"
                            : "antenna.radiowaves.left.and.right"
                    )
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(.white)
                    .padding(.horizontal, 20)
                    .padding(.vertical, 10)
                    .background(vm.isScanning ? LinearGradient.pkWarning : LinearGradient.pkPrimary)
                    .clipShape(Capsule())
                }
            }
            .padding(.horizontal, 20)
            .padding(.bottom, 12)

            if vm.isScanning && vm.discoveredDevices.isEmpty {
                Spacer()
                RadarView()
                    .frame(width: 220, height: 220)
                Text("Scanning for Parakram devices…")
                    .font(.subheadline)
                    .foregroundColor(.pkTextSecondary)
                    .padding(.top, 24)
                Spacer()
            } else if !vm.isScanning && vm.discoveredDevices.isEmpty {
                Spacer()
                VStack(spacing: 16) {
                    Image(systemName: "antenna.radiowaves.left.and.right.slash")
                        .font(.system(size: 56))
                        .foregroundColor(.pkTextTertiary)
                    Text("No devices found")
                        .font(.title3)
                        .fontWeight(.semibold)
                        .foregroundColor(.pkTextSecondary)
                    Text("Make sure your Parakram device is powered on and in pairing mode")
                        .font(.subheadline)
                        .foregroundColor(.pkTextTertiary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)
                    Button(action: { vm.startScan() }) {
                        Label("Try Again", systemImage: "arrow.clockwise")
                            .font(.system(size: 15, weight: .semibold))
                            .foregroundColor(.white)
                            .padding(.horizontal, 28)
                            .padding(.vertical, 14)
                            .background(LinearGradient.pkPrimary)
                            .clipShape(Capsule())
                    }
                }
                .padding(32)
                Spacer()
            } else {
                ScrollView {
                    VStack(spacing: 10) {
                        SectionHeader("Nearby Devices")
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.top, 8)

                        ForEach(vm.discoveredDevices) { device in
                            PairDeviceCard(device: device) {
                                vm.selectDevice(device)
                            }
                        }
                    }
                    .padding(.horizontal, 16)
                    .padding(.bottom, 32)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Step 2: Name

private struct NameStep: View {
    @ObservedObject var vm: PairDeviceViewModel
    let token: String

    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                // Selected device summary
                if let device = vm.selectedDevice {
                    HStack(spacing: 14) {
                        ZStack {
                            RoundedRectangle(cornerRadius: 10)
                                .fill(Color.pkPrimary.opacity(0.12))
                                .frame(width: 42, height: 42)
                            Image(systemName: "cpu")
                                .font(.system(size: 20))
                                .foregroundColor(.pkPrimary)
                        }
                        VStack(alignment: .leading, spacing: 2) {
                            Text(device.name)
                                .font(.system(size: 14, weight: .semibold))
                                .foregroundColor(.pkTextPrimary)
                            Text(device.address)
                                .font(.caption2)
                                .foregroundColor(.pkTextTertiary)
                        }
                        Spacer()
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundColor(.pkSuccess)
                    }
                    .padding(14)
                    .background(Color.pkSurface)
                    .clipShape(RoundedRectangle(cornerRadius: 14))
                }

                // Device name field
                VStack(alignment: .leading, spacing: 8) {
                    Label("Device Name", systemImage: "tag.fill")
                        .font(.caption)
                        .foregroundColor(.pkTextSecondary)
                    TextField("e.g. Living Room Sensor", text: $vm.deviceName)
                        .foregroundColor(.pkTextPrimary)
                        .accentColor(.pkPrimary)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 12)
                        .background(Color.white.opacity(0.06))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12)
                                .stroke(
                                    vm.deviceName.isEmpty
                                        ? Color.white.opacity(0.10)
                                        : Color.pkPrimary.opacity(0.5),
                                    lineWidth: 1
                                )
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                }

                // Board type radio buttons
                VStack(alignment: .leading, spacing: 12) {
                    Label("Board Type", systemImage: "cpu.fill")
                        .font(.caption)
                        .foregroundColor(.pkTextSecondary)

                    VStack(spacing: 8) {
                        ForEach(PairDeviceViewModel.boardOptions) { option in
                            Button(action: { vm.boardSku = option.id }) {
                                HStack {
                                    ZStack {
                                        Circle()
                                            .stroke(
                                                vm.boardSku == option.id
                                                    ? Color.pkPrimary
                                                    : Color.white.opacity(0.25),
                                                lineWidth: 2
                                            )
                                            .frame(width: 20, height: 20)
                                        if vm.boardSku == option.id {
                                            Circle()
                                                .fill(Color.pkPrimary)
                                                .frame(width: 10, height: 10)
                                        }
                                    }
                                    Text(option.displayName)
                                        .font(.system(size: 15, weight: .medium))
                                        .foregroundColor(.pkTextPrimary)
                                    Spacer()
                                    Text(option.id)
                                        .font(.caption2)
                                        .foregroundColor(.pkTextTertiary)
                                }
                                .padding(.horizontal, 14)
                                .padding(.vertical, 12)
                                .background(
                                    vm.boardSku == option.id
                                        ? Color.pkPrimary.opacity(0.08)
                                        : Color.white.opacity(0.04)
                                )
                                .overlay(
                                    RoundedRectangle(cornerRadius: 12)
                                        .stroke(
                                            vm.boardSku == option.id
                                                ? Color.pkPrimary.opacity(0.4)
                                                : Color.white.opacity(0.08),
                                            lineWidth: 1
                                        )
                                )
                                .clipShape(RoundedRectangle(cornerRadius: 12))
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }

                // Error banner
                if let error = vm.errorMessage {
                    HStack(spacing: 6) {
                        Image(systemName: "exclamationmark.triangle.fill")
                        Text(error)
                    }
                    .font(.caption)
                    .foregroundColor(.pkError)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Color.pkError.opacity(0.10))
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                }

                // Pair Device button
                Button(action: {
                    Task { await vm.pairDevice(token: token) }
                }) {
                    ZStack {
                        if vm.isPairing {
                            ProgressView()
                                .progressViewStyle(CircularProgressViewStyle(tint: .white))
                        } else {
                            Label("Pair Device", systemImage: "link.badge.plus")
                                .font(.system(size: 16, weight: .semibold))
                                .foregroundColor(.white)
                        }
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 52)
                    .background(
                        vm.deviceName.trimmingCharacters(in: .whitespaces).isEmpty
                            ? LinearGradient(
                                colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)],
                                startPoint: .leading,
                                endPoint: .trailing
                              )
                            : LinearGradient.pkPrimary
                    )
                    .clipShape(Capsule())
                }
                .disabled(vm.deviceName.trimmingCharacters(in: .whitespaces).isEmpty || vm.isPairing)
            }
            .padding(20)
        }
    }
}

// MARK: - Step 3: Success

private struct SuccessStep: View {
    @ObservedObject var vm: PairDeviceViewModel
    let onDone: () -> Void

    private var boardDisplayName: String {
        PairDeviceViewModel.boardOptions
            .first(where: { $0.id == vm.boardSku })?
            .displayName ?? vm.boardSku
    }

    var body: some View {
        VStack(spacing: 0) {
            Spacer()
            VStack(spacing: 24) {
                ZStack {
                    Circle()
                        .fill(LinearGradient.pkSuccess)
                        .frame(width: 96, height: 96)
                    Image(systemName: "checkmark")
                        .font(.system(size: 44, weight: .bold))
                        .foregroundColor(.white)
                }

                VStack(spacing: 8) {
                    Text("Paired Successfully!")
                        .font(.system(size: 24, weight: .bold))
                        .foregroundColor(.pkTextPrimary)

                    Text(vm.pairedDevice?.name ?? vm.deviceName)
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundColor(.pkSecondary)

                    Text(boardDisplayName)
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                }

                Button(action: onDone) {
                    Label("Done", systemImage: "checkmark.circle.fill")
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundColor(.white)
                        .frame(maxWidth: .infinity)
                        .frame(height: 52)
                        .background(LinearGradient.pkSuccess)
                        .clipShape(Capsule())
                }
                .padding(.horizontal, 8)
            }
            .padding(32)
            .glassCard(cornerRadius: 24)
            .padding(.horizontal, 32)
            Spacer()
        }
    }
}

// MARK: - BLE Device Card (pair flow)

private struct PairDeviceCard: View {
    let device: BLEDevice
    let onTap: () -> Void

    private var signalQuality: Int {
        switch device.rssi {
        case -50...:       return 4
        case -65..<(-50): return 3
        case -75..<(-65): return 2
        case -85..<(-75): return 1
        default:           return 0
        }
    }

    private var signalColor: Color {
        switch signalQuality {
        case 4, 3: return .pkSuccess
        case 2:    return .pkWarning
        default:   return .pkError
        }
    }

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 14) {
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

                HStack(alignment: .bottom, spacing: 2) {
                    ForEach(1...4, id: \.self) { bar in
                        RoundedRectangle(cornerRadius: 2)
                            .fill(bar <= signalQuality ? signalColor : Color.white.opacity(0.15))
                            .frame(width: 4, height: CGFloat(bar) * 5)
                    }
                }

                Label("Pair", systemImage: "link")
                    .font(.caption2.weight(.semibold))
                    .foregroundColor(.pkPrimary)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 6)
                    .background(Color.pkPrimary.opacity(0.10))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
            }
            .padding(16)
            .background(Color.pkSurface)
            .clipShape(RoundedRectangle(cornerRadius: 16))
        }
        .buttonStyle(.plain)
    }
}

#Preview {
    PairDeviceView().environmentObject(AppState())
}
