package com.vidyuthlabs.parakram.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// Parakram brand colors
val VidyuthPrimary = Color(0xFF6C63FF)
val VidyuthSecondary = Color(0xFF00D9FF)
val VidyuthTertiary = Color(0xFFFF6584)
val VidyuthSurface = Color(0xFF1A1A2E)
val VidyuthSurfaceVariant = Color(0xFF16213E)
val VidyuthBackground = Color(0xFF0F0F23)
val VidyuthOnPrimary = Color(0xFFFFFFFF)
val VidyuthOnSurface = Color(0xFFE0E0F0)
val VidyuthSuccess = Color(0xFF00E676)
val VidyuthWarning = Color(0xFFFFAB00)
val VidyuthError = Color(0xFFFF5252)

private val DarkColorScheme = darkColorScheme(
    primary = VidyuthPrimary,
    secondary = VidyuthSecondary,
    tertiary = VidyuthTertiary,
    background = VidyuthBackground,
    surface = VidyuthSurface,
    surfaceVariant = VidyuthSurfaceVariant,
    onPrimary = VidyuthOnPrimary,
    onSecondary = Color.Black,
    onTertiary = Color.White,
    onBackground = VidyuthOnSurface,
    onSurface = VidyuthOnSurface,
    error = VidyuthError,
)

private val LightColorScheme = lightColorScheme(
    primary = VidyuthPrimary,
    secondary = Color(0xFF0099CC),
    tertiary = VidyuthTertiary,
    background = Color(0xFFF8F9FF),
    surface = Color.White,
    surfaceVariant = Color(0xFFEEEEFF),
)

@Composable
fun ParakramTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography(),
        content = content
    )
}
