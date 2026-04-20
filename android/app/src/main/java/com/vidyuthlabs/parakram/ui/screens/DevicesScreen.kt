package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
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
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SignalCellular0Bar
import androidx.compose.material.icons.filled.SignalCellularAlt1Bar
import androidx.compose.material.icons.filled.SignalCellularAlt2Bar
import androidx.compose.material.icons.filled.SignalCellularAlt
import androidx.compose.material.icons.filled.SignalCellular4Bar
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.data.ble.BleManager
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DevicesScreen(
    onNavigateBack: () -> Unit,
    onNavigateToTelemetry: (String) -> Unit = {},
    viewModel: DevicesViewModel = hiltViewModel(),
) {
    val devices by viewModel.discoveredDevices.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val isScanning = connectionState is BleManager.ConnectionState.Connecting && devices.isEmpty()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Devices", fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = { viewModel.startScan() }) {
                        Icon(Icons.Default.Search, contentDescription = "Scan")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            // Connection status bar
            ConnectionStatusBar(connectionState = connectionState)

            if (isScanning && devices.isEmpty()) {
                // Radar animation while scanning
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    contentAlignment = Alignment.Center,
                ) {
                    RadarView()
                }
            } else if (devices.isEmpty()) {
                // Empty state
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    contentAlignment = Alignment.Center,
                ) {
                    EmptyDevicesState(onScan = { viewModel.startScan() })
                }
            } else {
                LazyColumn(
                    contentPadding = PaddingValues(16.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    item(key = "devices_header") {
                        Text(
                            "Nearby Devices",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.padding(bottom = 4.dp),
                        )
                    }
                    items(devices, key = { it.address }) { device ->
                        DeviceCard(
                            device = device,
                            isConnected = connectionState is BleManager.ConnectionState.Connected,
                            onClick = {
                                if (connectionState is BleManager.ConnectionState.Connected) {
                                    onNavigateToTelemetry(device.address)
                                } else {
                                    viewModel.connect(device.address)
                                }
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ConnectionStatusBar(connectionState: BleManager.ConnectionState) {
    val (icon, label, tintColor) = when (connectionState) {
        is BleManager.ConnectionState.Connected ->
            Triple(Icons.Default.BluetoothConnected, "Connected", VidyuthSuccess)
        is BleManager.ConnectionState.Connecting ->
            Triple(Icons.Default.BluetoothSearching, "Scanning...", VidyuthSecondary)
        is BleManager.ConnectionState.Error ->
            Triple(Icons.Default.BluetoothDisabled, "Error: ${connectionState.message}", VidyuthError)
        else ->
            Triple(Icons.Default.BluetoothDisabled, "Not connected", MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = tintColor.copy(alpha = 0.08f),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 20.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(icon, null, tint = tintColor, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(10.dp))
            Text(label, color = tintColor, fontWeight = FontWeight.Medium, fontSize = 14.sp)
        }
    }
}

@Composable
private fun RadarView() {
    val infiniteTransition = rememberInfiniteTransition(label = "radar")
    val ring1Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring1",
    )
    val ring1Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring1_alpha",
    )
    val ring2Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 800),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring2",
    )
    val ring2Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 800),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring2_alpha",
    )
    val ring3Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 1600),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring3",
    )
    val ring3Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 1600),
            repeatMode = RepeatMode.Restart,
        ),
        label = "ring3_alpha",
    )

    Box(contentAlignment = Alignment.Center) {
        // Ring 1
        Box(
            modifier = Modifier
                .size(200.dp)
                .scale(ring1Scale)
                .alpha(ring1Alpha)
                .clip(CircleShape)
                .background(VidyuthPrimary.copy(alpha = 0.25f)),
        )
        // Ring 2
        Box(
            modifier = Modifier
                .size(200.dp)
                .scale(ring2Scale)
                .alpha(ring2Alpha)
                .clip(CircleShape)
                .background(VidyuthPrimary.copy(alpha = 0.20f)),
        )
        // Ring 3
        Box(
            modifier = Modifier
                .size(200.dp)
                .scale(ring3Scale)
                .alpha(ring3Alpha)
                .clip(CircleShape)
                .background(VidyuthPrimary.copy(alpha = 0.15f)),
        )
        // Center icon
        Box(
            modifier = Modifier
                .size(72.dp)
                .clip(CircleShape)
                .background(VidyuthPrimary),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.Bluetooth,
                contentDescription = null,
                tint = Color.White,
                modifier = Modifier.size(36.dp),
            )
        }
    }

    Spacer(Modifier.height(24.dp))

    Text(
        "Scanning for Parakram devices...",
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
        fontSize = 14.sp,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DeviceCard(
    device: BleManager.BleDevice,
    isConnected: Boolean,
    onClick: () -> Unit,
) {
    Card(
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
        onClick = onClick,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // BLE icon
            Box(
                modifier = Modifier
                    .size(46.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(VidyuthPrimary.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    Icons.Default.DeveloperBoard,
                    contentDescription = null,
                    tint = VidyuthPrimary,
                    modifier = Modifier.size(26.dp),
                )
            }

            Spacer(Modifier.width(14.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(device.name, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                Text(
                    device.address,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
                )
            }

            // Signal bars
            SignalBars(rssi = device.rssi, modifier = Modifier.size(22.dp))

            Spacer(Modifier.width(12.dp))

            // Status badge
            if (isConnected) {
                Surface(
                    shape = RoundedCornerShape(8.dp),
                    color = VidyuthSuccess.copy(alpha = 0.12f),
                ) {
                    Text(
                        "Connected",
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                        fontSize = 11.sp,
                        color = VidyuthSuccess,
                        fontWeight = FontWeight.Medium,
                    )
                }
            } else {
                Icon(
                    Icons.Default.ChevronRight,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
                )
            }
        }
    }
}

@Composable
private fun SignalBars(rssi: Int, modifier: Modifier = Modifier) {
    val strength = when {
        rssi >= -60 -> 4
        rssi >= -70 -> 3
        rssi >= -80 -> 2
        rssi >= -90 -> 1
        else -> 0
    }
    val icon: ImageVector = when (strength) {
        4 -> Icons.Default.SignalCellular4Bar
        3 -> Icons.Default.SignalCellularAlt
        2 -> Icons.Default.SignalCellularAlt2Bar
        1 -> Icons.Default.SignalCellularAlt1Bar
        else -> Icons.Default.SignalCellular0Bar
    }
    val tintColor = when (strength) {
        4, 3 -> VidyuthSuccess
        2 -> VidyuthSecondary
        1 -> MaterialTheme.colorScheme.error
        else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f)
    }
    Icon(icon, contentDescription = "$rssi dBm", tint = tintColor, modifier = modifier)
}

@Composable
private fun EmptyDevicesState(onScan: () -> Unit) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.padding(32.dp),
    ) {
        Icon(
            Icons.Default.BluetoothSearching,
            contentDescription = null,
            modifier = Modifier.size(72.dp),
            tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f),
        )
        Text(
            "No devices found",
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
        )
        Text(
            "Make sure your Parakram device is powered on",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
        )
        Spacer(Modifier.height(8.dp))
        Surface(
            shape = RoundedCornerShape(12.dp),
            color = VidyuthPrimary,
            modifier = Modifier.clip(RoundedCornerShape(12.dp)),
            onClick = onScan,
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(Icons.Default.BluetoothSearching, null, tint = Color.White)
                Text("Scan Now", color = Color.White, fontWeight = FontWeight.SemiBold)
            }
        }
    }
}
