package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SignalCellular0Bar
import androidx.compose.material.icons.filled.SignalCellularAlt1Bar
import androidx.compose.material.icons.filled.SignalCellularAlt2Bar
import androidx.compose.material.icons.filled.SignalCellularAlt
import androidx.compose.material.icons.filled.SignalCellular4Bar
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.data.ble.BleManager
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthOnPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthSurface
import com.vidyuthlabs.parakram.ui.viewmodels.PairDeviceViewModel
import com.vidyuthlabs.parakram.ui.viewmodels.PairingState

private data class BoardOption(val displayName: String, val sku: String)

private val boardOptions = listOf(
    BoardOption("ESP32-S3", "vdyt-s3-r1"),
    BoardOption("RP2040", "rp2040"),
    BoardOption("STM32F4", "stm32f4"),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PairDeviceScreen(
    onNavigateBack: () -> Unit,
    onPairSuccess: () -> Unit,
    authToken: String = "",
    viewModel: PairDeviceViewModel = hiltViewModel(),
) {
    val discoveredDevices by viewModel.discoveredDevices.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()
    val uiState by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    val isScanning = connectionState is BleManager.ConnectionState.Connecting && discoveredDevices.isEmpty()

    // Step derived from uiState
    val step = when {
        uiState.pairingState == PairingState.SUCCESS -> 2
        uiState.selectedDevice != null -> 1
        else -> 0
    }

    LaunchedEffect(uiState.error) {
        val err = uiState.error
        if (err != null) {
            snackbarHostState.showSnackbar(err)
            viewModel.resetPairingState()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = when (step) {
                            0 -> "Pair a Device"
                            1 -> "Name Your Device"
                            else -> "Device Paired"
                        },
                        fontWeight = FontWeight.Bold,
                    )
                },
                navigationIcon = {
                    IconButton(onClick = {
                        when (step) {
                            1 -> viewModel.clearSelection()
                            2 -> onNavigateBack()
                            else -> onNavigateBack()
                        }
                    }) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    if (step == 0) {
                        IconButton(onClick = { viewModel.startScan() }) {
                            Icon(Icons.Default.Search, contentDescription = "Scan")
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState) { data ->
                Snackbar(
                    snackbarData = data,
                    containerColor = Color(0xFFFF5252),
                    contentColor = Color.White,
                    shape = RoundedCornerShape(12.dp),
                )
            }
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        AnimatedContent(
            targetState = step,
            transitionSpec = {
                slideInHorizontally { width -> width } + fadeIn() togetherWith
                    slideOutHorizontally { width -> -width } + fadeOut()
            },
            label = "pair_step",
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) { currentStep ->
            when (currentStep) {
                0 -> PairStep1SelectDevice(
                    devices = discoveredDevices,
                    connectionState = connectionState,
                    isScanning = isScanning,
                    onScan = { viewModel.startScan() },
                    onSelectDevice = { viewModel.selectDevice(it) },
                )
                1 -> PairStep2NameDevice(
                    selectedDevice = uiState.selectedDevice!!,
                    isPairing = uiState.pairingState == PairingState.PAIRING,
                    onPair = { name, boardSku ->
                        viewModel.pairDevice(name, boardSku, authToken)
                    },
                    onBack = { viewModel.clearSelection() },
                )
                else -> PairStep3Success(
                    deviceName = uiState.pairedDevice?.name
                        ?: uiState.selectedDevice?.name
                        ?: "Your Device",
                    onGoToDevices = onPairSuccess,
                )
            }
        }
    }
}

// ---- Step 1: Select BLE device ----

@Composable
private fun PairStep1SelectDevice(
    devices: List<BleManager.BleDevice>,
    connectionState: BleManager.ConnectionState,
    isScanning: Boolean,
    onScan: () -> Unit,
    onSelectDevice: (BleManager.BleDevice) -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        PairConnectionStatusBar(connectionState = connectionState)

        if (isScanning && devices.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                contentAlignment = Alignment.Center,
            ) {
                PairRadarView()
            }
        } else if (devices.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                contentAlignment = Alignment.Center,
            ) {
                PairEmptyState(onScan = onScan)
            }
        } else {
            LazyColumn(
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                item(key = "pair_header") {
                    Text(
                        "Nearby Devices",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(bottom = 4.dp),
                    )
                }
                items(devices, key = { it.address }) { device ->
                    PairDeviceCard(
                        device = device,
                        onClick = { onSelectDevice(device) },
                    )
                }
            }
        }
    }
}

