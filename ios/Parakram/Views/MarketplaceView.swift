import SwiftUI

struct MarketplaceView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var vm = MarketplaceViewModel()

    let columns = [GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        NavigationStack {
            ZStack {
                Color.pkBackground.ignoresSafeArea()

                VStack(spacing: 0) {
                    // Search bar
                    SearchBar(text: $vm.searchText)
                        .padding(.horizontal, 16)
                        .padding(.top, 8)
                        .padding(.bottom, 12)

                    // Filter chips
                    FilterRow(
                        types: vm.typeOptions,
                        buses: vm.busOptions,
                        selectedType: $vm.selectedType,
                        selectedBus: $vm.selectedBus,
                        hasActive: vm.hasActiveFilters,
                        onClear: vm.clearFilters
                    )
                    .padding(.bottom, 12)

                    if vm.isLoading && vm.drivers.isEmpty {
                        Spacer()
                        ProgressView("Loading drivers…")
                            .foregroundColor(.pkTextSecondary)
                        Spacer()
                    } else if vm.drivers.isEmpty {
                        Spacer()
                        EmptySearchState()
                        Spacer()
                    } else {
                        ScrollView {
                            LazyVGrid(columns: columns, spacing: 12) {
                                ForEach(vm.drivers) { driver in
                                    DriverCard(
                                        driver: driver,
                                        isInstalled: vm.installedIds.contains(driver.id),
                                        isInstalling: vm.installingId == driver.id,
                                        onInstall: {
                                            Task { await vm.install(driver: driver, token: appState.token) }
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
            .navigationTitle("Marketplace")
            .navigationBarTitleDisplayMode(.large)
            .task { await vm.loadDrivers() }
        }
    }
}

// MARK: - Search Bar

private struct SearchBar: View {
    @Binding var text: String
    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "magnifyingglass")
                .foregroundColor(.pkTextTertiary)
            TextField("Search drivers…", text: $text)
                .foregroundColor(.pkTextPrimary)
                .accentColor(.pkPrimary)
            if !text.isEmpty {
                Button(action: { text = "" }) {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(.pkTextTertiary)
                }
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - Filter Row

private struct FilterRow: View {
    let types: [String]
    let buses: [String]
    @Binding var selectedType: String?
    @Binding var selectedBus: String?
    let hasActive: Bool
    let onClear: () -> Void

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                if hasActive {
                    Button(action: onClear) {
                        HStack(spacing: 4) {
                            Image(systemName: "xmark")
                            Text("Clear")
                        }
                        .font(.caption)
                        .fontWeight(.medium)
                        .foregroundColor(.pkError)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(Color.pkError.opacity(0.12))
                        .clipShape(Capsule())
                    }
                }
                ForEach(types, id: \.self) { type in
                    FilterChip(label: type.capitalized, isSelected: selectedType == type) {
                        selectedType = (selectedType == type) ? nil : type
                    }
                }
                Divider()
                    .frame(height: 20)
                    .overlay(Color.white.opacity(0.15))
                ForEach(buses, id: \.self) { bus in
                    FilterChip(label: bus.uppercased(), isSelected: selectedBus == bus) {
                        selectedBus = (selectedBus == bus) ? nil : bus
                    }
                }
            }
            .padding(.horizontal, 16)
        }
    }
}

private struct FilterChip: View {
    let label: String
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.caption)
                .fontWeight(.medium)
                .foregroundColor(isSelected ? .white : .pkTextSecondary)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(isSelected ? AnyView(LinearGradient.pkPrimary) : AnyView(Color.pkSurface))
                .clipShape(Capsule())
        }
    }
}

// MARK: - Driver Card

private struct DriverCard: View {
    let driver: CommunityDriver
    let isInstalled: Bool
    let isInstalling: Bool
    let onInstall: () -> Void

    var typeGradient: LinearGradient {
        switch driver.driverType {
        case "sensor":   return .pkAccent
        case "actuator": return .pkTertiary
        case "display":  return .pkWarning
        default:         return .pkPrimary
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            // Header
            HStack(spacing: 10) {
                ZStack {
                    RoundedRectangle(cornerRadius: 10)
                        .fill(typeGradient)
                        .frame(width: 36, height: 36)
                    Image(systemName: driverIcon)
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundColor(.white)
                }
                VStack(alignment: .leading, spacing: 1) {
                    Text(driver.driverType.capitalized)
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(.pkTextTertiary)
                    Text(driver.displayName)
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(.pkTextPrimary)
                        .lineLimit(1)
                }
            }

            Text(driver.description)
                .font(.system(size: 11))
                .foregroundColor(.pkTextSecondary)
                .lineLimit(2)

            // Bus types
            HStack(spacing: 4) {
                ForEach(driver.busTypes.prefix(3), id: \.self) { bus in
                    Text(bus.uppercased())
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(.pkPrimary)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 2)
                        .background(Color.pkPrimary.opacity(0.12))
                        .clipShape(Capsule())
                }
            }

            // Stats row
            HStack(spacing: 6) {
                StarsView(rating: driver.averageRating, size: 10)
                Spacer()
                Image(systemName: "arrow.down.circle")
                    .font(.system(size: 10))
                    .foregroundColor(.pkTextTertiary)
                Text("\(driver.downloads)")
                    .font(.system(size: 10))
                    .foregroundColor(.pkTextTertiary)
            }

            // Install button
            Button(action: onInstall) {
                HStack(spacing: 4) {
                    if isInstalling {
                        ProgressView().progressViewStyle(CircularProgressViewStyle(tint: .white)).scaleEffect(0.7)
                    } else {
                        Image(systemName: isInstalled ? "checkmark" : "plus")
                            .font(.system(size: 11, weight: .bold))
                    }
                    Text(isInstalled ? "Installed" : "Install")
                        .font(.system(size: 11, weight: .semibold))
                }
                .foregroundColor(.white)
                .frame(maxWidth: .infinity, minHeight: 28)
                .background(
                    isInstalled
                        ? AnyView(Color.pkSuccess.opacity(0.8))
                        : AnyView(LinearGradient.pkPrimary)
                )
                .clipShape(RoundedRectangle(cornerRadius: 8))
            }
            .disabled(isInstalled || isInstalling)
        }
        .padding(12)
        .background(Color.pkSurface)
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }

    var driverIcon: String {
        switch driver.driverType {
        case "sensor":   return "thermometer"
        case "actuator": return "gearshape.fill"
        case "display":  return "display"
        case "comms":    return "dot.radiowaves.left.and.right"
        default:         return "cpu"
        }
    }
}

// MARK: - Stars View

struct StarsView: View {
    let rating: Double
    var size: CGFloat = 12

    var body: some View {
        HStack(spacing: 1) {
            ForEach(1...5, id: \.self) { star in
                Image(systemName: iconFor(star: star))
                    .font(.system(size: size))
                    .foregroundColor(.pkWarning)
            }
        }
    }

    private func iconFor(star: Int) -> String {
        let full = Int(rating)
        let hasHalf = (rating - Double(full)) >= 0.4
        if star <= full { return "star.fill" }
        if star == full + 1 && hasHalf { return "star.leadinghalf.filled" }
        return "star"
    }
}

// MARK: - Empty state

private struct EmptySearchState: View {
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 48))
                .foregroundColor(.pkTextTertiary)
            Text("No drivers found")
                .font(.headline)
                .foregroundColor(.pkTextSecondary)
            Text("Try a different search or clear filters")
                .font(.subheadline)
                .foregroundColor(.pkTextTertiary)
        }
        .padding(40)
    }
}

#Preview {
    MarketplaceView().environmentObject(AppState())
}
