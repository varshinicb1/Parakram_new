import SwiftUI

// MARK: - TelemetryView

struct TelemetryView: View {
    let deviceId: String
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = TelemetryViewModel()

    var body: some View {
        ZStack {
            Color.pkBackground.ignoresSafeArea()

            ScrollView {
                VStack(spacing: 20) {
                    // Header: live dot + device info + status chip
                    TelemetryHeaderCard(deviceId: deviceId, status: vm.status)

                    // 2x2 Metric grid
                    if let r = vm.latest {
                        MetricGrid(reading: r)
                    } else {
                        MetricGridPlaceholder()
                    }

                    // Sparkline
                    TemperatureSparkline(readings: vm.readings)

                    // Footer info
                    TelemetryFooter(latest: vm.latest)

                    // Reconnect button when disconnected
                    if case .disconnected = vm.status {
                        ReconnectButton {
                            vm.connect(
                                deviceId: deviceId,
                                token: appState.token,
                                baseURL: ParakramAPI.shared.baseURL
                            )
                        }
                    }
                    if case .error = vm.status {
                        ReconnectButton {
                            vm.connect(
                                deviceId: deviceId,
                                token: appState.token,
                                baseURL: ParakramAPI.shared.baseURL
                            )
                        }
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 16)
                .padding(.bottom, 32)
            }
        }
        .navigationTitle("Telemetry")
        .navigationBarTitleDisplayMode(.inline)
        .task {
            vm.connect(
                deviceId: deviceId,
                token: appState.token,
                baseURL: ParakramAPI.shared.baseURL
            )
        }
        .onDisappear { vm.disconnect() }
    }
}

// MARK: - Header Card

private struct TelemetryHeaderCard: View {
    let deviceId: String
    let status: WsStatus

    @State private var pulseScale: CGFloat = 1.0

    private var dotColor: Color {
        switch status {
        case .connected:  return .pkSuccess
        case .connecting: return .pkWarning
        case .error:      return .pkError
        case .disconnected: return Color.white.opacity(0.3)
        }
    }

    var body: some View {
        HStack(spacing: 14) {
            // Animated status dot
            ZStack {
                Circle()
                    .fill(dotColor.opacity(0.25))
                    .frame(width: 36, height: 36)
                    .scaleEffect(status.isLive ? pulseScale : 1.0)

                Circle()
                    .fill(dotColor)
                    .frame(width: 14, height: 14)
            }
            .onAppear {
                guard status.isLive else { return }
                withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: true)) {
                    pulseScale = 1.45
                }
            }
            .onChange(of: status.isLive) { isLive in
                if isLive {
                    withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: true)) {
                        pulseScale = 1.45
                    }
                } else {
                    withAnimation(.default) { pulseScale = 1.0 }
                }
            }

            VStack(alignment: .leading, spacing: 2) {
                Text("Device")
                    .font(.caption)
                    .foregroundColor(.pkTextTertiary)
                Text(deviceId)
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(.pkTextPrimary)
                    .lineLimit(1)
            }

            Spacer()

            // Status chip
            Text(status.displayLabel)
                .font(.caption2)
                .fontWeight(.semibold)
                .foregroundColor(dotColor)
                .padding(.horizontal, 10)
                .padding(.vertical, 5)
                .background(dotColor.opacity(0.14))
                .clipShape(Capsule())
        }
        .padding(16)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

// MARK: - Metric Grid

private struct MetricGrid: View {
    let reading: TelemetryReading

    let columns = [GridItem(.flexible(), spacing: 12), GridItem(.flexible(), spacing: 12)]

    var body: some View {
        LazyVGrid(columns: columns, spacing: 12) {
            MetricCard(
                icon: "thermometer.medium",
                label: "Temperature",
                value: reading.temperatureString,
                gradient: .pkPrimary
            )
            MetricCard(
                icon: "drop.fill",
                label: "Humidity",
                value: reading.humidityString,
                gradient: .pkAccent
            )
            MetricCard(
                icon: "clock.fill",
                label: "Uptime",
                value: reading.uptimeString,
                gradient: .pkSuccess
            )
            MetricCard(
                icon: "wifi",
                label: "Signal",
                value: reading.rssiString,
                gradient: .pkTertiary
            )
        }
    }
}

private struct MetricGridPlaceholder: View {
    let columns = [GridItem(.flexible(), spacing: 12), GridItem(.flexible(), spacing: 12)]

    var body: some View {
        LazyVGrid(columns: columns, spacing: 12) {
            MetricCard(icon: "thermometer.medium", label: "Temperature", value: "—",      gradient: .pkPrimary)
            MetricCard(icon: "drop.fill",          label: "Humidity",    value: "—",      gradient: .pkAccent)
            MetricCard(icon: "clock.fill",         label: "Uptime",      value: "—",      gradient: .pkSuccess)
            MetricCard(icon: "wifi",               label: "Signal",      value: "—",      gradient: .pkTertiary)
        }
    }
}

// MARK: - Metric Card

struct MetricCard: View {
    let icon: String
    let label: String
    let value: String
    let gradient: LinearGradient

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Icon circle
            ZStack {
                Circle()
                    .fill(gradient)
                    .frame(width: 40, height: 40)
                Image(systemName: icon)
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundColor(.white)
            }

