package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.SignalCellular0Bar
import androidx.compose.material.icons.filled.SignalCellular4Bar
import androidx.compose.material.icons.filled.SignalCellularAlt
import androidx.compose.material.icons.filled.SignalCellularAlt1Bar
import androidx.compose.material.icons.filled.SignalCellularAlt2Bar
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.material.icons.filled.Timer
import androidx.compose.material.icons.filled.WaterDrop
import androidx.compose.material.icons.filled.Wifi
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthWarning
import com.vidyuthlabs.parakram.ui.viewmodels.TelemetryReading
import com.vidyuthlabs.parakram.ui.viewmodels.TelemetryViewModel
import com.vidyuthlabs.parakram.ui.viewmodels.WsStatus
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TelemetryScreen(
    deviceId: String,
    deviceName: String = "Device",
    token: String = "",
    onNavigateBack: () -> Unit,
    viewModel: TelemetryViewModel = hiltViewModel(),
) {
    val readings by viewModel.readings.collectAsState()
    val latestReading by viewModel.latestReading.collectAsState()
    val connectionStatus by viewModel.connectionStatus.collectAsState()

    DisposableEffect(deviceId, token) {
        viewModel.connect(deviceId, token)
        onDispose { viewModel.disconnect() }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        LiveDot(isConnected = connectionStatus == WsStatus.CONNECTED)
                        Spacer(Modifier.width(8.dp))
                        Column {
                            Text(
                                deviceName.ifBlank { "Device" },
                                fontWeight = FontWeight.Bold,
                                fontSize = 18.sp,
                            )
                            Text(
                                deviceId,
                                fontSize = 11.sp,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                            )
                        }
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    WsStatusChip(status = connectionStatus, modifier = Modifier.padding(end = 12.dp))
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // Connection status bar
            item(key = "status_bar") {
                WsConnectionStatusBar(status = connectionStatus)
            }

            // Metric cards 2×2 grid
            item(key = "metric_grid") {
                if (latestReading != null) {
                    MetricGrid(reading = latestReading!!)
                } else {
                    WaitingForDataCard()
                }
            }

            // Sparkline chart
            if (readings.size >= 2) {
                item(key = "sparkline") {
                    TemperatureSparklineCard(readings = readings)
                }
            }

            // Footer details
            if (latestReading != null) {
                item(key = "footer") {
                    FooterDetailsCard(reading = latestReading!!)
                }
            }
        }
    }
}

// ---- Live dot ----

@Composable
private fun LiveDot(isConnected: Boolean) {
    val infiniteTransition = rememberInfiniteTransition(label = "live_dot")
    val alpha by infiniteTransition.animateFloat(
        initialValue = 1f,
        targetValue = 0.2f,
        animationSpec = infiniteRepeatable(
            animation = tween(800, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse,
        ),
        label = "dot_alpha",
    )

    Box(
        modifier = Modifier
            .size(10.dp)
            .clip(CircleShape)
            .background(
                (if (isConnected) VidyuthSuccess else Color.Gray)
                    .copy(alpha = if (isConnected) alpha else 0.4f),
            ),
    )
}

// ---- WS status chip ----

@Composable
private fun WsStatusChip(status: WsStatus, modifier: Modifier = Modifier) {
    val (label, color) = when (status) {
        WsStatus.CONNECTING -> "Connecting" to VidyuthWarning
        WsStatus.CONNECTED -> "Live" to VidyuthSuccess
        WsStatus.DISCONNECTED -> "Offline" to MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f)
        WsStatus.ERROR -> "Error" to VidyuthError
    }
    Surface(
        shape = RoundedCornerShape(20.dp),
        color = color.copy(alpha = 0.14f),
        modifier = modifier,
    ) {
        Text(
            label,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp),
            fontSize = 12.sp,
            fontWeight = FontWeight.Medium,
            color = color,
        )
    }
}

// ---- Connection status bar ----

