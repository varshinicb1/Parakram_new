package com.vidyuthlabs.parakram.data.repository

import com.google.firebase.messaging.FirebaseMessaging
import com.vidyuthlabs.parakram.data.api.ParakramApi
import com.vidyuthlabs.parakram.data.ble.BleManager
import com.vidyuthlabs.parakram.domain.model.BillingPlan
import com.vidyuthlabs.parakram.domain.model.CompileResult
import com.vidyuthlabs.parakram.domain.model.DeployRequest
import com.vidyuthlabs.parakram.domain.model.DeploymentProgress
import com.vidyuthlabs.parakram.domain.model.DeploymentState
import com.vidyuthlabs.parakram.domain.model.Device
import com.vidyuthlabs.parakram.domain.model.HealthResponse
import com.vidyuthlabs.parakram.domain.model.IntentRequest
import com.vidyuthlabs.parakram.domain.model.IntentResponse
import com.vidyuthlabs.parakram.domain.model.LoginRequest
import com.vidyuthlabs.parakram.domain.model.LoginResponse
import com.vidyuthlabs.parakram.domain.model.RegisterRequest
import com.vidyuthlabs.parakram.domain.model.MarketplaceResponse
import com.vidyuthlabs.parakram.domain.model.SubscriptionView
import com.vidyuthlabs.parakram.domain.model.UsageView
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.coroutines.resume

