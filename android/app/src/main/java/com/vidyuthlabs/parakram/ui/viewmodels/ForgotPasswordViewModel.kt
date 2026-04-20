package com.vidyuthlabs.parakram.ui.viewmodels

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.api.ParakramApi
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

sealed class ForgotPasswordStep {
    object EnterUsername : ForgotPasswordStep()
    data class EnterCode(val username: String) : ForgotPasswordStep()
    object Success : ForgotPasswordStep()
}

data class ForgotPasswordUiState(
    val step: ForgotPasswordStep = ForgotPasswordStep.EnterUsername,
    val username: String = "",
    val code: String = "",
    val newPassword: String = "",
    val confirmPassword: String = "",
    val isLoading: Boolean = false,
    val error: String? = null,
)

@HiltViewModel
class ForgotPasswordViewModel @Inject constructor(
    private val api: ParakramApi,
) : ViewModel() {

    private val _uiState = MutableStateFlow(ForgotPasswordUiState())
    val uiState: StateFlow<ForgotPasswordUiState> = _uiState.asStateFlow()

    fun onUsernameChange(value: String) {
        _uiState.value = _uiState.value.copy(username = value, error = null)
    }

    fun onCodeChange(value: String) {
        _uiState.value = _uiState.value.copy(code = value, error = null)
    }

    fun onNewPasswordChange(value: String) {
        _uiState.value = _uiState.value.copy(newPassword = value, error = null)
    }

    fun onConfirmPasswordChange(value: String) {
        _uiState.value = _uiState.value.copy(confirmPassword = value, error = null)
    }

    fun requestReset() {
        val username = _uiState.value.username.trim()
        if (username.length < 3) {
            _uiState.value = _uiState.value.copy(error = "Enter your username")
            return
        }
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            try {
                val resp = api.forgotPassword(mapOf("username" to username))
                if (resp.isSuccessful) {
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        step = ForgotPasswordStep.EnterCode(username),
                    )
                } else {
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        error = "Request failed (${resp.code()})",
                    )
                }
            } catch (e: Exception) {
                _uiState.value = _uiState.value.copy(isLoading = false, error = e.message)
            }
        }
    }

    fun submitReset() {
        val state = _uiState.value
        val username = (state.step as? ForgotPasswordStep.EnterCode)?.username ?: return
        val code = state.code.trim()
        val newPass = state.newPassword
        val confirm = state.confirmPassword

        if (code.length != 6) {
            _uiState.value = state.copy(error = "Enter the 6-digit code")
            return
        }
        if (newPass.length < 8) {
            _uiState.value = state.copy(error = "Password must be at least 8 characters")
            return
        }
        if (newPass != confirm) {
            _uiState.value = state.copy(error = "Passwords do not match")
            return
        }
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            try {
                val resp = api.resetPassword(
                    mapOf(
                        "username" to username,
                        "code" to code,
                        "new_password" to newPass,
                    )
                )
                if (resp.isSuccessful) {
                    _uiState.value = _uiState.value.copy(isLoading = false, step = ForgotPasswordStep.Success)
                } else {
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        error = "Invalid or expired code",
                    )
                }
            } catch (e: Exception) {
                _uiState.value = _uiState.value.copy(isLoading = false, error = e.message)
            }
        }
    }
}
