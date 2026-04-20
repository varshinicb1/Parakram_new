package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowForward
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.DeveloperBoard
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.RocketLaunch
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.domain.model.DeploymentState
import com.vidyuthlabs.parakram.domain.model.IRPreview
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthTertiary

private val ExamplePrompts = listOf(
    "Read temperature every 10 seconds and log it",
    "Turn on the LED when a button is pressed",
    "Monitor humidity and alert when above 80%",
    "Blink LED 3 times on startup",
    "Control motor speed via potentiometer",
)

private val WizardSteps = listOf("Describe", "Generate", "Compile", "Deploy")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProgramScreen(
    onNavigateBack: () -> Unit,
    onNavigateToDevices: () -> Unit = {},
    viewModel: ProgramViewModel = hiltViewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val deployProgress by viewModel.deploymentProgress.collectAsState()

    // Derive wizard step from deployment state
    val currentStep = when {
        deployProgress.state == DeploymentState.RUNNING -> 3
        deployProgress.state == DeploymentState.TRANSFERRING ||
            deployProgress.state == DeploymentState.VERIFYING -> 2
        deployProgress.state == DeploymentState.COMPILING ||
            deployProgress.state == DeploymentState.COMPILED -> 1
        uiState.irPreview != null -> 1
        else -> 0
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Create Program", fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
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
                .padding(padding)
                .verticalScroll(rememberScrollState()),
        ) {
            // Step indicator
            StepIndicator(
                steps = WizardSteps,
                currentStep = currentStep,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 20.dp),
            )

            // Animated step content
            AnimatedContent(
                targetState = currentStep,
                transitionSpec = {
                    if (targetState > initialState) {
                        slideInHorizontally { it } + fadeIn() togetherWith
                            slideOutHorizontally { -it } + fadeOut()
                    } else {
                        slideInHorizontally { -it } + fadeIn() togetherWith
                            slideOutHorizontally { it } + fadeOut()
                    }
                },
                label = "wizard_step",
            ) { step ->
                when (step) {
                    0 -> DescribeStep(
                        description = uiState.description,
                        onDescriptionChange = viewModel::updateDescription,
                        boardId = uiState.boardId,
                        onBoardIdChange = viewModel::updateBoardId,
                        deviceId = uiState.deviceId,
                        onDeviceIdChange = viewModel::updateDeviceId,
                        onNext = { viewModel.deploy() },
                        isNextEnabled = uiState.description.isNotBlank() && !uiState.isDeploying,
                    )
                    1 -> GenerateStep(
                        irPreview = uiState.irPreview,
                        isLoading = deployProgress.state == DeploymentState.COMPILING,
                        onRegenerate = { viewModel.deploy() },
                    )
                    2 -> CompileStep(
                        progress = deployProgress.progress,
                        message = deployProgress.message,
                        hasError = deployProgress.state == DeploymentState.ERROR,
                        errorMessage = deployProgress.error,
                    )
                    3 -> DeployStep(onViewDevices = onNavigateToDevices)
                    else -> DescribeStep(
                        description = uiState.description,
                        onDescriptionChange = viewModel::updateDescription,
                        boardId = uiState.boardId,
                        onBoardIdChange = viewModel::updateBoardId,
                        deviceId = uiState.deviceId,
                        onDeviceIdChange = viewModel::updateDeviceId,
                        onNext = { viewModel.deploy() },
                        isNextEnabled = uiState.description.isNotBlank() && !uiState.isDeploying,
                    )
                }
            }
        }
    }
}

