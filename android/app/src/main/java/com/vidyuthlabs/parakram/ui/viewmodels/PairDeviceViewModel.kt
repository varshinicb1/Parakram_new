package com.vidyuthlabs.parakram.ui.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.ble.BleManager
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import com.vidyuthlabs.parakram.domain.model.Device
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

enum class PairingState { IDLE, PAIRING, SUCCESS, ERROR }

data class PairDeviceUiState(
    val pairingState: PairingState = PairingState.IDLE,
    val selectedDevice: BleManager.BleDevice? = null,
    val pairedDevice: Device? = null,
    val error: String? = null,
)

@HiltViewModel
class PairDeviceViewModel @Inject constructor(
    application: Application,
    private val bleManager: BleManager,
    private val repository: ParakramRepository,
) : AndroidViewModel(application) {

    val discoveredDevices: StateFlow<List<BleManager.BleDevice>> = bleManager.discoveredDevices
    val connectionState: StateFlow<BleManager.ConnectionState> = bleManager.connectionState

    private val _uiState = MutableStateFlow(PairDeviceUiState())
    val uiState: StateFlow<PairDeviceUiState> = _uiState.asStateFlow()

    fun startScan() {
        bleManager.startScan(getApplication())
    }

    fun selectDevice(device: BleManager.BleDevice) {
        _uiState.value = _uiState.value.copy(selectedDevice = device, error = null)
    }

    fun clearSelection() {
        _uiState.value = _uiState.value.copy(selectedDevice = null, error = null)
    }

    fun pairDevice(name: String, boardSku: String, token: String) {
        val device = _uiState.value.selectedDevice ?: return
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(pairingState = PairingState.PAIRING, error = null)
            try {
                val bearer = if (token.startsWith("Bearer ")) token else "Bearer $token"
                val body = mapOf(
                    "ble_address" to device.address,
                    "name" to name,
                    "board_sku" to boardSku,
                )
                val response = repository.pairDevice(bearer, body)
                response.fold(
                    onSuccess = { paired ->
                        _uiState.value = _uiState.value.copy(
                            pairingState = PairingState.SUCCESS,
                            pairedDevice = paired,
                        )
                    },
                    onFailure = { e ->
                        _uiState.value = _uiState.value.copy(
                            pairingState = PairingState.ERROR,
                            error = e.message ?: "Pairing failed. Please try again.",
                        )
                    },
                )
            } catch (e: Exception) {
                _uiState.value = _uiState.value.copy(
                    pairingState = PairingState.ERROR,
                    error = e.message ?: "An unexpected error occurred.",
                )
            }
        }
    }

    fun resetPairingState() {
        _uiState.value = _uiState.value.copy(
            pairingState = PairingState.IDLE,
            error = null,
        )
    }
}
