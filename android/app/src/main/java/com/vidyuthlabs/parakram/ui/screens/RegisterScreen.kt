package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Email
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.SolidColor
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
import com.vidyuthlabs.parakram.ui.theme.VidyuthBackground
import com.vidyuthlabs.parakram.ui.theme.VidyuthOnPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthSurface
import com.vidyuthlabs.parakram.ui.viewmodels.RegisterViewModel

private val RegisterGradientBackground = listOf(
    Color(0xFF0F0F23),
    Color(0xFF1A1040),
    Color(0xFF0F0F23),
)

// Validation helpers
private fun isValidUsername(value: String): Boolean =
    value.length in 3..32 && value.all { it.isLetterOrDigit() || it == '_' }

private fun isValidEmail(value: String): Boolean =
    value.isEmpty() || android.util.Patterns.EMAIL_ADDRESS.matcher(value).matches()

private fun isValidPassword(value: String): Boolean = value.length >= 8

private fun passwordsMatch(password: String, confirm: String): Boolean =
    confirm.isEmpty() || password == confirm

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RegisterScreen(
    onRegisterSuccess: () -> Unit,
    onNeedsVerification: (username: String) -> Unit = {},
    onNavigateToLogin: () -> Unit = {},
    viewModel: RegisterViewModel = hiltViewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    var username by remember { mutableStateOf("") }
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var confirmPassword by remember { mutableStateOf("") }

    var usernameTouched by remember { mutableStateOf(false) }
    var emailTouched by remember { mutableStateOf(false) }
    var passwordTouched by remember { mutableStateOf(false) }
    var confirmPasswordTouched by remember { mutableStateOf(false) }

    var passwordVisible by remember { mutableStateOf(false) }
    var confirmPasswordVisible by remember { mutableStateOf(false) }

    val usernameError by remember(username, usernameTouched) {
        derivedStateOf {
            if (!usernameTouched) null
            else when {
                username.isBlank() -> "Username is required"
                username.length < 3 -> "Must be at least 3 characters"
                username.length > 32 -> "Must be 32 characters or fewer"
                !username.all { it.isLetterOrDigit() || it == '_' } -> "Only letters, digits, and underscores"
                else -> null
            }
        }
    }

    val emailError by remember(email, emailTouched) {
        derivedStateOf {
            if (!emailTouched || email.isEmpty()) null
            else if (!android.util.Patterns.EMAIL_ADDRESS.matcher(email).matches()) "Enter a valid email address"
            else null
        }
    }

    val passwordError by remember(password, passwordTouched) {
        derivedStateOf {
            if (!passwordTouched) null
            else when {
                password.isBlank() -> "Password is required"
                password.length < 8 -> "Must be at least 8 characters"
                else -> null
            }
        }
    }

    val confirmPasswordError by remember(password, confirmPassword, confirmPasswordTouched) {
        derivedStateOf {
            if (!confirmPasswordTouched) null
            else when {
                confirmPassword.isBlank() -> "Please confirm your password"
                confirmPassword != password -> "Passwords do not match"
                else -> null
            }
        }
    }

    val isFormValid by remember(username, email, password, confirmPassword) {
        derivedStateOf {
            isValidUsername(username) &&
                isValidEmail(email) &&
                isValidPassword(password) &&
                confirmPassword == password &&
                confirmPassword.isNotBlank()
        }
    }

    LaunchedEffect(uiState.isSuccess) {
        if (uiState.isSuccess) onRegisterSuccess()
    }
    LaunchedEffect(uiState.needsEmailVerification) {
        if (uiState.needsEmailVerification) onNeedsVerification(uiState.pendingUsername)
    }

    LaunchedEffect(uiState.error) {
        val err = uiState.error
        if (err != null) {
            snackbarHostState.showSnackbar(err)
            viewModel.clearError()
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
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
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Brush.verticalGradient(RegisterGradientBackground)),
        ) {
            // Ambient glow circles
            RegisterGlowCircle(
                color = VidyuthPrimary.copy(alpha = 0.18f),
                size = 280.dp,
                modifier = Modifier.offset(x = (-60).dp, y = (-40).dp),
            )
            RegisterGlowCircle(
                color = VidyuthSecondary.copy(alpha = 0.12f),
                size = 220.dp,
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .offset(x = 60.dp, y = 120.dp),
            )
            RegisterGlowCircle(
                color = Color(0xFFFF6584).copy(alpha = 0.10f),
                size = 200.dp,
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .offset(x = (-40).dp, y = 60.dp),
            )

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Spacer(Modifier.height(56.dp))

                // Logo + title
                Box(
                    modifier = Modifier
                        .size(72.dp)
                        .clip(RoundedCornerShape(20.dp))
                        .background(
                            Brush.verticalGradient(listOf(VidyuthPrimary, Color(0xFF8B5CF6))),
                        ),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Default.Bolt,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(40.dp),
                    )
                }

                Spacer(Modifier.height(20.dp))

                Text(
                    text = "Parakram",
                    fontSize = 42.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                )
                Text(
                    text = "by Vidyuthlabs",
                    fontSize = 14.sp,
                    color = Color.White.copy(alpha = 0.5f),
                    letterSpacing = 2.sp,
                )

                Spacer(Modifier.height(40.dp))

                // Success state
                AnimatedVisibility(visible = uiState.isSuccess, enter = fadeIn(), exit = fadeOut()) {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = VidyuthSuccess.copy(alpha = 0.15f),
                        ),
                        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
                    ) {
                        Column(
                            modifier = Modifier.padding(32.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            Icon(
                                Icons.Default.CheckCircle,
                                contentDescription = null,
                                tint = VidyuthSuccess,
                                modifier = Modifier.size(48.dp),
                            )
                            Text(
                                "Account created!",
                                fontSize = 20.sp,
                                fontWeight = FontWeight.Bold,
                                color = Color.White,
                                textAlign = TextAlign.Center,
                            )
                            Text(
                                "Signing you in...",
                                fontSize = 14.sp,
                                color = Color.White.copy(alpha = 0.65f),
                                textAlign = TextAlign.Center,
                            )
                        }
                    }
                }

                // Registration card
                AnimatedVisibility(visible = !uiState.isSuccess, enter = fadeIn(), exit = fadeOut()) {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = VidyuthSurface.copy(alpha = 0.85f),
                        ),
                        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
                    ) {
                        Column(
                            modifier = Modifier.padding(28.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp),
                        ) {
                            Text(
                                "Create account",
                                fontSize = 22.sp,
                                fontWeight = FontWeight.Bold,
                                color = Color.White,
                            )

                            Spacer(Modifier.height(8.dp))

                            // Username field
                            OutlinedTextField(
                                value = username,
                                onValueChange = {
                                    username = it
                                    usernameTouched = true
                                },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("Username") },
                                leadingIcon = {
                                    Icon(Icons.Default.AccountCircle, contentDescription = null)
                                },
                                isError = usernameError != null,
                                shape = RoundedCornerShape(14.dp),
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(
                                    keyboardType = KeyboardType.Ascii,
                                    imeAction = ImeAction.Next,
                                ),
                                colors = registerTextFieldColors(),
                            )
                            if (usernameError != null) {
                                Text(
                                    text = usernameError!!,
                                    color = Color(0xFFFF5252),
                                    fontSize = 12.sp,
                                    modifier = Modifier.padding(start = 16.dp, top = 2.dp),
                                )
                            }

                            Spacer(Modifier.height(4.dp))

                            // Email field (optional)
                            OutlinedTextField(
                                value = email,
                                onValueChange = {
                                    email = it
                                    emailTouched = true
                                },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("Email (optional)") },
                                leadingIcon = {
                                    Icon(Icons.Default.Email, contentDescription = null)
                                },
                                isError = emailError != null,
                                shape = RoundedCornerShape(14.dp),
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(
                                    keyboardType = KeyboardType.Email,
                                    imeAction = ImeAction.Next,
                                ),
                                colors = registerTextFieldColors(),
                            )
                            if (emailError != null) {
                                Text(
                                    text = emailError!!,
                                    color = Color(0xFFFF5252),
                                    fontSize = 12.sp,
                                    modifier = Modifier.padding(start = 16.dp, top = 2.dp),
                                )
                            }

                            Spacer(Modifier.height(4.dp))

                            // Password field
                            OutlinedTextField(
                                value = password,
                                onValueChange = {
                                    password = it
                                    passwordTouched = true
                                },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("Password") },
                                leadingIcon = {
                                    Icon(Icons.Default.Lock, contentDescription = null)
                                },
                                trailingIcon = {
                                    IconButton(onClick = { passwordVisible = !passwordVisible }) {
                                        Icon(
                                            if (passwordVisible) Icons.Default.VisibilityOff
                                            else Icons.Default.Visibility,
                                            contentDescription = if (passwordVisible) "Hide password" else "Show password",
                                        )
                                    }
                                },
                                visualTransformation = if (passwordVisible) VisualTransformation.None
                                else PasswordVisualTransformation(),
                                isError = passwordError != null,
                                shape = RoundedCornerShape(14.dp),
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(
                                    keyboardType = KeyboardType.Password,
                                    imeAction = ImeAction.Next,
                                ),
                                colors = registerTextFieldColors(),
                            )
                            if (passwordError != null) {
                                Text(
                                    text = passwordError!!,
                                    color = Color(0xFFFF5252),
                                    fontSize = 12.sp,
                                    modifier = Modifier.padding(start = 16.dp, top = 2.dp),
                                )
                            }

                            Spacer(Modifier.height(4.dp))

                            // Confirm password field
                            OutlinedTextField(
                                value = confirmPassword,
                                onValueChange = {
                                    confirmPassword = it
                                    confirmPasswordTouched = true
                                },
                                modifier = Modifier.fillMaxWidth(),
                                label = { Text("Confirm Password") },
                                leadingIcon = {
                                    Icon(Icons.Default.Lock, contentDescription = null)
                                },
                                trailingIcon = {
                                    IconButton(onClick = { confirmPasswordVisible = !confirmPasswordVisible }) {
                                        Icon(
                                            if (confirmPasswordVisible) Icons.Default.VisibilityOff
                                            else Icons.Default.Visibility,
                                            contentDescription = if (confirmPasswordVisible) "Hide password" else "Show password",
                                        )
                                    }
                                },
                                visualTransformation = if (confirmPasswordVisible) VisualTransformation.None
                                else PasswordVisualTransformation(),
                                isError = confirmPasswordError != null,
                                shape = RoundedCornerShape(14.dp),
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(
                                    keyboardType = KeyboardType.Password,
                                    imeAction = ImeAction.Done,
                                ),
                                colors = registerTextFieldColors(),
                            )
                            if (confirmPasswordError != null) {
                                Text(
                                    text = confirmPasswordError!!,
                                    color = Color(0xFFFF5252),
                                    fontSize = 12.sp,
                                    modifier = Modifier.padding(start = 16.dp, top = 2.dp),
                                )
                            }

                            Spacer(Modifier.height(16.dp))

                            // Register button with gradient
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(56.dp)
                                    .clip(RoundedCornerShape(28.dp))
                                    .background(
                                        brush = if (uiState.isLoading || !isFormValid)
                                            SolidColor(Color.Gray.copy(alpha = 0.4f))
                                        else Brush.horizontalGradient(
                                            listOf(VidyuthPrimary, Color(0xFF8B5CF6)),
                                        ),
                                    ),
                                contentAlignment = Alignment.Center,
                            ) {
                                Button(
                                    onClick = {
                                        usernameTouched = true
                                        emailTouched = true
                                        passwordTouched = true
                                        confirmPasswordTouched = true
                                        if (isFormValid) {
                                            viewModel.register(username, email, password)
                                        }
                                    },
                                    modifier = Modifier.fillMaxSize(),
                                    shape = RoundedCornerShape(28.dp),
                                    enabled = !uiState.isLoading && isFormValid,
                                    colors = ButtonDefaults.buttonColors(
                                        containerColor = Color.Transparent,
                                        disabledContainerColor = Color.Transparent,
                                    ),
                                    elevation = ButtonDefaults.buttonElevation(0.dp, 0.dp),
                                ) {
                                    AnimatedVisibility(
                                        visible = uiState.isLoading,
                                        enter = fadeIn(),
                                        exit = fadeOut(),
                                    ) {
                                        CircularProgressIndicator(
                                            modifier = Modifier.size(22.dp),
                                            color = VidyuthOnPrimary,
                                            strokeWidth = 2.dp,
                                        )
                                    }
                                    AnimatedVisibility(
                                        visible = !uiState.isLoading,
                                        enter = fadeIn(),
                                        exit = fadeOut(),
                                    ) {
                                        Text(
                                            "Register",
                                            fontSize = 16.sp,
                                            fontWeight = FontWeight.Bold,
                                            color = VidyuthOnPrimary,
                                        )
                                    }
                                }
                            }
                        }
                    }
                }

                Spacer(Modifier.height(28.dp))

                TextButton(onClick = onNavigateToLogin) {
                    Text(
                        "Already have an account? Sign in",
                        color = Color.White.copy(alpha = 0.65f),
                        fontSize = 14.sp,
                        textAlign = TextAlign.Center,
                    )
                }

                Spacer(Modifier.height(32.dp))
            }
        }
    }
}

@Composable
private fun RegisterGlowCircle(
    color: Color,
    size: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .size(size)
            .blur(60.dp)
            .clip(CircleShape)
            .background(color),
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun registerTextFieldColors() = OutlinedTextFieldDefaults.colors(
    focusedBorderColor = VidyuthPrimary,
    unfocusedBorderColor = Color.White.copy(alpha = 0.2f),
    errorBorderColor = Color(0xFFFF5252),
    focusedLabelColor = VidyuthPrimary,
    unfocusedLabelColor = Color.White.copy(alpha = 0.5f),
    errorLabelColor = Color(0xFFFF5252),
    cursorColor = VidyuthPrimary,
    focusedTextColor = Color.White,
    unfocusedTextColor = Color.White.copy(alpha = 0.85f),
    errorTextColor = Color.White,
    focusedLeadingIconColor = VidyuthPrimary,
    unfocusedLeadingIconColor = Color.White.copy(alpha = 0.4f),
    errorLeadingIconColor = Color(0xFFFF5252),
    focusedTrailingIconColor = VidyuthPrimary,
    unfocusedTrailingIconColor = Color.White.copy(alpha = 0.4f),
    errorTrailingIconColor = Color(0xFFFF5252),
)