            Spacer(minLength: 0)

            // Value
            Text(value)
                .font(.system(size: 24, weight: .bold, design: .rounded))
                .foregroundColor(.pkTextPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.6)

            // Label
            Text(label)
                .font(.caption)
                .fontWeight(.medium)
                .foregroundColor(.pkTextSecondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(Color.white.opacity(0.06), lineWidth: 0.5)
        )
    }
}

// MARK: - Temperature Sparkline

private struct TemperatureSparkline: View {
    let readings: [TelemetryReading]

    // Last 30 readings for the chart
    private var chartData: [Double] {
        readings.suffix(30).map { $0.temperature }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Image(systemName: "waveform.path.ecg")
                    .foregroundColor(.pkPrimary)
                Text("Temperature History")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(.pkTextSecondary)
                Spacer()
                if let last = chartData.last {
                    Text(String(format: "%.1f°C", last))
                        .font(.system(size: 13, weight: .bold, design: .rounded))
                        .foregroundColor(.pkPrimary)
                }
            }

            SparklineCanvas(values: chartData)
                .frame(height: 96)
                .animation(.easeInOut(duration: 0.4), value: chartData.count)
        }
        .padding(16)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(Color.white.opacity(0.06), lineWidth: 0.5)
        )
    }
}

// MARK: - Sparkline Canvas

private struct SparklineCanvas: View {
    let values: [Double]

    /// Normalise temperature to 0–50°C range
    private func normalised(_ v: Double) -> Double {
        min(max(v / 50.0, 0), 1)
    }

    var body: some View {
        Canvas { ctx, size in
            guard values.count >= 2 else {
                // Draw a flat baseline when no data
                var path = Path()
                path.move(to:    CGPoint(x: 0,        y: size.height / 2))
                path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
                ctx.stroke(path, with: .color(.white.opacity(0.15)), lineWidth: 1)
                return
            }

            let count  = values.count
            let stepX  = size.width / CGFloat(count - 1)

            func point(at index: Int) -> CGPoint {
                let x = CGFloat(index) * stepX
                let y = size.height - normalised(values[index]) * size.height
                return CGPoint(x: x, y: y)
            }

            // Fill path (under the line)
            var fillPath = Path()
            fillPath.move(to: CGPoint(x: 0, y: size.height))
            for i in 0..<count {
                fillPath.addLine(to: point(at: i))
            }
            fillPath.addLine(to: CGPoint(x: CGFloat(count - 1) * stepX, y: size.height))
            fillPath.closeSubpath()

            ctx.fill(
                fillPath,
                with: .linearGradient(
                    Gradient(colors: [
                        Color.pkPrimary.opacity(0.45),
                        Color.pkPrimary.opacity(0.0)
                    ]),
                    startPoint: CGPoint(x: size.width / 2, y: 0),
                    endPoint:   CGPoint(x: size.width / 2, y: size.height)
                )
            )

            // Line path
            var linePath = Path()
            linePath.move(to: point(at: 0))
            for i in 1..<count {
                linePath.addLine(to: point(at: i))
            }

            ctx.stroke(
                linePath,
                with: .linearGradient(
                    Gradient(colors: [Color.pkPrimary, Color(hex: "#9C27B0")]),
                    startPoint: CGPoint(x: 0, y: 0),
                    endPoint:   CGPoint(x: size.width, y: 0)
                ),
                style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round)
            )

            // Dot at latest value
            let lastPt = point(at: count - 1)
            let dotRect = CGRect(
                x: lastPt.x - 4,
                y: lastPt.y - 4,
                width: 8,
                height: 8
            )
            ctx.fill(Path(ellipseIn: dotRect), with: .color(.pkPrimary))
        }
    }
}

// MARK: - Footer

private struct TelemetryFooter: View {
    let latest: TelemetryReading?

    var body: some View {
        HStack(spacing: 0) {
            // Free heap
            VStack(alignment: .leading, spacing: 2) {
                Text("Free Heap")
                    .font(.caption2)
                    .foregroundColor(.pkTextTertiary)
                Text(latest?.freeHeapKBString ?? "—")
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(.pkTextPrimary)
            }

            Spacer()

            // Last update
            VStack(alignment: .trailing, spacing: 2) {
                Text("Last update")
                    .font(.caption2)
                    .foregroundColor(.pkTextTertiary)
                Text(lastUpdateText)
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundColor(.pkTextPrimary)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }

    private var lastUpdateText: String {
        guard let r = latest else { return "—" }
        let diff = Date().timeIntervalSince1970 - r.ts
        if diff < 5  { return "Just now" }
        if diff < 60 { return "\(Int(diff))s ago" }
        let m = Int(diff / 60)
        return "\(m)m ago"
    }
}

// MARK: - Reconnect Button

private struct ReconnectButton: View {
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label("Reconnect", systemImage: "arrow.triangle.2.circlepath")
                .font(.system(size: 15, weight: .semibold))
                .foregroundColor(.white)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 16)
                .background(LinearGradient.pkPrimary)
                .clipShape(RoundedRectangle(cornerRadius: 14))
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Preview

#Preview {
    NavigationStack {
        TelemetryView(deviceId: "dev-preview-001")
            .environmentObject(AppState())
    }
}
