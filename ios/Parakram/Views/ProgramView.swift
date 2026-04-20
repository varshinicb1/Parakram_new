import SwiftUI

struct ProgramView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = ProgramViewModel()

    var body: some View {
        NavigationStack {
            ZStack {
                Color.pkBackground.ignoresSafeArea()

                VStack(spacing: 0) {
                    // Step indicator
                    StepIndicator(steps: ProgramStep.allCases, current: vm.currentStep)
                        .padding(.horizontal, 24)
                        .padding(.top, 8)
                        .padding(.bottom, 24)

                    // Step content (slide transition)
                    Group {
                        switch vm.currentStep {
                        case .describe:
                            DescribeStep(vm: vm)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        case .generate:
                            GenerateStep(vm: vm)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        case .compile:
                            CompileStep(vm: vm)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        case .deploy:
                            DeployStep(vm: vm)
                                .transition(.asymmetric(
                                    insertion: .move(edge: .trailing).combined(with: .opacity),
                                    removal: .move(edge: .leading).combined(with: .opacity)
                                ))
                        }
                    }
                    .animation(.spring(response: 0.4, dampingFraction: 0.85), value: vm.currentStep)

                    Spacer()
                }
            }
            .navigationTitle("Build")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    if vm.currentStep != .describe {
                        Button("Reset") { withAnimation { vm.reset() } }
                            .foregroundColor(.pkError)
                            .font(.system(size: 14, weight: .medium))
                    }
                }
            }
        }
    }
}

// MARK: - Step Indicator

private struct StepIndicator: View {
    let steps: [ProgramStep]
    let current: ProgramStep

    var body: some View {
        HStack(spacing: 0) {
            ForEach(steps, id: \.rawValue) { step in
                HStack(spacing: 0) {
                    // Circle
                    ZStack {
                        Circle()
                            .fill(step.rawValue <= current.rawValue ? LinearGradient.pkPrimary : LinearGradient(colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)], startPoint: .top, endPoint: .bottom))
                            .frame(width: 32, height: 32)
                        if step.rawValue < current.rawValue {
                            Image(systemName: "checkmark")
                                .font(.system(size: 12, weight: .bold))
                                .foregroundColor(.white)
                        } else {
                            Image(systemName: step.icon)
                                .font(.system(size: 12, weight: .semibold))
                                .foregroundColor(step.rawValue == current.rawValue ? .white : Color.white.opacity(0.3))
                        }
                    }

                    // Connector line
                    if step != steps.last {
                        Rectangle()
                            .fill(step.rawValue < current.rawValue ? Color.pkPrimary : Color.white.opacity(0.1))
                            .frame(height: 2)
                            .frame(maxWidth: .infinity)
                    }
                }
            }
        }
    }
}

// MARK: - Describe Step

private struct DescribeStep: View {
    @ObservedObject var vm: ProgramViewModel
    @EnvironmentObject var appState: AppState
    @FocusState private var textFocused: Bool

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("What should your device do?")
                        .font(.system(size: 20, weight: .bold))
                        .foregroundColor(.pkTextPrimary)
                    Text("Describe in plain English — no coding needed")
                        .font(.subheadline)
                        .foregroundColor(.pkTextSecondary)
                }

                // Description text field
                ZStack(alignment: .topLeading) {
                    RoundedRectangle(cornerRadius: 14)
                        .fill(Color.pkSurface)
                        .overlay(
                            RoundedRectangle(cornerRadius: 14)
                                .stroke(textFocused ? Color.pkPrimary.opacity(0.6) : Color.white.opacity(0.08), lineWidth: 1)
                        )
                    if vm.descriptionText.isEmpty {
                        Text("e.g. Blink an LED every 500ms when button is pressed")
                            .foregroundColor(.pkTextTertiary)
                            .font(.system(size: 15))
                            .padding(16)
                    }
                    TextEditor(text: $vm.descriptionText)
                        .foregroundColor(.pkTextPrimary)
                        .font(.system(size: 15))
                        .frame(minHeight: 130)
                        .padding(12)
                        .scrollContentBackground(.hidden)
                        .focused($textFocused)
                }
                .frame(minHeight: 140)

                // Example prompt chips
                VStack(alignment: .leading, spacing: 8) {
                    Text("Examples")
                        .font(.caption)
                        .fontWeight(.medium)
                        .foregroundColor(.pkTextTertiary)
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 160))], spacing: 8) {
                        ForEach(vm.examplePrompts, id: \.self) { prompt in
                            Button(action: { vm.descriptionText = prompt }) {
                                Text(prompt)
                                    .font(.caption)
                                    .foregroundColor(.pkPrimary)
                                    .lineLimit(2)
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 6)
                                    .background(Color.pkPrimary.opacity(0.10))
                                    .clipShape(RoundedRectangle(cornerRadius: 8))
                            }
                        }
                    }
                }

                // Board picker
                VStack(alignment: .leading, spacing: 8) {
                    Text("Target Board")
                        .font(.caption)
                        .fontWeight(.medium)
                        .foregroundColor(.pkTextTertiary)
                    HStack(spacing: 8) {
                        ForEach(vm.boardOptions, id: \.id) { board in
                            Button(action: { vm.selectedBoard = board.id }) {
                                Text(board.name)
                                    .font(.caption)
                                    .fontWeight(.medium)
                                    .foregroundColor(vm.selectedBoard == board.id ? .white : .pkTextSecondary)
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 6)
                                    .background(
                                        vm.selectedBoard == board.id
                                            ? AnyView(LinearGradient.pkPrimary)
                                            : AnyView(Color.pkSurface)
                                    )
                                    .clipShape(RoundedRectangle(cornerRadius: 8))
                            }
                        }
                    }
                    .horizontalScroll()
                }

                // Error
                if let err = vm.errorMessage {
                    ErrorBanner(message: err)
                }

                // Generate button
                Button(action: {
                    textFocused = false
                    Task { await vm.generate(token: appState.token) }
                }) {
                    HStack {
                        if vm.isLoading {
                            ProgressView()
                                .progressViewStyle(CircularProgressViewStyle(tint: .white))
                                .frame(width: 20, height: 20)
                        } else {
                            Image(systemName: "wand.and.stars")
                                .font(.system(size: 16, weight: .semibold))
                        }
                        Text(vm.isLoading ? "Generating…" : "Generate Program")
                            .font(.system(size: 16, weight: .semibold))
                    }
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, minHeight: 52)
                    .background(vm.canGenerate ? LinearGradient.pkPrimary : LinearGradient(colors: [Color.white.opacity(0.1), Color.white.opacity(0.1)], startPoint: .leading, endPoint: .trailing))
                    .clipShape(RoundedRectangle(cornerRadius: 14))
                }
                .disabled(!vm.canGenerate || vm.isLoading)
            }
            .padding(20)
        }
    }
}

