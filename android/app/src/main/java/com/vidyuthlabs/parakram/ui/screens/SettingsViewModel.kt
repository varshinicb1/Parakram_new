package com.vidyuthlabs.parakram.ui.screens

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

data class SettingsUiState(
    val llmKeySaveResult: String? = null,
    val isSavingKey: Boolean = false,
)

@HiltViewModel
class SettingsViewModel @Inject constructor(
    private val repository: ParakramRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    fun saveLlmKey(apiKey: String) {
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isSavingKey = true, llmKeySaveResult = null)
            val result = repository.setLlmKey(apiKey)
            _uiState.value = _uiState.value.copy(
                isSavingKey = false,
                llmKeySaveResult = result.getOrElse { it.message ?: "Error" },
            )
        }
    }

    fun clearSaveResult() {
        _uiState.value = _uiState.value.copy(llmKeySaveResult = null)
    }
}