@Composable
private fun WsConnectionStatusBar(status: WsStatus) {
    val (icon, label, color) = when (status) {
        WsStatus.CONNECTING -> Triple(Icons.Default.Wifi, "Connecting to device…", VidyuthWarning)
        WsStatus.CONNECTED -> Triple(Icons.Default.Wifi, "WebSocket connected — live data", VidyuthSuccess)
        WsStatus.DISCONNECTED -> Triple(Icons.Default.Wifi, "Disconnected — waiting to reconnect", MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
        WsStatus.ERROR -> Triple(Icons.Default.Wifi, "Connection error — retrying in 3s", VidyuthError)
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = color.copy(alpha = 0.08f),
        shape = RoundedCornerShape(12.dp),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(icon, contentDescription = null, tint = color, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(10.dp))
            Text(label, color = color, fontWeight = FontWeight.Medium, fontSize = 13.sp)
        }
    }
}

// ---- 2×2 Metric grid ----

@Composable
private fun MetricGrid(reading: TelemetryReading) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MetricCard(
                icon = Icons.Default.Thermostat,
                label = "Temperature",
                value = "%.1f".format(reading.temperature),
                unit = "°C",
                iconBackground = VidyuthError.copy(alpha = 0.14f),
                iconTint = VidyuthError,
                valueColor = VidyuthError,
                modifier = Modifier.weight(1f),
            )
            MetricCard(
                icon = Icons.Default.WaterDrop,
                label = "Humidity",
                value = "%.1f".format(reading.humidity),
                unit = "%",
                iconBackground = VidyuthSecondary.copy(alpha = 0.14f),
                iconTint = VidyuthSecondary,
                valueColor = VidyuthSecondary,
                modifier = Modifier.weight(1f),
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            val (hours, minutes) = uptimeToHoursMinutes(reading.uptimeSeconds)
            MetricCard(
                icon = Icons.Default.Timer,
                label = "Uptime",
                value = if (hours > 0) "${hours}h ${minutes}m" else "${minutes}m",
                unit = "",
                iconBackground = VidyuthPrimary.copy(alpha = 0.14f),
                iconTint = VidyuthPrimary,
                valueColor = VidyuthPrimary,
                modifier = Modifier.weight(1f),
            )
            val (sigIcon, sigColor) = rssiToSignalIconAndColor(reading.rssi)
            MetricCard(
                icon = sigIcon,
                label = "Signal",
                value = "${reading.rssi}",
                unit = " dBm",
                iconBackground = sigColor.copy(alpha = 0.14f),
                iconTint = sigColor,
                valueColor = sigColor,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun MetricCard(
    icon: ImageVector,
    label: String,
    value: String,
    unit: String,
    iconBackground: Color,
    iconTint: Color,
    valueColor: Color,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(iconBackground),
                contentAlignment = Alignment.Center,
            ) {
                Icon(icon, contentDescription = label, tint = iconTint, modifier = Modifier.size(22.dp))
            }
            Spacer(Modifier.height(10.dp))
            Text(
                label,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f),
            )
            Spacer(Modifier.height(2.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    value,
                    fontSize = 26.sp,
                    fontWeight = FontWeight.Bold,
                    color = valueColor,
                )
                if (unit.isNotEmpty()) {
                    Text(
                        unit,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Medium,
                        color = valueColor.copy(alpha = 0.7f),
                        modifier = Modifier.padding(bottom = 3.dp, start = 2.dp),
                    )
                }
            }
        }
    }
}

// ---- Sparkline chart ----

@Composable
private fun TemperatureSparklineCard(readings: List<TelemetryReading>) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "Temperature History",
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 14.sp,
                )
                Text(
                    "last ${readings.size} readings",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
                )
            }
            Spacer(Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text("50°C", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f))
                Text("0°C", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f))
            }
            Spacer(Modifier.height(4.dp))
            TemperatureSparkline(
                readings = readings,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(100.dp),
            )
            Spacer(Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text("Oldest", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f))
                Text("Latest", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f))
            }
        }
    }
}