// MARK: - Generate Step

private struct GenerateStep: View {
    @ObservedObject var vm: ProgramViewModel
    @EnvironmentObject var appState: AppState

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack(spacing: 10) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(.pkSuccess)
                        .font(.title2)
                    Text("Program Generated")
                        .font(.system(size: 20, weight: .bold))
                        .foregroundColor(.pkTextPrimary)
                }

                if let preview = vm.intentResponse?.preview {
                    IRPreviewCard(preview: preview)
                }

                if let err = vm.errorMessage { ErrorBanner(message: err) }

                Button(action: {
                    Task { await vm.compile(token: appState.token) }
                }) {
                    ActionButton(title: "Compile", icon: "hammer.fill", isLoading: vm.isLoading)
                }
                .disabled(vm.isLoading)
            }
            .padding(20)
        }
    }
}

private struct IRPreviewCard: View {
    let preview: IRPreview

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            ChipRow(title: "Sensors", items: preview.sensors, color: .pkSecondary)
            ChipRow(title: "Actuators", items: preview.actuators, color: .pkTertiary)
            ChipRow(title: "Logic", items: preview.logic, color: .pkSuccess)
            if !preview.warnings.isEmpty {
                ChipRow(title: "Warnings", items: preview.warnings, color: .pkWarning)
            }
        }
        .padding(16)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

private struct ChipRow: View {
    let title: String
    let items: [String]
    let color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.caption)
                .fontWeight(.semibold)
                .foregroundColor(color)
            if items.isEmpty {
                Text("None")
                    .font(.caption)
                    .foregroundColor(.pkTextTertiary)
            } else {
                FlowLayout(spacing: 6) {
                    ForEach(items, id: \.self) { item in
                        Text(item)
                            .font(.caption)
                            .foregroundColor(color)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(color.opacity(0.12))
                            .clipShape(Capsule())
                    }
                }
            }
        }
    }
}

// MARK: - Compile Step

