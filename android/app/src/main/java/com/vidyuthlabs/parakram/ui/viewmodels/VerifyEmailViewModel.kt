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

data class VerifyEmailUiState(
    val isLoading: Boolean = false,
    val error: String? = null,
    val isVerified: Boolean = false,
)

@HiltViewModel
class VerifyEmailViewModel @Inject constructor(
    private val api: ParakramApi,
) : ViewModel() {

    private val _uiState = MutableStateFlow(VerifyEmailUiState())
    val uiState: StateFlow<VerifyEmailUiState> = _uiState.asStateFlow()

    fun verify(username: String, code: String) {
        if (code.length != 6) {
            _uiState.value = _uiState.value.copy(error = "Enter the 6-digit code from your email")
            return
        }
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            try {
                val response = api.verifyEmail(mapOf("username" to username, "code" to code))
                if (response.isSuccessful) {
                    _uiState.value = _uiState.value.copy(isLoading = false, isVerified = true)
                } else {
                    val msg = when (response.code()) {
                        400 -> "Invalid or expired code. Check your email and try again."
                        else -> "Verification failed (${response.code()})"
                    }
                    _uiState.value = _uiState.value.copy(isLoading = false, error = msg)
                }
            } catch (e: Exception) {
                _uiState.value = _uiState.value.copy(
                    isLoading = false,
                    error = e.message ?: "Network error. Check your connection.",
                )
            }
        }
    }

    fun clearError() {
        _uiState.value = _uiState.value.copy(error = null)
    }
}
