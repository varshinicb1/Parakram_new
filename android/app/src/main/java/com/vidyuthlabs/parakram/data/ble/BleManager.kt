package com.vidyuthlabs.parakram.data.ble

import android.bluetooth.*
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID
import javax.inject.Inject
import javax.inject.Singleton

/**
 * BLE Manager for communicating with Parakram devices.
 *
 * Handles scanning, connection, GATT service discovery,
 * and chunked payload transfer.
 */
@Singleton
class BleManager @Inject constructor() {

    companion object {
        private const val TAG = "BleManager"
        const val CHUNK_SIZE = 508

        // Parakram service UUIDs (matching firmware)
        val SVC_DEPLOY_UUID: UUID = UUID.fromString("1200f0bc-9a78-5634-12ef-cdab00000001")
        val CHR_PAYLOAD_UUID: UUID = UUID.fromString("1200f0bc-9a78-5634-12ef-cdab00000002")
        val CHR_STATUS_UUID: UUID = UUID.fromString("1200f0bc-9a78-5634-12ef-cdab00000003")
    }

    data class BleDevice(
        val name: String,
        val address: String,
        val rssi: Int,
    )

    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Connecting : ConnectionState()
        object Connected : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _discoveredDevices = MutableStateFlow<List<BleDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<BleDevice>> = _discoveredDevices.asStateFlow()

    private val _transferProgress = MutableStateFlow(0f)
    val transferProgress: StateFlow<Float> = _transferProgress.asStateFlow()

    private var bluetoothGatt: BluetoothGatt? = null
    private var payloadCharacteristic: BluetoothGattCharacteristic? = null

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "Connected to GATT server")
                    _connectionState.value = ConnectionState.Connected
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "Disconnected from GATT server")
                    _connectionState.value = ConnectionState.Disconnected
                    bluetoothGatt = null
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SVC_DEPLOY_UUID)
                payloadCharacteristic = service?.getCharacteristic(CHR_PAYLOAD_UUID)
                if (payloadCharacteristic != null) {
                    Log.i(TAG, "Deploy service discovered")
                } else {
                    Log.w(TAG, "Deploy characteristic not found")
                }

                // Subscribe to status notifications
                val statusChar = service?.getCharacteristic(CHR_STATUS_UUID)
                if (statusChar != null) {
                    gatt.setCharacteristicNotification(statusChar, true)
                    val descriptor = statusChar.getDescriptor(
                        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
                    )
                    descriptor?.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    gatt.writeDescriptor(descriptor)
                }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid == CHR_STATUS_UUID) {
                val status = String(value)
                Log.i(TAG, "Device status: $status")
            }
        }
    }

    fun connect(context: Context, address: String) {
        _connectionState.value = ConnectionState.Connecting
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val device = bluetoothManager.adapter.getRemoteDevice(address)
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _connectionState.value = ConnectionState.Disconnected
    }

    /**
     * Transfer a bytecode payload via BLE GATT chunking.
     */
    suspend fun transferPayload(payload: ByteArray): Boolean {
        val gatt = bluetoothGatt ?: return false
        val char = payloadCharacteristic ?: return false

        val totalLen = payload.size
        _transferProgress.value = 0f

        // First chunk: 4-byte length header + data
        val headerBytes = ByteArray(4)
        headerBytes[0] = (totalLen and 0xFF).toByte()
        headerBytes[1] = ((totalLen shr 8) and 0xFF).toByte()
        headerBytes[2] = ((totalLen shr 16) and 0xFF).toByte()
        headerBytes[3] = ((totalLen shr 24) and 0xFF).toByte()

        val firstChunkDataSize = minOf(CHUNK_SIZE - 4, totalLen)
        val firstChunk = headerBytes + payload.copyOfRange(0, firstChunkDataSize)

        char.value = firstChunk
        char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        gatt.writeCharacteristic(char)

        var offset = firstChunkDataSize
        _transferProgress.value = offset.toFloat() / totalLen

        // Subsequent chunks
        while (offset < totalLen) {
            val chunkSize = minOf(CHUNK_SIZE, totalLen - offset)
            val chunk = payload.copyOfRange(offset, offset + chunkSize)

            // Simple delay for flow control (BLE spec)
            kotlinx.coroutines.delay(20)

            char.value = chunk
            gatt.writeCharacteristic(char)

            offset += chunkSize
            _transferProgress.value = offset.toFloat() / totalLen
        }

        _transferProgress.value = 1f
        Log.i(TAG, "Payload transfer complete: $totalLen bytes")
        return true
    }

    fun startScan(context: Context) {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val scanner = bluetoothManager.adapter.bluetoothLeScanner ?: return

        _discoveredDevices.value = emptyList()

        val callback = object : android.bluetooth.le.ScanCallback() {
            override fun onScanResult(callbackType: Int, result: android.bluetooth.le.ScanResult) {
                val name = result.device.name ?: return
                if (!name.startsWith("Parakram")) return

                val bleDevice = BleDevice(name, result.device.address, result.rssi)
                val current = _discoveredDevices.value.toMutableList()
                if (current.none { it.address == bleDevice.address }) {
                    current.add(bleDevice)
                    _discoveredDevices.value = current
                }
            }
        }
        scanner.startScan(callback)

        // Stop scan after 10 seconds
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
            try { scanner.stopScan(callback) } catch (_: Exception) {}
        }, 10_000)
    }
}
