package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.viewmodels.ForgotPasswordStep
import com.vidyuthlabs.parakram.ui.viewmodels.ForgotPasswordViewModel

private val DarkBg = Color(0xFF0F0F23)
private val Surface = Color(0xFF1A1A2E)
private val TextPrimary = Color(0xFFF4F4FB)
private val TextSecondary = Color(0x99F4F4FB)
private val FieldBorder = Color(0x12FFFFFF)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ForgotPasswordScreen(
    onNavigateToLogin: () -> Unit,
    viewModel: ForgotPasswordViewModel = hiltViewModel(),
) {
    val state by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }
    var showNewPass by remember { mutableStateOf(false) }
    var showConfirmPass by remember { mutableStateOf(false) }

    LaunchedEffect(state.error) {
        state.error?.let { snackbarHostState.showSnackbar(it) }
    }

    Scaffold(
        containerColor = DarkBg,
        snackbarHost = {
            SnackbarHost(snackbarHostState) { data ->
                Snackbar(
                    snackbarData = data,
                    containerColor = Color(0xFFFF5252),
                    contentColor = Color.White,
                    shape = RoundedCornerShape(12.dp),
                )
            }
        },
    ) { padding ->
        Box(
            Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            // Ambient orbs
            Box(
                Modifier
                    .size(380.dp)
                    .offset((-80).dp, (-60).dp)
                    .blur(120.dp)
                    .clip(CircleShape)
                    .background(VidyuthPrimary.copy(alpha = 0.09f))
            )
            Box(
                Modifier
                    .size(300.dp)
                    .align(Alignment.BottomEnd)
                    .offset(60.dp, 40.dp)
                    .blur(100.dp)
                    .clip(CircleShape)
                    .background(VidyuthSecondary.copy(alpha = 0.06f))
            )

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Spacer(Modifier.height(48.dp))

                // Logo
                Box(
                    Modifier
                        .size(64.dp)
                        .clip(RoundedCornerShape(18.dp))
                        .background(
                            Brush.linearGradient(listOf(VidyuthPrimary, VidyuthSecondary))
                        ),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Default.Bolt,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(36.dp),
                    )
                }

                Spacer(Modifier.height(20.dp))

                Text(
                    "Reset Password",
                    fontSize = 26.sp,
                    fontWeight = FontWeight.ExtraBold,
                    color = TextPrimary,
                )
                Text(
                    "We'll send a 6-digit code to verify your identity",
                    fontSize = 14.sp,
                    color = TextSecondary,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.padding(top = 6.dp, start = 8.dp, end = 8.dp),
                )

                Spacer(Modifier.height(36.dp))

                // Card
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = Surface),
                    shape = RoundedCornerShape(20.dp),
                    elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
                ) {
                    AnimatedContent(
                        targetState = state.step,
                        transitionSpec = { fadeIn() togetherWith fadeOut() },
                        label = "step",
                    ) { step ->
                        when (step) {
                            is ForgotPasswordStep.EnterUsername -> UsernameStep(
                                username = state.username,
                                isLoading = state.isLoading,
                                onUsernameChange = viewModel::onUsernameChange,
                                onSubmit = viewModel::requestReset,
                            )

                            is ForgotPasswordStep.EnterCode -> CodeStep(
                                code = state.code,
                                newPassword = state.newPassword,
                                confirmPassword = state.confirmPassword,
                                showNewPass = showNewPass,
                                showConfirmPass = showConfirmPass,
                                isLoading = state.isLoading,
                                onCodeChange = viewModel::onCodeChange,
                                onNewPasswordChange = viewModel::onNewPasswordChange,
                                onConfirmPasswordChange = viewModel::onConfirmPasswordChange,
                                onToggleNewPass = { showNewPass = !showNewPass },
                                onToggleConfirmPass = { showConfirmPass = !showConfirmPass },
                                onSubmit = viewModel::submitReset,
                            )

                            is ForgotPasswordStep.Success -> SuccessStep(
                                onGoToLogin = onNavigateToLogin,
                            )
                        }
                    }
                }

                Spacer(Modifier.height(24.dp))

                TextButton(onClick = onNavigateToLogin) {
                    Text("Back to Login", color = VidyuthPrimary, fontWeight = FontWeight.SemiBold)
                }
            }
        }
    }
}