@Composable
private fun TemperatureSparkline(
    readings: List<TelemetryReading>,
    modifier: Modifier = Modifier,
) {
    if (readings.size < 2) return

    // Capture colors outside Canvas lambda (Compose constraint)
    val lineColor = VidyuthError
    val fillStart = VidyuthError.copy(alpha = 0.25f)
    val fillEnd = VidyuthError.copy(alpha = 0.02f)
    val dotColor = VidyuthError

    val temperatures = remember(readings) { readings.map { it.temperature } }

    Canvas(modifier = modifier) {
        val minY = 0f
        val maxY = 50f
        val range = maxY - minY
        val count = temperatures.size
        val step = size.width / (count - 1).coerceAtLeast(1)

        fun xAt(i: Int) = i * step
        fun yAt(v: Float) = size.height - ((v - minY) / range).coerceIn(0f, 1f) * size.height

        // Build line path
        val linePath = Path()
        temperatures.forEachIndexed { i, v ->
            if (i == 0) linePath.moveTo(xAt(i), yAt(v))
            else linePath.lineTo(xAt(i), yAt(v))
        }

        // Build fill path (close below)
        val fillPath = Path()
        fillPath.addPath(linePath)
        fillPath.lineTo(xAt(count - 1), size.height)
        fillPath.lineTo(xAt(0), size.height)
        fillPath.close()

        // Draw fill
        drawPath(
            path = fillPath,
            brush = Brush.verticalGradient(
                colors = listOf(fillStart, fillEnd),
                startY = 0f,
                endY = size.height,
            ),
        )

        // Draw line
        drawPath(
            path = linePath,
            brush = Brush.horizontalGradient(
                colors = listOf(lineColor.copy(alpha = 0.6f), lineColor),
                startX = 0f,
                endX = size.width,
            ),
            style = Stroke(width = 3f, cap = StrokeCap.Round),
        )

        // Endpoint dot
        val lastX = xAt(count - 1)
        val lastY = yAt(temperatures.last())
        drawCircle(color = dotColor, radius = 6f, center = Offset(lastX, lastY))
        drawCircle(
            color = dotColor.copy(alpha = 0.25f),
            radius = 12f,
            center = Offset(lastX, lastY),
        )
    }
}

// ---- Footer details ----

@Composable
private fun FooterDetailsCard(reading: TelemetryReading) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                "Raw Details",
                fontWeight = FontWeight.SemiBold,
                fontSize = 14.sp,
            )
            DetailRow(label = "RSSI", value = "${reading.rssi} dBm")
            DetailRow(label = "Free Heap", value = "${reading.freeHeap / 1024} KB")
            DetailRow(
                label = "Last Update",
                value = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
                    .format(Date(reading.ts)),
            )
        }
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f),
        )
        Text(
            value,
            style = MaterialTheme.typography.bodySmall,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

// ---- Waiting card ----

@Composable
private fun WaitingForDataCard() {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(40.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Icon(
                Icons.Default.Wifi,
                contentDescription = null,
                modifier = Modifier.size(48.dp),
                tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f),
            )
            Text(
                "Waiting for telemetry…",
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
                fontWeight = FontWeight.Medium,
            )
            Text(
                "Data will appear once the WebSocket is connected",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
            )
        }
    }
}

// ---- Helpers ----

private fun uptimeToHoursMinutes(seconds: Long): Pair<Long, Long> {
    val hours = seconds / 3600
    val minutes = (seconds % 3600) / 60
    return hours to minutes
}

@Composable
private fun rssiToSignalIconAndColor(rssi: Int): Pair<ImageVector, Color> {
    return when {
        rssi >= -60 -> Icons.Default.SignalCellular4Bar to VidyuthSuccess
        rssi >= -70 -> Icons.Default.SignalCellularAlt to VidyuthSuccess
        rssi >= -80 -> Icons.Default.SignalCellularAlt2Bar to VidyuthWarning
        rssi >= -90 -> Icons.Default.SignalCellularAlt1Bar to VidyuthError
        else -> Icons.Default.SignalCellular0Bar to MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f)
    }
}