@Singleton
class ParakramRepository @Inject constructor(
    private val api: ParakramApi,
    val bleManager: BleManager,
) {
    private var authToken: String? = null

    private val _deploymentProgress = MutableStateFlow(DeploymentProgress())
    val deploymentProgress: StateFlow<DeploymentProgress> = _deploymentProgress.asStateFlow()

    private fun bearer() = "Bearer ${authToken ?: ""}"

    // Auth
    suspend fun login(username: String, password: String): Result<LoginResponse> = try {
        val response = api.login(LoginRequest(username, password))
        if (response.isSuccessful) {
            val body = response.body()!!
            authToken = body.token
            // Register device push token now that we have an auth token.
            registerPushToken()
            Result.success(body)
        } else {
            Result.failure(Exception("Login failed: ${response.code()}"))
        }
    } catch (e: Exception) {
        Result.failure(e)
    }

    suspend fun register(username: String, email: String, password: String): Result<LoginResponse> = try {
        val response = api.register(RegisterRequest(username, email, password))
        if (response.isSuccessful) {
            val body = response.body()!!
            authToken = body.token
            // Register device push token now that we have an auth token.
            registerPushToken()
            Result.success(body)
        } else {
            Result.failure(Exception("Registration failed: ${response.code()}"))
        }
    } catch (e: Exception) {
        Result.failure(e)
    }

    fun isLoggedIn() = authToken != null

    /** Expose bearer token for other repositories in the same Hilt scope. */
    fun currentBearer() = "Bearer ${authToken ?: ""}"

    // Pairing
    suspend fun pairDevice(bearer: String, body: Map<String, String>): Result<Device> = try {
        val response = api.pairDevice(bearer, body)
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Pairing failed: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    // Health
    suspend fun healthCheck(): Result<HealthResponse> = try {
        val response = api.healthCheck()
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Health check failed"))
    } catch (e: Exception) { Result.failure(e) }

    // Device
    suspend fun getDevice(deviceId: String): Result<Device> = try {
        val response = api.getDevice(bearer(), deviceId)
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Failed to fetch device: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    // Billing
    suspend fun getMySubscription(): Result<SubscriptionView> = try {
        val response = api.getMySubscription(bearer())
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Failed to fetch subscription: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun getMyUsage(): Result<UsageView> = try {
        val response = api.getMyUsage(bearer())
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Failed to fetch usage: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun listPlans(): Result<List<BillingPlan>> = try {
        val response = api.listPlans()
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Failed to fetch plans: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun createCheckout(tier: String): Result<String> = try {
        val body = mapOf(
            "tier" to tier,
            "success_url" to "https://vidyuthlabs.co.in/billing/success",
            "cancel_url" to "https://vidyuthlabs.co.in/billing",
        )
        val response = api.createCheckout(bearer(), body)
        if (response.isSuccessful) Result.success(response.body()!!["url"]!!)
        else Result.failure(Exception("Checkout failed: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun createPortal(): Result<String> = try {
        val body = mapOf("return_url" to "https://vidyuthlabs.co.in/billing")
        val response = api.createPortal(bearer(), body)
        if (response.isSuccessful) Result.success(response.body()!!["url"]!!)
        else Result.failure(Exception("Portal failed: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    // Marketplace
    suspend fun listMarketplaceDrivers(
        search: String? = null,
        type: String? = null,
        bus: String? = null,
        sort: String = "newest",
        page: Int = 1,
        limit: Int = 20,
    ): Result<MarketplaceResponse> = try {
        val response = api.listMarketplaceDrivers(search, type, bus, sort, page, limit)
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Failed to fetch marketplace: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun setLlmKey(apiKey: String): Result<String> = try {
        val response = api.setLlmKey(bearer(), mapOf("api_key" to apiKey))
        if (response.isSuccessful) Result.success(response.body()!!["message"] ?: "Saved")
        else Result.failure(Exception("Failed to save key: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    suspend fun installDriver(driverId: String): Result<Unit> = try {
        val response = api.installDriver(bearer(), driverId)
        if (response.isSuccessful) Result.success(Unit)
        else Result.failure(Exception("Failed to install driver: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    /**
     * Retrieve the current FCM registration token and POST it to the backend
     * so the server can send push notifications to this device.
     *
     * This is a no-op if:
     *  - The user is not logged in (no auth token).
     *  - Firebase is not configured in the project (google-services.json absent).
     *  - Any network error occurs.
     *
     * Always safe to call on app start-up and after login.
     */
    suspend fun registerPushToken() {
        val bearer = authToken ?: return
        try {
            val fcmToken: String = suspendCancellableCoroutine { cont ->
                FirebaseMessaging.getInstance().token
                    .addOnSuccessListener { token -> cont.resume(token) }
                    .addOnFailureListener { e -> cont.cancel(e) }
            }
            val body = mapOf("platform" to "android", "token" to fcmToken)
            api.registerPushToken("Bearer $bearer", body)
        } catch (e: Exception) {
            // Firebase not configured or network error — degrade silently.
        }
    }

    // LLM Intent
    suspend fun processIntent(
        description: String, boardId: String, deviceId: String?
    ): Result<IntentResponse> = try {
        val response = api.processIntent(bearer(), IntentRequest(description, boardId, deviceId))
        if (response.isSuccessful) Result.success(response.body()!!)
        else Result.failure(Exception("Intent processing failed: ${response.code()}"))
    } catch (e: Exception) { Result.failure(e) }

    // Full deployment pipeline: intent → compile → deploy
    @Suppress("UNCHECKED_CAST")
    suspend fun deployFromIntent(
        description: String, boardId: String, deviceId: String, transferMethod: String = "wifi"
    ): Result<Unit> {
        return try {
        // Step 1: Process intent
        _deploymentProgress.value = DeploymentProgress(DeploymentState.COMPILING, 0.1f, "Understanding your request...")
        val intentResult = processIntent(description, boardId, deviceId)
        if (intentResult.isFailure) throw intentResult.exceptionOrNull()!!

        val intent = intentResult.getOrThrow()
        if (!intent.feasible) {
            _deploymentProgress.value = DeploymentProgress(DeploymentState.ERROR, 0f, error = intent.reason ?: "Not feasible")
            return Result.failure(Exception(intent.reason ?: "Request not feasible"))
        }

        val ir = intent.ir ?: throw Exception("No IR generated")

        // Step 2: Compile
        _deploymentProgress.value = DeploymentProgress(DeploymentState.COMPILING, 0.4f, "Compiling bytecode...")
        val compileReq = mapOf("ir" to ir, "device_id" to deviceId)
        val compileResponse = api.compileIR(bearer(), compileReq)
        if (!compileResponse.isSuccessful) throw Exception("Compilation failed")
        val compileResult = compileResponse.body()!!

        _deploymentProgress.value = DeploymentProgress(DeploymentState.COMPILED, 0.6f,
            "${compileResult.numInstructions} instructions, ${compileResult.bytecodeSize} bytes")

        // Step 3: Deploy
        _deploymentProgress.value = DeploymentProgress(DeploymentState.TRANSFERRING, 0.7f, "Deploying to device...")

        if (transferMethod == "ble") {
            // BLE transfer
            val payload = android.util.Base64.decode(compileResult.bytecodeB64, android.util.Base64.DEFAULT)
            val success = bleManager.transferPayload(payload)
            if (!success) throw Exception("BLE transfer failed")
        } else {
            // WiFi transfer via backend
            val deployReq = DeployRequest(compileResult.bytecodeB64, "", transferMethod)
            val deployResponse = api.deploy(bearer(), deviceId, deployReq)
            if (!deployResponse.isSuccessful) throw Exception("Deploy failed")
        }

        _deploymentProgress.value = DeploymentProgress(DeploymentState.RUNNING, 1f, "Program running on device!")
        Result.success(Unit)
    } catch (e: Exception) {
        _deploymentProgress.value = DeploymentProgress(DeploymentState.ERROR, 0f, error = e.message)
        Result.failure(e)
    }
    }
}