@Composable
private fun UsernameStep(
    username: String,
    isLoading: Boolean,
    onUsernameChange: (String) -> Unit,
    onSubmit: () -> Unit,
) {
    Column(Modifier.padding(24.dp)) {
        Text(
            "Enter your username",
            fontSize = 15.sp,
            fontWeight = FontWeight.SemiBold,
            color = Color(0xFFF4F4FB),
        )
        Spacer(Modifier.height(4.dp))
        Text(
            "We'll log the reset code — check server logs in dev mode.",
            fontSize = 13.sp,
            color = Color(0x66F4F4FB),
        )
        Spacer(Modifier.height(20.dp))

        OutlinedTextField(
            value = username,
            onValueChange = onUsernameChange,
            label = { Text("Username") },
            leadingIcon = { Icon(Icons.Default.AccountCircle, null) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = VidyuthPrimary,
                unfocusedBorderColor = FieldBorder,
                focusedLabelColor = VidyuthPrimary,
                cursorColor = VidyuthPrimary,
            ),
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(Modifier.height(20.dp))

        Button(
            onClick = onSubmit,
            enabled = !isLoading && username.trim().length >= 3,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
        ) {
            if (isLoading) {
                CircularProgressIndicator(color = Color.White, modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
            } else {
                Text("Send Reset Code", fontWeight = FontWeight.Bold, fontSize = 16.sp)
            }
        }
    }
}

@Composable
private fun CodeStep(
    code: String,
    newPassword: String,
    confirmPassword: String,
    showNewPass: Boolean,
    showConfirmPass: Boolean,
    isLoading: Boolean,
    onCodeChange: (String) -> Unit,
    onNewPasswordChange: (String) -> Unit,
    onConfirmPasswordChange: (String) -> Unit,
    onToggleNewPass: () -> Unit,
    onToggleConfirmPass: () -> Unit,
    onSubmit: () -> Unit,
) {
    Column(Modifier.padding(24.dp)) {
        Text(
            "Check your server logs for the 6-digit code",
            fontSize = 13.sp,
            color = Color(0x88F4F4FB),
        )
        Spacer(Modifier.height(16.dp))

        OutlinedTextField(
            value = code,
            onValueChange = { if (it.length <= 6) onCodeChange(it) },
            label = { Text("6-digit Code") },
            leadingIcon = { Icon(Icons.Default.Tag, null) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Next),
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = VidyuthPrimary,
                unfocusedBorderColor = FieldBorder,
                focusedLabelColor = VidyuthPrimary,
                cursorColor = VidyuthPrimary,
            ),
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(Modifier.height(12.dp))

        OutlinedTextField(
            value = newPassword,
            onValueChange = onNewPasswordChange,
            label = { Text("New Password") },
            leadingIcon = { Icon(Icons.Default.Lock, null) },
            trailingIcon = {
                IconButton(onClick = onToggleNewPass) {
                    Icon(if (showNewPass) Icons.Default.VisibilityOff else Icons.Default.Visibility, null)
                }
            },
            visualTransformation = if (showNewPass) VisualTransformation.None else PasswordVisualTransformation(),
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Next),
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = VidyuthPrimary,
                unfocusedBorderColor = FieldBorder,
                focusedLabelColor = VidyuthPrimary,
                cursorColor = VidyuthPrimary,
            ),
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(Modifier.height(12.dp))

        OutlinedTextField(
            value = confirmPassword,
            onValueChange = onConfirmPasswordChange,
            label = { Text("Confirm Password") },
            leadingIcon = { Icon(Icons.Default.Lock, null) },
            trailingIcon = {
                IconButton(onClick = onToggleConfirmPass) {
                    Icon(if (showConfirmPass) Icons.Default.VisibilityOff else Icons.Default.Visibility, null)
                }
            },
            visualTransformation = if (showConfirmPass) VisualTransformation.None else PasswordVisualTransformation(),
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = VidyuthPrimary,
                unfocusedBorderColor = FieldBorder,
                focusedLabelColor = VidyuthPrimary,
                cursorColor = VidyuthPrimary,
            ),
            modifier = Modifier.fillMaxWidth(),
        )

        Spacer(Modifier.height(20.dp))

        Button(
            onClick = onSubmit,
            enabled = !isLoading && code.length == 6 && newPassword.length >= 8,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
        ) {
            if (isLoading) {
                CircularProgressIndicator(color = Color.White, modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
            } else {
                Text("Reset Password", fontWeight = FontWeight.Bold, fontSize = 16.sp)
            }
        }
    }
}

@Composable
private fun SuccessStep(onGoToLogin: () -> Unit) {
    Column(
        Modifier.padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(
            Icons.Default.CheckCircle,
            contentDescription = null,
            tint = Color(0xFF00E676),
            modifier = Modifier.size(64.dp),
        )
        Spacer(Modifier.height(16.dp))
        Text(
            "Password Reset!",
            fontSize = 22.sp,
            fontWeight = FontWeight.ExtraBold,
            color = Color(0xFFF4F4FB),
        )
        Text(
            "Your password has been updated. You can now log in with your new password.",
            fontSize = 14.sp,
            color = Color(0x99F4F4FB),
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 8.dp),
        )
        Spacer(Modifier.height(28.dp))
        Button(
            onClick = onGoToLogin,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
        ) {
            Text("Go to Login", fontWeight = FontWeight.Bold, fontSize = 16.sp)
        }
    }
}
