package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.CreditCard
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.Store
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.ui.theme.VidyuthBackground
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthOnPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthTertiary

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    onNavigateToProgram: () -> Unit,
    onNavigateToDevices: () -> Unit,
    onNavigateToSettings: () -> Unit,
    onNavigateToMarketplace: () -> Unit = {},
    onNavigateToBilling: () -> Unit = {},
    onNavigateToFleet: () -> Unit = {},
    viewModel: HomeViewModel = hiltViewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentPadding = PaddingValues(bottom = 32.dp),
    ) {
        // Custom header
        item(key = "header") {
            HomeHeader(onAvatarClick = onNavigateToSettings)
        }

        // Hero card
        item(key = "hero") {
            HeroCard(
                isOnline = uiState.backendOnline,
                deviceCount = uiState.deviceCount,
                programCount = uiState.programCount,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }

        item(key = "spacer_after_hero") { Spacer(Modifier.height(24.dp)) }

        // Quick actions
        item(key = "quick_actions_header") {
            Text(
                "Quick Actions",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }

        item(key = "quick_actions_grid") {
            Column(
                modifier = Modifier.padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    QuickActionButton(
                        icon = Icons.Default.Code,
                        label = "New Program",
                        gradient = listOf(VidyuthPrimary, Color(0xFF8B5CF6)),
                        onClick = onNavigateToProgram,
                        modifier = Modifier.weight(1f),
                    )
                    QuickActionButton(
                        icon = Icons.Default.Devices,
                        label = "My Devices",
                        gradient = listOf(VidyuthSecondary, Color(0xFF0EA5E9)),
                        onClick = onNavigateToDevices,
                        modifier = Modifier.weight(1f),
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    QuickActionButton(
                        icon = Icons.Default.Store,
                        label = "Marketplace",
                        gradient = listOf(Color(0xFF8B5CF6), Color(0xFFA855F7)),
                        onClick = onNavigateToMarketplace,
                        modifier = Modifier.weight(1f),
                    )
                    QuickActionButton(
                        icon = Icons.Default.CreditCard,
                        label = "My Plan",
                        gradient = listOf(VidyuthTertiary, Color(0xFFFF8C42)),
                        onClick = onNavigateToBilling,
                        modifier = Modifier.weight(1f),
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    QuickActionButton(
                        icon = Icons.Default.FlashOn,
                        label = "Fleet",
                        gradient = listOf(VidyuthSuccess, Color(0xFF059669)),
                        onClick = onNavigateToFleet,
                        modifier = Modifier.weight(1f),
                    )
                    Spacer(Modifier.weight(1f))
                }
            }
        }

        item(key = "spacer_before_activity") { Spacer(Modifier.height(28.dp)) }

        // Recent activity
        item(key = "activity_header") {
            Text(
                "Recent",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }

        if (uiState.recentProjects.isEmpty()) {
            item(key = "empty_activity") {
                EmptyActivity()
            }
        } else {
            items(uiState.recentProjects, key = { it }) { item ->
                ActivityRow(
                    label = item,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }
        }
    }
}

@Composable
private fun HomeHeader(onAvatarClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 20.dp)
            .padding(top = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column {
            Text(
                "Good morning \u26A1",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            )
            Text(
                "Parakram",
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface,
            )
        }

        IconButton(
            onClick = onAvatarClick,
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .background(VidyuthPrimary.copy(alpha = 0.12f)),
        ) {
            Icon(
                Icons.Default.AccountCircle,
                contentDescription = "Profile",
                tint = VidyuthPrimary,
                modifier = Modifier.size(28.dp),
            )
        }
    }
}

@Composable
private fun HeroCard(
    isOnline: Boolean,
    deviceCount: Int,
    programCount: Int,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.horizontalGradient(
                    listOf(VidyuthPrimary, VidyuthSecondary),
                ),
            )
            .padding(24.dp),
    ) {
        Column {
            Text(
                "Zero-Code Hardware",
                fontSize = 22.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
            )
            Spacer(Modifier.height(4.dp))
            Text(
                "Describe it. We deploy it.",
                color = Color.White.copy(alpha = 0.7f),
                fontSize = 14.sp,
            )
            Spacer(Modifier.height(16.dp))

            Row(
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // Animated status dot
                OnlineStatusBadge(isOnline = isOnline)

                // Stats
                HeroStat(count = deviceCount, label = "devices")
                HeroStat(count = programCount, label = "programs")
            }
        }

        // Background bolt icon
        Icon(
            Icons.Default.Bolt,
            contentDescription = null,
            tint = Color.White.copy(alpha = 0.06f),
            modifier = Modifier
                .size(140.dp)
                .align(Alignment.BottomEnd)
                .padding(bottom = 4.dp, end = 4.dp),
        )
    }
}

@Composable
private fun OnlineStatusBadge(isOnline: Boolean) {
    val infiniteTransition = rememberInfiniteTransition(label = "hero_dot")
    val scale by infiniteTransition.animateFloat(
        initialValue = 1f,
        targetValue = 1.4f,
        animationSpec = infiniteRepeatable(
            animation = tween(900, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse,
        ),
        label = "dot_scale",
    )

    Surface(
        shape = RoundedCornerShape(20.dp),
        color = Color.White.copy(alpha = 0.18f),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .scale(if (isOnline) scale else 1f)
                    .clip(CircleShape)
                    .background(if (isOnline) VidyuthSuccess else VidyuthError),
            )
            Text(
                if (isOnline) "Backend Online" else "Offline",
                color = Color.White,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
            )
        }
    }
}

@Composable
private fun HeroStat(count: Int, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            "$count",
            fontWeight = FontWeight.Bold,
            color = Color.White,
            fontSize = 16.sp,
        )
        Spacer(Modifier.width(4.dp))
        Text(
            label,
            color = Color.White.copy(alpha = 0.65f),
            fontSize = 13.sp,
        )
    }
}

@Composable
private fun QuickActionButton(
    icon: ImageVector,
    label: String,
    gradient: List<Color>,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .height(80.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(Brush.verticalGradient(gradient))
            .clickable(onClick = onClick)
            .padding(16.dp),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Icon(
                icon,
                contentDescription = label,
                tint = Color.White,
                modifier = Modifier.size(26.dp),
            )
            Text(
                label,
                color = Color.White,
                fontWeight = FontWeight.SemiBold,
                fontSize = 13.sp,
                lineHeight = 16.sp,
            )
        }
    }
}

@Composable
private fun ActivityRow(label: String, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(CircleShape)
                    .background(VidyuthPrimary.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    Icons.Default.FlashOn,
                    contentDescription = null,
                    tint = VidyuthPrimary,
                    modifier = Modifier.size(18.dp),
                )
            }
            Spacer(Modifier.width(12.dp))
            Text(label, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
private fun EmptyActivity() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surface)
            .padding(40.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Icon(
            Icons.Default.FlashOn,
            contentDescription = null,
            modifier = Modifier.size(48.dp),
            tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f),
        )
        Text(
            "No activity yet",
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
            fontWeight = FontWeight.Medium,
        )
        Text(
            "Tap 'New Program' to get started",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
        )
    }
}