@Composable
private fun PairConnectionStatusBar(connectionState: BleManager.ConnectionState) {
    val (icon, label, tintColor) = when (connectionState) {
        is BleManager.ConnectionState.Connected ->
            Triple(Icons.Default.BluetoothConnected, "Connected", VidyuthSuccess)
        is BleManager.ConnectionState.Connecting ->
            Triple(Icons.Default.BluetoothSearching, "Scanning...", VidyuthSecondary)
        is BleManager.ConnectionState.Error ->
            Triple(Icons.Default.BluetoothDisabled, "Error: ${connectionState.message}", VidyuthError)
        else ->
            Triple(
                Icons.Default.BluetoothDisabled,
                "Tap scan to find nearby Parakram devices",
                MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
            )
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PairDeviceCard(
    device: BleManager.BleDevice,
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

            PairSignalBars(rssi = device.rssi, modifier = Modifier.size(22.dp))

            Spacer(Modifier.width(12.dp))

            Icon(
                Icons.Default.ChevronRight,
                contentDescription = "Select",
                tint = VidyuthPrimary.copy(alpha = 0.7f),
            )
        }
    }
}

@Composable
private fun PairSignalBars(rssi: Int, modifier: Modifier = Modifier) {
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
private fun PairRadarView() {
    val infiniteTransition = rememberInfiniteTransition(label = "pair_radar")
    val ring1Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring1",
    )
    val ring1Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring1_alpha",
    )
    val ring2Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 800),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring2",
    )
    val ring2Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 800),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring2_alpha",
    )
    val ring3Scale by infiniteTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1.5f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 1600),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring3",
    )
    val ring3Alpha by infiniteTransition.animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing, delayMillis = 1600),
            repeatMode = RepeatMode.Restart,
        ),
        label = "pair_ring3_alpha",
    )

    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(contentAlignment = Alignment.Center) {
            Box(
                modifier = Modifier
                    .size(200.dp)
                    .scale(ring1Scale)
                    .alpha(ring1Alpha)
                    .clip(CircleShape)
                    .background(VidyuthPrimary.copy(alpha = 0.25f)),
            )
            Box(
                modifier = Modifier
                    .size(200.dp)
                    .scale(ring2Scale)
                    .alpha(ring2Alpha)
                    .clip(CircleShape)
                    .background(VidyuthPrimary.copy(alpha = 0.20f)),
            )
            Box(
                modifier = Modifier
                    .size(200.dp)
                    .scale(ring3Scale)
                    .alpha(ring3Alpha)
                    .clip(CircleShape)
                    .background(VidyuthPrimary.copy(alpha = 0.15f)),
            )
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
}

