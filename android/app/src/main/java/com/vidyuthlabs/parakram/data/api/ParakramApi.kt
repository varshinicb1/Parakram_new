package com.vidyuthlabs.parakram.data.api

import com.vidyuthlabs.parakram.domain.model.*
import retrofit2.Response
import retrofit2.http.*

interface ParakramApi {

    // Auth
    @POST("api/auth/login")
    suspend fun login(@Body request: LoginRequest): Response<LoginResponse>

    @POST("api/auth/register")
    suspend fun register(@Body request: RegisterRequest): Response<LoginResponse>

    @POST("api/auth/refresh")
    suspend fun refreshToken(@Header("Authorization") bearer: String): Response<LoginResponse>

    @POST("api/auth/verify-email")
    suspend fun verifyEmail(@Body request: Map<String, String>): Response<Map<String, String>>

    @POST("api/auth/forgot-password")
    suspend fun forgotPassword(@Body request: Map<String, String>): Response<Map<String, String>>

    @POST("api/auth/reset-password")
    suspend fun resetPassword(@Body request: Map<String, String>): Response<Map<String, String>>

    @PUT("api/auth/me/llm-key")
    suspend fun setLlmKey(
        @Header("Authorization") bearer: String,
        @Body body: Map<String, String>,
    ): Response<Map<String, String>>

    // Devices
    @GET("api/devices")
    suspend fun listDevices(@Header("Authorization") bearer: String): Response<Map<String, Any>>

    @GET("api/devices/{deviceId}")
    suspend fun getDevice(
        @Header("Authorization") bearer: String,
        @Path("deviceId") deviceId: String,
    ): Response<Device>

    @POST("api/devices/pair")
    suspend fun pairDevice(
        @Header("Authorization") bearer: String,
        @Body request: Map<String, String>,
    ): Response<Device>

    // Projects
    @GET("api/projects")
    suspend fun listProjects(@Header("Authorization") bearer: String): Response<Map<String, Any>>

    @POST("api/projects")
    suspend fun createProject(
        @Header("Authorization") bearer: String,
        @Body request: Map<String, Any>,
    ): Response<Project>

    @GET("api/projects/{projectId}")
    suspend fun getProject(
        @Header("Authorization") bearer: String,
        @Path("projectId") projectId: String,
    ): Response<Project>

    // LLM
    @POST("api/llm/intent")
    suspend fun processIntent(
        @Header("Authorization") bearer: String,
        @Body request: IntentRequest,
    ): Response<IntentResponse>

    // IR / Compile / Deploy
    @POST("api/ir/validate")
    suspend fun validateIR(
        @Header("Authorization") bearer: String,
        @Body ir: Map<String, Any>,
    ): Response<ValidationResult>

    @POST("api/ir/compile")
    suspend fun compileIR(
        @Header("Authorization") bearer: String,
        @Body request: Map<String, Any>,
    ): Response<CompileResult>

    @POST("api/ir/deploy/{deviceId}")
    suspend fun deploy(
        @Header("Authorization") bearer: String,
        @Path("deviceId") deviceId: String,
        @Body request: DeployRequest,
    ): Response<Map<String, Any>>

    // System
    @GET("api/system/health")
    suspend fun healthCheck(): Response<HealthResponse>

    @GET("api/drivers")
    suspend fun listDrivers(): Response<Map<String, Any>>

    // Billing
    @GET("api/billing/me")
    suspend fun getMySubscription(
        @Header("Authorization") bearer: String,
    ): Response<SubscriptionView>

    @GET("api/billing/usage")
    suspend fun getMyUsage(
        @Header("Authorization") bearer: String,
    ): Response<UsageView>

    @GET("api/billing/plans")
    suspend fun listPlans(): Response<List<BillingPlan>>

    @POST("api/billing/checkout")
    suspend fun createCheckout(
        @Header("Authorization") bearer: String,
        @Body body: Map<String, String>,
    ): Response<Map<String, String>>

    @POST("api/billing/portal")
    suspend fun createPortal(
        @Header("Authorization") bearer: String,
        @Body body: Map<String, String>,
    ): Response<Map<String, String>>

    // Marketplace
    @GET("api/marketplace")
    suspend fun listMarketplaceDrivers(
        @Query("search") search: String? = null,
        @Query("type") type: String? = null,
        @Query("bus") bus: String? = null,
        @Query("sort") sort: String = "newest",
        @Query("page") page: Int = 1,
        @Query("limit") limit: Int = 20,
    ): Response<MarketplaceResponse>

    @GET("api/marketplace/{id}")
    suspend fun getDriver(
        @Path("id") id: String,
    ): Response<CommunityDriverDetail>

    @POST("api/marketplace/{id}/install")
    suspend fun installDriver(
        @Header("Authorization") bearer: String,
        @Path("id") id: String,
    ): Response<Map<String, Any>>

    @POST("api/marketplace/{id}/rate")
    suspend fun rateDriver(
        @Header("Authorization") bearer: String,
        @Path("id") id: String,
        @Body request: RateRequest,
    ): Response<Map<String, Any>>

    // Fleet
    @GET("api/fleet/overview")
    suspend fun getFleetOverview(
        @Header("Authorization") bearer: String,
    ): Response<Map<String, Any>>

    @GET("api/fleet/devices")
    suspend fun getFleetDevices(
        @Header("Authorization") bearer: String,
    ): Response<List<FleetDevice>>

    // Push notification token registration
    @POST("api/notifications/token")
    suspend fun registerPushToken(
        @Header("Authorization") bearer: String,
        @Body request: Map<String, String>,
    ): Response<Map<String, Any>>
}