private struct CompileStep: View {
    @ObservedObject var vm: ProgramViewModel
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 24) {
            Spacer()
            // Progress
            ZStack {
                Circle()
                    .stroke(Color.pkPrimary.opacity(0.2), lineWidth: 8)
                    .frame(width: 120, height: 120)
                Circle()
                    .trim(from: 0, to: vm.progressFraction)
                    .stroke(LinearGradient.pkPrimary, style: StrokeStyle(lineWidth: 8, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                    .frame(width: 120, height: 120)
                    .animation(.easeInOut(duration: 0.4), value: vm.progressFraction)
                Text("\(Int(vm.progressFraction * 100))%")
                    .font(.system(size: 22, weight: .bold))
                    .foregroundColor(.pkTextPrimary)
            }
            Text(vm.deploymentState.displayName)
                .font(.headline)
                .foregroundColor(.pkTextSecondary)

            if let result = vm.compileResult {
                VStack(spacing: 8) {
                    StatRow(label: "Binary size", value: "\(result.binarySize ?? 0) bytes")
                    StatRow(label: "Status", value: result.success ? "Success" : "Failed")
                }
                .padding(16)
                .background(Color.pkSurface)
                .clipShape(RoundedRectangle(cornerRadius: 14))
                .padding(.horizontal, 24)
            }

            if let err = vm.errorMessage {
                ErrorBanner(message: err).padding(.horizontal, 24)
            }

            if vm.compileResult?.success == true {
                Button(action: {
                    Task { await vm.deploy(token: appState.token, deviceId: "dev-001") }
                }) {
                    ActionButton(title: "Deploy to Device", icon: "bolt.fill", isLoading: vm.isLoading)
                }
                .disabled(vm.isLoading)
                .padding(.horizontal, 24)
            } else if vm.compileResult == nil {
                ProgressView("Compiling…")
                    .foregroundColor(.pkTextSecondary)
            }
            Spacer()
        }
    }
}

// MARK: - Deploy Step

private struct DeployStep: View {
    @ObservedObject var vm: ProgramViewModel

    var body: some View {
        VStack(spacing: 24) {
            Spacer()
            ZStack {
                Circle()
                    .fill(Color.pkSuccess.opacity(0.12))
                    .frame(width: 120, height: 120)
                Image(systemName: "checkmark.circle.fill")
                    .font(.system(size: 64))
                    .foregroundColor(.pkSuccess)
            }
            Text("Program Running!")
                .font(.system(size: 24, weight: .bold))
                .foregroundColor(.pkTextPrimary)
            if let msg = vm.successMessage {
                Text(msg)
                    .font(.subheadline)
                    .foregroundColor(.pkTextSecondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)
            }
            Button(action: { withAnimation { vm.reset() } }) {
                Label("Build Another", systemImage: "plus.circle.fill")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(.white)
                    .padding(.horizontal, 28)
                    .padding(.vertical, 14)
                    .background(LinearGradient.pkPrimary)
                    .clipShape(Capsule())
            }
            Spacer()
        }
    }
}

// MARK: - Helpers

private struct ActionButton: View {
    let title: String
    let icon: String
    let isLoading: Bool

    var body: some View {
        HStack(spacing: 8) {
            if isLoading {
                ProgressView().progressViewStyle(CircularProgressViewStyle(tint: .white))
            } else {
                Image(systemName: icon).font(.system(size: 15, weight: .semibold))
            }
            Text(isLoading ? "Please wait…" : title).font(.system(size: 15, weight: .semibold))
        }
        .foregroundColor(.white)
        .frame(maxWidth: .infinity, minHeight: 52)
        .background(LinearGradient.pkPrimary)
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }
}

private struct StatRow: View {
    let label: String
    let value: String
    var body: some View {
        HStack {
            Text(label).font(.caption).foregroundColor(.pkTextSecondary)
            Spacer()
            Text(value).font(.caption).fontWeight(.medium).foregroundColor(.pkTextPrimary)
        }
    }
}

struct ErrorBanner: View {
    let message: String
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill").foregroundColor(.pkError)
            Text(message).font(.caption).foregroundColor(.pkError)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.pkError.opacity(0.10))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

// MARK: - Flow Layout

struct FlowLayout: Layout {
    var spacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = computeRows(proposal: proposal, subviews: subviews)
        return rows.reduce(CGSize.zero) { result, row in
            CGSize(
                width: max(result.width, row.reduce(0) { $0 + $1.sizeThatFits(.unspecified).width + spacing }),
                height: result.height + (row.first?.sizeThatFits(.unspecified).height ?? 0) + spacing
            )
        }
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let rows = computeRows(proposal: proposal, subviews: subviews)
        var y = bounds.minY
        for row in rows {
            var x = bounds.minX
            let height = row.map { $0.sizeThatFits(.unspecified).height }.max() ?? 0
            for subview in row {
                let size = subview.sizeThatFits(.unspecified)
                subview.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += height + spacing
        }
    }

    private func computeRows(proposal: ProposedViewSize, subviews: Subviews) -> [[LayoutSubview]] {
        let maxWidth = proposal.width ?? .infinity
        var rows: [[LayoutSubview]] = [[]]
        var rowWidth: CGFloat = 0

        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if rowWidth + size.width > maxWidth, !rows[rows.count - 1].isEmpty {
                rows.append([subview])
                rowWidth = size.width + spacing
            } else {
                rows[rows.count - 1].append(subview)
                rowWidth += size.width + spacing
            }
        }
        return rows
    }
}

// MARK: - View extension helpers

extension View {
    func horizontalScroll() -> some View {
        ScrollView(.horizontal, showsIndicators: false) { self }
    }
}

#Preview {
    ProgramView().environmentObject(AppState())
}