@Composable
private fun PairEmptyState(onScan: () -> Unit) {
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
            "Make sure your Parakram device is powered on and in pairing mode",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
            textAlign = TextAlign.Center,
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

// ---- Step 2: Name the device ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PairStep2NameDevice(
    selectedDevice: BleManager.BleDevice,
    isPairing: Boolean,
    onPair: (name: String, boardSku: String) -> Unit,
    onBack: () -> Unit,
) {
    var deviceName by remember(selectedDevice.name) { mutableStateOf(selectedDevice.name) }
    var selectedBoard by remember { mutableStateOf(boardOptions.first()) }
    var boardDropdownExpanded by remember { mutableStateOf(false) }

    val nameError = when {
        deviceName.isBlank() -> "Device name is required"
        deviceName.length > 32 -> "Must be 32 characters or fewer"
        else -> null
    }
    val isFormValid = nameError == null && !isPairing

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 20.dp, vertical = 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        // Selected device summary card
        Card(
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = VidyuthPrimary.copy(alpha = 0.08f),
            ),
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(VidyuthPrimary.copy(alpha = 0.18f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Default.Bluetooth,
                        contentDescription = null,
                        tint = VidyuthPrimary,
                        modifier = Modifier.size(22.dp),
                    )
                }
                Spacer(Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        selectedDevice.name,
                        fontWeight = FontWeight.SemiBold,
                        fontSize = 14.sp,
                        color = MaterialTheme.colorScheme.onSurface,
                    )
                    Text(
                        selectedDevice.address,
                        fontSize = 12.sp,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
                    )
                }
                IconButton(onClick = onBack) {
                    Icon(
                        Icons.Default.Edit,
                        contentDescription = "Change device",
                        tint = VidyuthPrimary,
                        modifier = Modifier.size(18.dp),
                    )
                }
            }
        }

        Text(
            "Set up your device",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
        )

        // Device name field
        Column {
            OutlinedTextField(
                value = deviceName,
                onValueChange = { if (it.length <= 32) deviceName = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Device Name") },
                leadingIcon = {
                    Icon(Icons.Default.DeveloperBoard, contentDescription = null)
                },
                isError = nameError != null && deviceName.isNotEmpty(),
                shape = RoundedCornerShape(14.dp),
                singleLine = true,
                keyboardOptions = KeyboardOptions(
                    capitalization = KeyboardCapitalization.Words,
                    keyboardType = KeyboardType.Text,
                    imeAction = ImeAction.Done,
                ),
                colors = pairTextFieldColors(),
            )
            if (nameError != null && deviceName.isNotEmpty()) {
                Text(
                    text = nameError,
                    color = VidyuthError,
                    fontSize = 12.sp,
                    modifier = Modifier.padding(start = 16.dp, top = 4.dp),
                )
            }
        }

        // Board type picker
        Column {
            Text(
                "Board Type",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                modifier = Modifier.padding(start = 4.dp, bottom = 8.dp),
            )
            Box {
                OutlinedTextField(
                    value = selectedBoard.displayName,
                    onValueChange = {},
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text("Board Type") },
                    leadingIcon = {
                        Icon(Icons.Default.DeveloperBoard, contentDescription = null)
                    },
                    trailingIcon = {
                        IconButton(onClick = { boardDropdownExpanded = true }) {
                            Icon(Icons.Default.ArrowDropDown, contentDescription = "Select board")
                        }
                    },
                    readOnly = true,
                    shape = RoundedCornerShape(14.dp),
                    singleLine = true,
                    colors = pairTextFieldColors(),
                )
                DropdownMenu(
                    expanded = boardDropdownExpanded,
                    onDismissRequest = { boardDropdownExpanded = false },
                    modifier = Modifier.background(MaterialTheme.colorScheme.surface),
                ) {
                    boardOptions.forEach { option ->
                        DropdownMenuItem(
                            text = {
                                Column {
                                    Text(option.displayName, fontWeight = FontWeight.Medium)
                                    Text(
                                        option.sku,
                                        fontSize = 11.sp,
                                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
                                    )
                                }
                            },
                            onClick = {
                                selectedBoard = option
                                boardDropdownExpanded = false
                            },
                            colors = MenuDefaults.itemColors(
                                textColor = MaterialTheme.colorScheme.onSurface,
                            ),
                        )
                    }
                }
            }
            Text(
                "SKU: ${selectedBoard.sku}",
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.35f),
                modifier = Modifier.padding(start = 16.dp, top = 4.dp),
            )
        }

        Spacer(Modifier.weight(1f))

        // Pair Device button with gradient
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
                .clip(RoundedCornerShape(28.dp))
                .background(
                    brush = if (!isFormValid) SolidColor(Color.Gray.copy(alpha = 0.4f))
                    else Brush.horizontalGradient(listOf(VidyuthPrimary, Color(0xFF8B5CF6))),
                ),
            contentAlignment = Alignment.Center,
        ) {
            Button(
                onClick = { if (isFormValid) onPair(deviceName, selectedBoard.sku) },
                modifier = Modifier.fillMaxSize(),
                shape = RoundedCornerShape(28.dp),
                enabled = isFormValid,
                colors = ButtonDefaults.buttonColors(
                    containerColor = Color.Transparent,
                    disabledContainerColor = Color.Transparent,
                ),
                elevation = ButtonDefaults.buttonElevation(0.dp, 0.dp),
            ) {
                AnimatedVisibility(visible = isPairing, enter = fadeIn(), exit = fadeOut()) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            color = VidyuthOnPrimary,
                            strokeWidth = 2.dp,
                        )
                        Text(
                            "Pairing...",
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Bold,
                            color = VidyuthOnPrimary,
                        )
                    }
                }
                AnimatedVisibility(visible = !isPairing, enter = fadeIn(), exit = fadeOut()) {
                    Text(
                        "Pair Device",
                        fontSize = 16.sp,
                        fontWeight = FontWeight.Bold,
                        color = VidyuthOnPrimary,
                    )
                }
            }
        }
    }
}

