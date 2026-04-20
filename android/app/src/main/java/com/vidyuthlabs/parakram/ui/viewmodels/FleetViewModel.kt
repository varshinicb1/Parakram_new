package com.vidyuthlabs.parakram.ui.viewmodels

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.FleetRepository
import com.vidyuthlabs.parakram.domain.model.FleetDevice
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

// ---- Fleet UI state ----

data class FleetUiState(
    val totalDevices: Int = 0,
    val onlineDevices: Int = 0,
    val totalProjects: Int = 0,
    val deployedProjects: Int = 0,
    val devices: List<FleetDevice> = emptyList(),
    val isLoading: Boolean = false,
    val error: String? = null,
)

// ---- ViewModel ----

@HiltViewModel
class FleetViewModel @Inject constructor(
    private val repository: FleetRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(FleetUiState(isLoading = true))
    val uiState: StateFlow<FleetUiState> = _uiState.asStateFlow()

    init {
        load()
    }

    fun refresh() {
        load()
    }

    private fun load() {
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)

            val overviewResult = repository.getFleetOverview()
            val devicesResult = repository.getFleetDevices()

            val overviewError = overviewResult.exceptionOrNull()?.message
            val devicesError = devicesResult.exceptionOrNull()?.message
            val combinedError = listOfNotNull(overviewError, devicesError).firstOrNull()

            val overview = overviewResult.getOrNull() ?: emptyMap()
            val devices = devicesResult.getOrNull() ?: emptyList()

            _uiState.value = FleetUiState(
                totalDevices = (overview["total_devices"] as? Double)?.toInt()
                    ?: (overview["total_devices"] as? Int) ?: devices.size,
                onlineDevices = (overview["online_devices"] as? Double)?.toInt()
                    ?: (overview["online_devices"] as? Int)
                    ?: devices.count { it.status == "online" },
                totalProjects = (overview["total_projects"] as? Double)?.toInt()
                    ?: (overview["total_projects"] as? Int) ?: 0,
                deployedProjects = (overview["deployed_projects"] as? Double)?.toInt()
                    ?: (overview["deployed_projects"] as? Int) ?: 0,
                devices = devices,
                isLoading = false,
                error = combinedError,
            )
        }
    }
}
