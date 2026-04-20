package com.vidyuthlabs.parakram.ui.viewmodels

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

data class RegisterUiState(
    val isLoading: Boolean = false,
    val error: String? = null,
    val isSuccess: Boolean = false,
    val needsEmailVerification: Boolean = false,
    val pendingUsername: String = "",
)

@HiltViewModel
class RegisterViewModel @Inject constructor(
    private val repository: ParakramRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(RegisterUiState())
    val uiState: StateFlow<RegisterUiState> = _uiState.asStateFlow()

    fun register(username: String, email: String, password: String) {
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            val result = repository.register(username, email, password)
            result.fold(
                onSuccess = {
                    if (email.isNotBlank()) {
                        _uiState.value = _uiState.value.copy(
                            isLoading = false,
                            needsEmailVerification = true,
                            pendingUsername = username,
                        )
                    } else {
                        _uiState.value = _uiState.value.copy(isLoading = false, isSuccess = true)
                    }
                },
                onFailure = { e ->
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        error = e.message ?: "Registration failed. Please try again.",
                    )
                },
            )
        }
    }

    fun clearError() {
        _uiState.value = _uiState.value.copy(error = null)
    }
}
