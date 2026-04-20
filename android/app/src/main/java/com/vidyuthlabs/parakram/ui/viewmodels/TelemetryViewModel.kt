package com.vidyuthlabs.parakram.ui.viewmodels

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import javax.inject.Inject

// ---- Data models ----

data class TelemetryReading(
    val ts: Long,
    val temperature: Float,
    val humidity: Float,
    val uptimeSeconds: Long,
    val freeHeap: Long,
    val rssi: Int,
)

enum class WsStatus { CONNECTING, CONNECTED, DISCONNECTED, ERROR }

// ---- ViewModel ----

@HiltViewModel
class TelemetryViewModel @Inject constructor(
    private val okHttpClient: OkHttpClient,
) : ViewModel() {

    private val _readings = MutableStateFlow<List<TelemetryReading>>(emptyList())
    val readings: StateFlow<List<TelemetryReading>> = _readings.asStateFlow()

    private val _latestReading = MutableStateFlow<TelemetryReading?>(null)
    val latestReading: StateFlow<TelemetryReading?> = _latestReading.asStateFlow()

    private val _connectionStatus = MutableStateFlow(WsStatus.DISCONNECTED)
    val connectionStatus: StateFlow<WsStatus> = _connectionStatus.asStateFlow()

    private var webSocket: WebSocket? = null
    private var manuallyDisconnected = false
    private var currentDeviceId: String = ""
    private var currentToken: String = ""

    private val readingsDeque = ArrayDeque<TelemetryReading>(30)

    fun connect(deviceId: String, token: String) {
        manuallyDisconnected = false
        currentDeviceId = deviceId
        currentToken = token
        openSocket(deviceId, token)
    }

    fun disconnect() {
        manuallyDisconnected = true
        webSocket?.close(1000, "User disconnected")
        webSocket = null
        _connectionStatus.value = WsStatus.DISCONNECTED
    }

    private fun openSocket(deviceId: String, token: String) {
        _connectionStatus.value = WsStatus.CONNECTING

        val url = "ws://api.parakram.com:8400/api/telemetry/ws/$deviceId?token=$token"
        val request = Request.Builder().url(url).build()

        webSocket = okHttpClient.newWebSocket(request, object : WebSocketListener() {

            override fun onOpen(webSocket: WebSocket, response: Response) {
                _connectionStatus.value = WsStatus.CONNECTED
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                parseAndAppend(text)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                _connectionStatus.value = WsStatus.DISCONNECTED
                if (!manuallyDisconnected) scheduleReconnect()
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                _connectionStatus.value = WsStatus.ERROR
                if (!manuallyDisconnected) scheduleReconnect()
            }
        })
    }

    private fun parseAndAppend(text: String) {
        try {
            val root = JSONObject(text)
            if (root.optString("type") != "telemetry") return

            val data = root.getJSONObject("data")
            val reading = TelemetryReading(
                ts = root.optLong("ts", System.currentTimeMillis()),
                temperature = data.optDouble("temperature", 0.0).toFloat(),
                humidity = data.optDouble("humidity", 0.0).toFloat(),
                uptimeSeconds = data.optLong("uptime_s", 0L),
                freeHeap = data.optLong("free_heap", 0L),
                rssi = data.optInt("rssi", 0),
            )

            if (readingsDeque.size >= 30) readingsDeque.removeFirst()
            readingsDeque.addLast(reading)

            _readings.value = readingsDeque.toList()
            _latestReading.value = reading
        } catch (_: Exception) {
            // Malformed frame — skip
        }
    }

    private fun scheduleReconnect() {
        viewModelScope.launch {
            delay(3_000)
            if (!manuallyDisconnected) {
                openSocket(currentDeviceId, currentToken)
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        manuallyDisconnected = true
        webSocket?.close(1000, "ViewModel cleared")
        webSocket = null
    }
}
