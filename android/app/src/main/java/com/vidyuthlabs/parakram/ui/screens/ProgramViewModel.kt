package com.vidyuthlabs.parakram.ui.screens

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import com.vidyuthlabs.parakram.domain.model.*
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import javax.inject.Inject

data class ProgramUiState(
    val description: String = "",
    val boardId: String = "VDYT-S3-R1",
    val deviceId: String = "",
    val isDeploying: Boolean = false,
    val irPreview: IRPreview? = null,
    val error: String? = null,
)

@HiltViewModel
class ProgramViewModel @Inject constructor(
    private val repository: ParakramRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(ProgramUiState())
    val uiState: StateFlow<ProgramUiState> = _uiState.asStateFlow()

    val deploymentProgress: StateFlow<DeploymentProgress> = repository.deploymentProgress

    fun updateDescription(desc: String) { _uiState.value = _uiState.value.copy(description = desc) }
    fun updateBoardId(id: String) { _uiState.value = _uiState.value.copy(boardId = id) }
    fun updateDeviceId(id: String) { _uiState.value = _uiState.value.copy(deviceId = id) }

    fun deploy() {
        val state = _uiState.value
        if (state.description.isBlank()) return

        _uiState.value = state.copy(isDeploying = true, error = null)

        viewModelScope.launch {
            val result = repository.deployFromIntent(
                description = state.description,
                boardId = state.boardId,
                deviceId = state.deviceId,
            )

            result.onSuccess {
                _uiState.value = _uiState.value.copy(isDeploying = false)
            }.onFailure { e ->
                _uiState.value = _uiState.value.copy(isDeploying = false, error = e.message)
            }
        }
    }
}
