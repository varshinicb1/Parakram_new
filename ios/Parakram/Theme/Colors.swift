import SwiftUI

// MARK: - Color Extensions

extension Color {
    // Brand colors matching Android exactly
    static let pkPrimary       = Color(hex: "#6C63FF") // indigo-purple
    static let pkSecondary     = Color(hex: "#00D9FF") // cyan
    static let pkTertiary      = Color(hex: "#FF6584") // coral
    static let pkBackground    = Color(hex: "#0F0F23") // deep navy
    static let pkSurface       = Color(hex: "#1A1A2E") // surface
    static let pkSurfaceVariant = Color(hex: "#16213E") // surface variant
    static let pkSuccess       = Color(hex: "#00E676") // green
    static let pkWarning       = Color(hex: "#FFAB00") // amber
    static let pkError         = Color(hex: "#FF5252") // red
    static let pkGlass         = Color.white.opacity(0.08)

    // Text hierarchy
    static let pkTextPrimary   = Color.white
    static let pkTextSecondary = Color.white.opacity(0.60)
    static let pkTextTertiary  = Color.white.opacity(0.38)

    // Hex initializer
    init(hex: String) {
        let cleaned = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: cleaned).scanHexInt64(&int)
        let a, r, g, b: UInt64
        switch cleaned.count {
        case 3:
            (a, r, g, b) = (255, (int >> 8) * 17, (int >> 4 & 0xF) * 17, (int & 0xF) * 17)
        case 6:
            (a, r, g, b) = (255, int >> 16, int >> 8 & 0xFF, int & 0xFF)
        case 8:
            (a, r, g, b) = (int >> 24, int >> 16 & 0xFF, int >> 8 & 0xFF, int & 0xFF)
        default:
            (a, r, g, b) = (255, 0, 0, 0)
        }
        self.init(
            .sRGB,
            red: Double(r) / 255,
            green: Double(g) / 255,
            blue: Double(b) / 255,
            opacity: Double(a) / 255
        )
    }
}

// MARK: - Gradient Extensions

extension LinearGradient {
    /// Primary: indigo → purple
    static let pkPrimary = LinearGradient(
        colors: [Color.pkPrimary, Color(hex: "#9C27B0")],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    /// Accent: cyan → blue
    static let pkAccent = LinearGradient(
        colors: [Color.pkSecondary, Color(hex: "#0066FF")],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    /// Success: green gradient
    static let pkSuccess = LinearGradient(
        colors: [Color.pkSuccess, Color(hex: "#00BFA5")],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    /// Tertiary: coral → pink
    static let pkTertiary = LinearGradient(
        colors: [Color.pkTertiary, Color(hex: "#E91E8C")],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    /// Warning: amber gradient
    static let pkWarning = LinearGradient(
        colors: [Color.pkWarning, Color(hex: "#FF6D00")],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )

    /// Dark background gradient
    static let pkBackground = LinearGradient(
        colors: [Color.pkBackground, Color.pkSurface],
        startPoint: .top,
        endPoint: .bottom
    )

    /// Hero card gradient
    static let pkHero = LinearGradient(
        colors: [Color.pkPrimary.opacity(0.9), Color.pkSecondary.opacity(0.6)],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
}

// MARK: - Radial Gradient Extensions

extension RadialGradient {
    static func pkGlow(color: Color) -> RadialGradient {
        RadialGradient(
            colors: [color.opacity(0.35), color.opacity(0)],
            center: .center,
            startRadius: 0,
            endRadius: 200
        )
    }
}

// MARK: - Glass Style

struct GlassStyle {
    static func apply<V: View>(to view: V, cornerRadius: CGFloat = 16) -> some View {
        view
            .background(.ultraThinMaterial)
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(Color.white.opacity(0.10), lineWidth: 0.5)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
    }
}

// MARK: - View Extensions

extension View {
    /// Apply glass morphism card styling
    func glassCard(cornerRadius: CGFloat = 16) -> some View {
        self
            .background(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .fill(Color.pkGlass)
                    .background(.ultraThinMaterial.opacity(0.7))
                    .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(Color.white.opacity(0.10), lineWidth: 0.5)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
            .shadow(color: Color.black.opacity(0.25), radius: 12, x: 0, y: 4)
    }

    /// Shimmer/glow border
    func glowBorder(color: Color, radius: CGFloat = 8, lineWidth: CGFloat = 1) -> some View {
        self.overlay(
            RoundedRectangle(cornerRadius: radius)
                .stroke(color, lineWidth: lineWidth)
                .blur(radius: 2)
        )
    }

    /// Full-screen background
    func pkBackground() -> some View {
        self.background(LinearGradient.pkBackground.ignoresSafeArea())
    }
}
