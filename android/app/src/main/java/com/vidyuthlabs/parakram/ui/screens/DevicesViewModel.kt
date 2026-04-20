package com.vidyuthlabs.parakram.ui.screens

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.vidyuthlabs.parakram.data.ble.BleManager
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.StateFlow
import javax.inject.Inject

@HiltViewModel
class DevicesViewModel @Inject constructor(
    application: Application,
    private val bleManager: BleManager,
) : AndroidViewModel(application) {

    val discoveredDevices: StateFlow<List<BleManager.BleDevice>> = bleManager.discoveredDevices
    val connectionState: StateFlow<BleManager.ConnectionState> = bleManager.connectionState

    fun startScan() {
        bleManager.startScan(getApplication())
    }

    fun connect(address: String) {
        bleManager.connect(getApplication(), address)
    }

    fun disconnect() {
        bleManager.disconnect()
    }
}
