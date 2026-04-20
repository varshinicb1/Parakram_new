package com.vidyuthlabs.parakram.ui.screens

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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.CreditCard
import androidx.compose.material.icons.filled.DarkMode
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Key
import androidx.compose.material.icons.filled.Logout
import androidx.compose.material.icons.filled.Store
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.hilt.navigation.compose.hiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess

private const val AppVersion = "1.0.0"
private const val CompanyWebsite = "vidyuthlabs.co.in"
private const val DefaultBackendUrl = "https://api.parakram.com/"

private val LogLevels = listOf("Off", "Error", "Warn", "Info", "Debug", "Verbose")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    onNavigateBack: () -> Unit,
    onSignOut: () -> Unit = {},
    onNavigateToSubmissions: () -> Unit = {},
    userEmail: String = "",
    userName: String = "User",
    planName: String = "Free",
    viewModel: SettingsViewModel = hiltViewModel(),
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val uiState by viewModel.uiState.collectAsState()

    var backendUrl by remember { mutableStateOf(DefaultBackendUrl) }
    var darkModeEnabled by remember { mutableStateOf(true) }
    var bleAutoConnect by remember { mutableStateOf(false) }
    var showRawIr by remember { mutableStateOf(false) }
    var logLevel by remember { mutableStateOf("Info") }
    var logLevelExpanded by remember { mutableStateOf(false) }
    var connectionTestResult by remember { mutableStateOf<Boolean?>(null) }
    var isTesting by remember { mutableStateOf(false) }
    var llmApiKey by remember { mutableStateOf("") }
    var llmKeyVisible by remember { mutableStateOf(false) }
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(uiState.llmKeySaveResult) {
        uiState.llmKeySaveResult?.let {
            snackbarHostState.showSnackbar(it)
            viewModel.clearSaveResult()
        }
    }

    Scaffold(
        snackbarHost = {
            SnackbarHost(snackbarHostState) { data ->
                Snackbar(snackbarData = data)
            }
        },
        topBar = {
            TopAppBar(
                title = { Text("Settings", fontWeight = FontWeight.Bold) },
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
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp),
        ) {
            // Profile header card
            item(key = "profile") {
                ProfileCard(
                    userName = userName,
                    userEmail = userEmail,
                    planName = planName,
                )
            }

            // Connection section
            item(key = "connection_header") {
                SectionHeader(label = "Connection")
            }
            item(key = "connection_content") {
                SettingsCard {
                    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        OutlinedTextField(
                            value = backendUrl,
                            onValueChange = { backendUrl = it },
                            modifier = Modifier.fillMaxWidth(),
                            label = { Text("Backend URL") },
                            leadingIcon = { Icon(Icons.Default.Cloud, null) },
                            shape = RoundedCornerShape(12.dp),
                            singleLine = true,
                        )

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Button(
                                onClick = {
                                    scope.launch {
                                        isTesting = true
                                        connectionTestResult = null
                                        connectionTestResult = withContext(Dispatchers.IO) {
                                            try {
                                                val url = java.net.URL(
                                                    backendUrl.trimEnd('/') + "/api/system/health"
                                                )
                                                val conn = url.openConnection() as java.net.HttpURLConnection
                                                conn.connectTimeout = 4000
                                                conn.readTimeout = 4000
                                                val code = conn.responseCode
                                                conn.disconnect()
                                                code in 200..299
                                            } catch (_: Exception) { false }
                                        }
                                        isTesting = false
                                    }
                                },
                                enabled = !isTesting,
                                modifier = Modifier.weight(1f),
                                shape = RoundedCornerShape(10.dp),
                                colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
                            ) {
                                if (isTesting) {
                                    androidx.compose.material3.CircularProgressIndicator(
                                        modifier = Modifier.size(16.dp),
                                        strokeWidth = 2.dp,
                                        color = androidx.compose.ui.graphics.Color.White,
                                    )
                                } else {
                                    Icon(Icons.Default.Wifi, null, modifier = Modifier.size(16.dp))
                                }
                                Spacer(Modifier.width(6.dp))
                                Text("Test Connection", fontSize = 13.sp)
                            }

                            connectionTestResult?.let { success ->
                                Icon(
                                    Icons.Default.CheckCircle,
                                    null,
                                    tint = if (success) VidyuthSuccess else VidyuthError,
                                    modifier = Modifier.size(24.dp),
                                )
                            }
                        }
                    }
                }
            }

            // AI / LLM section
            item(key = "ai_header") {
                SectionHeader(label = "AI / OpenRouter")
            }
            item(key = "ai_content") {
                SettingsCard {
                    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        OutlinedTextField(
                            value = llmApiKey,
                            onValueChange = { llmApiKey = it },
                            modifier = Modifier.fillMaxWidth(),
                            label = { Text("OpenRouter API Key") },
                            placeholder = { Text("sk-or-v1-...") },
                            leadingIcon = { Icon(Icons.Default.Key, null) },
                            trailingIcon = {
                                IconButton(onClick = { llmKeyVisible = !llmKeyVisible }) {
                                    Icon(
                                        if (llmKeyVisible) Icons.Default.VisibilityOff else Icons.Default.Visibility,
                                        contentDescription = if (llmKeyVisible) "Hide" else "Show",
                                    )
                                }
                            },
                            visualTransformation = if (llmKeyVisible) VisualTransformation.None
                                                   else PasswordVisualTransformation(),
                            shape = RoundedCornerShape(12.dp),
                            singleLine = true,
                        )
                        Text(
                            "Your key is stored on the server and used for AI intent processing. Get a free key at openrouter.ai",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        )
                        Button(
                            onClick = { viewModel.saveLlmKey(llmApiKey) },
                            enabled = !uiState.isSavingKey,
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(10.dp),
                            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
                        ) {
                            if (uiState.isSavingKey) {
                                androidx.compose.material3.CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                    color = androidx.compose.ui.graphics.Color.White,
                                )
                                Spacer(Modifier.width(6.dp))
                            }
                            Text("Save API Key")
                        }
                    }
                }
            }

            // Preferences section
            item(key = "prefs_header") {
                SectionHeader(label = "Preferences")
            }
            item(key = "prefs_content") {
                SettingsCard {
                    Column {
                        ToggleRow(
                            icon = Icons.Default.DarkMode,
                            label = "Dark Mode",
                            checked = darkModeEnabled,
                            onCheckedChange = { darkModeEnabled = it },
                        )
                        SettingsDivider()
                        ToggleRow(
                            icon = Icons.Default.Cloud,
                            label = "BLE Auto-Connect",
                            checked = bleAutoConnect,
                            onCheckedChange = { bleAutoConnect = it },
                        )
                    }
                }
            }

            // Developer section
            item(key = "dev_header") {
                SectionHeader(label = "Developer")
            }
            item(key = "dev_content") {
                SettingsCard {
                    Column {
                        ToggleRow(
                            icon = Icons.Default.Code,
                            label = "Show Raw IR",
                            checked = showRawIr,
                            onCheckedChange = { showRawIr = it },
                        )
                        SettingsDivider()
                        ExposedDropdownMenuBox(
                            expanded = logLevelExpanded,
                            onExpandedChange = { logLevelExpanded = it },
                        ) {
                            OutlinedTextField(
                                value = logLevel,
                                onValueChange = {},
                                readOnly = true,
                                label = { Text("Log Level") },
                                leadingIcon = { Icon(Icons.Default.BugReport, null) },
                                trailingIcon = {
                                    ExposedDropdownMenuDefaults.TrailingIcon(expanded = logLevelExpanded)
                                },
                                shape = RoundedCornerShape(12.dp),
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .menuAnchor()
                                    .padding(top = 12.dp),
                            )
                            ExposedDropdownMenu(
                                expanded = logLevelExpanded,
                                onDismissRequest = { logLevelExpanded = false },
                            ) {
                                LogLevels.forEach { level ->
                                    DropdownMenuItem(
                                        text = { Text(level) },
                                        onClick = {
                                            logLevel = level
                                            logLevelExpanded = false
                                        },
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // Account section
            item(key = "account_header") {
                SectionHeader(label = "Account")
            }
            item(key = "account_content") {
                SettingsCard {
                    Column {
                        NavigationRow(
                            icon = Icons.Default.Store,
                            label = "My Submissions",
                            onClick = onNavigateToSubmissions,
                        )
                        SettingsDivider()
                        NavigationRow(
                            icon = Icons.Default.Logout,
                            label = "Sign Out",
                            tint = VidyuthError,
                            onClick = onSignOut,
                        )
                    }
                }
            }

            // About section
            item(key = "about_header") {
                SectionHeader(label = "About")
            }
            item(key = "about_content") {
                SettingsCard {
                    Column {
                        InfoRow(label = "Version", value = AppVersion)
                        SettingsDivider()
                        InfoRow(label = "License", value = "MIT Open Source")
                        SettingsDivider()
                        NavigationRow(
                            icon = Icons.Default.Info,
                            label = CompanyWebsite,
                            onClick = {
                                context.startActivity(
                                    Intent(Intent.ACTION_VIEW, Uri.parse("https://$CompanyWebsite"))
                                )
                            },
                        )
                    }
                }
            }

            item(key = "footer") { Spacer(Modifier.height(8.dp)) }
        }
    }
}

@Composable
private fun ProfileCard(
    userName: String,
    userEmail: String,
    planName: String,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Row(
            modifier = Modifier.padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // Avatar with initials
            val initials = userName.take(2).uppercase().ifBlank { "U" }
            Box(
                modifier = Modifier
                    .size(56.dp)
                    .clip(CircleShape)
                    .background(VidyuthPrimary),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    initials,
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 20.sp,
                )
            }

            Spacer(Modifier.width(16.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    userName.ifBlank { "Parakram User" },
                    fontWeight = FontWeight.Bold,
                    fontSize = 16.sp,
                )
                if (userEmail.isNotBlank()) {
                    Text(
                        userEmail,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f),
                    )
                }
            }

            // Plan badge
            Surface(
                shape = RoundedCornerShape(10.dp),
                color = VidyuthPrimary.copy(alpha = 0.15f),
            ) {
                Text(
                    planName,
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                    fontSize = 12.sp,
                    color = VidyuthPrimary,
                    fontWeight = FontWeight.Bold,
                )
            }
        }
    }
}

@Composable
private fun SectionHeader(label: String) {
    Text(
        label.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.45f),
        letterSpacing = 1.2.sp,
        modifier = Modifier.padding(horizontal = 4.dp),
    )
}

@Composable
private fun SettingsCard(content: @Composable () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Box(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
            content()
        }
    }
}

@Composable
private fun ToggleRow(
    icon: ImageVector,
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    tint: Color = VidyuthPrimary,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, null, tint = tint, modifier = Modifier.size(20.dp))
        Spacer(Modifier.width(12.dp))
        Text(label, modifier = Modifier.weight(1f), fontWeight = FontWeight.Medium)
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(checkedThumbColor = Color.White, checkedTrackColor = VidyuthPrimary),
        )
    }
}

@Composable
private fun NavigationRow(
    icon: ImageVector,
    label: String,
    tint: Color = VidyuthPrimary,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, null, tint = tint, modifier = Modifier.size(20.dp))
        Spacer(Modifier.width(12.dp))
        Text(
            label,
            modifier = Modifier.weight(1f),
            color = tint,
            fontWeight = FontWeight.Medium,
        )
        Icon(
            Icons.Default.ChevronRight,
            null,
            tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
            modifier = Modifier.size(18.dp),
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.75f),
        )
        Text(
            value,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
        )
    }
}

@Composable
private fun SettingsDivider() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f)),
    )
}