// ---- Step 3: Success ----

@Composable
private fun PairStep3Success(
    deviceName: String,
    onGoToDevices: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        // Success icon with glow
        Box(
            modifier = Modifier
                .size(120.dp)
                .clip(CircleShape)
                .background(VidyuthSuccess.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.CheckCircle,
                contentDescription = null,
                tint = VidyuthSuccess,
                modifier = Modifier.size(72.dp),
            )
        }

        Spacer(Modifier.height(28.dp))

        Text(
            "Device Paired!",
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(12.dp))

        Card(
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = VidyuthSuccess.copy(alpha = 0.08f),
            ),
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(VidyuthPrimary.copy(alpha = 0.15f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Default.DeveloperBoard,
                        contentDescription = null,
                        tint = VidyuthPrimary,
                        modifier = Modifier.size(24.dp),
                    )
                }
                Column {
                    Text(
                        deviceName,
                        fontWeight = FontWeight.SemiBold,
                        fontSize = 16.sp,
                    )
                    Text(
                        "Device paired successfully!",
                        fontSize = 13.sp,
                        color = VidyuthSuccess,
                        fontWeight = FontWeight.Medium,
                    )
                }
            }
        }

        Spacer(Modifier.height(40.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
                .clip(RoundedCornerShape(28.dp))
                .background(Brush.horizontalGradient(listOf(VidyuthPrimary, Color(0xFF8B5CF6)))),
            contentAlignment = Alignment.Center,
        ) {
            Button(
                onClick = onGoToDevices,
                modifier = Modifier.fillMaxSize(),
                shape = RoundedCornerShape(28.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = Color.Transparent,
                ),
                elevation = ButtonDefaults.buttonElevation(0.dp, 0.dp),
            ) {
                Text(
                    "Go to Devices",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    color = VidyuthOnPrimary,
                )
            }
        }
    }
}

// ---- Shared field colors ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun pairTextFieldColors() = OutlinedTextFieldDefaults.colors(
    focusedBorderColor = VidyuthPrimary,
    unfocusedBorderColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f),
    errorBorderColor = VidyuthError,
    focusedLabelColor = VidyuthPrimary,
    unfocusedLabelColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
    errorLabelColor = VidyuthError,
    cursorColor = VidyuthPrimary,
    focusedTextColor = MaterialTheme.colorScheme.onSurface,
    unfocusedTextColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.85f),
    focusedLeadingIconColor = VidyuthPrimary,
    unfocusedLeadingIconColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
    focusedTrailingIconColor = VidyuthPrimary,
    unfocusedTrailingIconColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
)