@Composable
private fun StepIndicator(
    steps: List<String>,
    currentStep: Int,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.Top,
    ) {
        steps.forEachIndexed { index, label ->
            val isDone = index < currentStep
            val isCurrent = index == currentStep
            val circleColor = when {
                isDone -> VidyuthSuccess
                isCurrent -> VidyuthPrimary
                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f)
            }
            val textColor = when {
                isDone || isCurrent -> MaterialTheme.colorScheme.onSurface
                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.35f)
            }

            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Box(
                    modifier = Modifier
                        .size(28.dp)
                        .clip(CircleShape)
                        .background(circleColor)
                        .then(
                            if (!isDone && !isCurrent)
                                Modifier.border(2.dp, circleColor, CircleShape)
                            else Modifier,
                        ),
                    contentAlignment = Alignment.Center,
                ) {
                    if (isDone) {
                        Icon(
                            Icons.Default.CheckCircle,
                            contentDescription = null,
                            tint = Color.White,
                            modifier = Modifier.size(16.dp),
                        )
                    } else {
                        Text(
                            "${index + 1}",
                            color = if (isCurrent) Color.White else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                        )
                    }
                }
                Spacer(Modifier.height(4.dp))
                Text(
                    label,
                    fontSize = 10.sp,
                    color = textColor,
                    fontWeight = if (isCurrent) FontWeight.Bold else FontWeight.Normal,
                )
            }

            // Connecting line
            if (index < steps.size - 1) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(2.dp)
                        .padding(horizontal = 4.dp)
                        .clip(RoundedCornerShape(1.dp))
                        .background(
                            if (index < currentStep) VidyuthSuccess
                            else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.15f),
                        )
                        .align(Alignment.CenterVertically),
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun DescribeStep(
    description: String,
    onDescriptionChange: (String) -> Unit,
    boardId: String,
    onBoardIdChange: (String) -> Unit,
    deviceId: String,
    onDeviceIdChange: (String) -> Unit,
    onNext: () -> Unit,
    isNextEnabled: Boolean,
) {
    var boardDropdownExpanded by remember { mutableStateOf(false) }
    val boards = listOf("VDYT-S3-R1", "ESP32-S3-DevKit", "RP2040-Zero", "STM32F4-Discovery")

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        OutlinedTextField(
            value = description,
            onValueChange = onDescriptionChange,
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 140.dp),
            label = { Text("What should your device do?") },
            placeholder = { Text("e.g., Monitor temperature and turn on the fan when it goes above 30°C") },
            shape = RoundedCornerShape(16.dp),
            maxLines = 8,
        )

        // Example chips
        Text(
            "Examples",
            fontSize = 12.sp,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
        )
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ExamplePrompts.forEach { prompt ->
                Surface(
                    shape = RoundedCornerShape(20.dp),
                    color = VidyuthPrimary.copy(alpha = 0.10f),
                    modifier = Modifier.clickable { onDescriptionChange(prompt) },
                ) {
                    Text(
                        prompt,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                        fontSize = 11.sp,
                        color = VidyuthPrimary,
                    )
                }
            }
        }

        // Board picker
        ExposedDropdownMenuBox(
            expanded = boardDropdownExpanded,
            onExpandedChange = { boardDropdownExpanded = it },
        ) {
            OutlinedTextField(
                value = boardId,
                onValueChange = {},
                readOnly = true,
                label = { Text("Board") },
                leadingIcon = { Icon(Icons.Default.DeveloperBoard, null) },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = boardDropdownExpanded) },
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier
                    .fillMaxWidth()
                    .menuAnchor(),
            )
            ExposedDropdownMenu(
                expanded = boardDropdownExpanded,
                onDismissRequest = { boardDropdownExpanded = false },
            ) {
                boards.forEach { board ->
                    DropdownMenuItem(
                        text = { Text(board) },
                        onClick = {
                            onBoardIdChange(board)
                            boardDropdownExpanded = false
                        },
                    )
                }
            }
        }

        OutlinedTextField(
            value = deviceId,
            onValueChange = onDeviceIdChange,
            modifier = Modifier.fillMaxWidth(),
            label = { Text("Device ID") },
            shape = RoundedCornerShape(12.dp),
            singleLine = true,
            leadingIcon = { Icon(Icons.Default.Code, null) },
        )

        Spacer(Modifier.height(4.dp))

        Button(
            onClick = onNext,
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp),
            shape = RoundedCornerShape(14.dp),
            enabled = isNextEnabled,
            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
        ) {
            Icon(Icons.Default.FlashOn, null)
            Spacer(Modifier.width(8.dp))
            Text("Generate Program", fontWeight = FontWeight.Bold, fontSize = 16.sp)
            Spacer(Modifier.width(8.dp))
            Icon(Icons.Default.ArrowForward, null, modifier = Modifier.size(18.dp))
        }

        Spacer(Modifier.height(16.dp))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GenerateStep(
    irPreview: IRPreview?,
    isLoading: Boolean,
    onRegenerate: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        if (isLoading || irPreview == null) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(200.dp),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(color = VidyuthPrimary)
                    Spacer(Modifier.height(16.dp))
                    Text("Generating program...", color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
                }
            }
        } else {
            // IR Preview card
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
            ) {
                Column(modifier = Modifier.padding(20.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("Generated Program", fontWeight = FontWeight.Bold)
                        TextButton(onClick = onRegenerate) {
                            Icon(Icons.Default.Refresh, null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(4.dp))
                            Text("Regenerate", fontSize = 12.sp)
                        }
                    }

                    Spacer(Modifier.height(8.dp))
                    Text(irPreview.summary, style = MaterialTheme.typography.bodyMedium)

                    if (irPreview.sensorsUsed.isNotEmpty()) {
                        Spacer(Modifier.height(12.dp))
                        Text("Sensors", fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
                        Spacer(Modifier.height(4.dp))
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            irPreview.sensorsUsed.forEach { sensor ->
                                Chip(label = sensor, color = VidyuthSecondary)
                            }
                        }
                    }

                    if (irPreview.actuatorsUsed.isNotEmpty()) {
                        Spacer(Modifier.height(10.dp))
                        Text("Actuators", fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f))
                        Spacer(Modifier.height(4.dp))
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            irPreview.actuatorsUsed.forEach { act ->
                                Chip(label = act, color = VidyuthTertiary)
                            }
                        }
                    }
                }
            }
        }
        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun CompileStep(
    progress: Float,
    message: String,
    hasError: Boolean,
    errorMessage: String?,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(20.dp),
    ) {
        Spacer(Modifier.height(24.dp))

        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(
                containerColor = if (hasError)
                    VidyuthError.copy(alpha = 0.08f)
                else
                    MaterialTheme.colorScheme.surface,
            ),
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                if (!hasError) {
                    CircularProgressIndicator(
                        progress = progress,
                        modifier = Modifier.size(72.dp),
                        color = VidyuthPrimary,
                        trackColor = VidyuthPrimary.copy(alpha = 0.15f),
                        strokeWidth = 6.dp,
                    )
                    Spacer(Modifier.height(20.dp))
                    Text(
                        "${(progress * 100).toInt()}%",
                        fontSize = 22.sp,
                        fontWeight = FontWeight.Bold,
                        color = VidyuthPrimary,
                    )
                } else {
                    Icon(
                        Icons.Default.RocketLaunch,
                        contentDescription = null,
                        modifier = Modifier.size(56.dp),
                        tint = VidyuthError,
                    )
                }
                Spacer(Modifier.height(12.dp))
                Text(
                    message.ifBlank { "Compiling..." },
                    fontWeight = FontWeight.Medium,
                    textAlign = TextAlign.Center,
                )
                if (errorMessage != null) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        errorMessage,
                        color = VidyuthError,
                        style = MaterialTheme.typography.bodySmall,
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }
    }
}

@Composable
private fun DeployStep(onViewDevices: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Spacer(Modifier.height(32.dp))

        Box(
            modifier = Modifier
                .size(100.dp)
                .clip(CircleShape)
                .background(VidyuthSuccess.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.CheckCircle,
                contentDescription = null,
                modifier = Modifier.size(64.dp),
                tint = VidyuthSuccess,
            )
        }

        Text(
            "Deployed!",
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold,
            color = VidyuthSuccess,
        )
        Text(
            "Your program is now running on the device.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(8.dp))

        Button(
            onClick = onViewDevices,
            modifier = Modifier
                .fillMaxWidth()
                .height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
        ) {
            Icon(Icons.Default.DeveloperBoard, null)
            Spacer(Modifier.width(8.dp))
            Text("View in Devices", fontWeight = FontWeight.Bold)
        }

        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun Chip(label: String, color: Color) {
    Surface(
        shape = RoundedCornerShape(8.dp),
        color = color.copy(alpha = 0.12f),
    ) {
        Text(
            label,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp),
            fontSize = 11.sp,
            color = color,
            fontWeight = FontWeight.Medium,
        )
    }
}
