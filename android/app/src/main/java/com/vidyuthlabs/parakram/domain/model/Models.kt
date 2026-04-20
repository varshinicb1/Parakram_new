package com.vidyuthlabs.parakram.domain.model

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.JsonObject

@Serializable
data class Device(
    val deviceId: String,
    val name: String,
    val boardSku: String,
    val firmwareVersion: String? = null,
    val status: String = "offline",
    val ipAddress: String? = null,
    val bleAddress: String? = null,
    val activeProgramId: String? = null,
    val errorCount: Int = 0,
    val pairedAt: String = "",
    val lastSeenAt: String? = null,
)

@Serializable
data class Project(
    val projectId: String,
    val userId: String,
    val deviceId: String,
    val name: String,
    val description: String? = null,
    val ir: JsonObject? = null,
    val bytecodeHash: String? = null,
    val createdAt: String = "",
    val updatedAt: String = "",
    val deployedAt: String? = null,
)

@Serializable
data class IntentRequest(
    val description: String,
    val boardId: String,
    val deviceId: String? = null,
)

@Serializable
data class IntentResponse(
    val feasible: Boolean,
    val ir: JsonObject? = null,
    val irPreview: IRPreview? = null,
    val validation: ValidationResult? = null,
    val reason: String? = null,
    val clarifications: List<String>? = null,
    val suggestions: List<String>? = null,
    val llmModel: String = "",
    val generationTimeMs: Long = 0,
)

@Serializable
data class IRPreview(
    val summary: String,
    val triggers: List<TriggerPreview> = emptyList(),
    val actions: List<ActionPreview> = emptyList(),
    val sensorsUsed: List<String> = emptyList(),
    val actuatorsUsed: List<String> = emptyList(),
)

@Serializable
data class TriggerPreview(val description: String, val interval: String = "")

@Serializable
data class ActionPreview(val condition: String, val action: String)

@Serializable
data class ValidationResult(
    val valid: Boolean,
    val errors: List<ValidationError> = emptyList(),
    val warnings: List<ValidationWarning> = emptyList(),
    val stepsCompleted: Int = 0,
)

@Serializable
data class ValidationError(
    val step: Int, val stepName: String, val fieldPath: String,
    val code: String, val message: String,
)

@Serializable
data class ValidationWarning(
    val step: Int, val fieldPath: String, val code: String, val message: String,
)

@Serializable
data class CompileRequest(val ir: JsonObject, val deviceId: String)

@Serializable
data class CompileResult(
    val bytecodeB64: String, val bytecodeSize: Int, val bytecodeHash: String,
    val numInstructions: Int, val numConstants: Int, val numPipelines: Int,
)

@Serializable
data class DeployRequest(
    val bytecodeB64: String, val projectId: String, val transferMethod: String = "wifi",
)

@Serializable
data class LoginRequest(val username: String, val password: String)

@Serializable
data class RegisterRequest(val username: String, val email: String, val password: String)

@Serializable
data class LoginResponse(val token: String, val expiresAt: String, val userId: String)

@Serializable
data class HealthResponse(
    val status: String, val version: String, val database: String,
    val llmAvailable: Boolean, val registeredDrivers: Int,
)

enum class DeploymentState {
    IDLE, COMPILING, COMPILED, TRANSFERRING, VERIFYING, RUNNING, ERROR
}

data class DeploymentProgress(
    val state: DeploymentState = DeploymentState.IDLE,
    val progress: Float = 0f,
    val message: String = "",
    val error: String? = null,
)

@Serializable
data class BillingPlan(
    val tier: String,
    val displayName: String,
    val monthlyPriceUsd: Int,
    val llmIntentsPerMonth: Int,
    val compilesPerMonth: Int,
    val maxDevices: Int,
    val features: List<String>,
)

@Serializable
data class UsageCounter(val used: Int, val limit: Int)

@Serializable
data class UsageView(
    val periodStart: String,
    val llmIntents: UsageCounter,
    val compiles: UsageCounter,
    val deploys: UsageCounter,
    val devicesActive: UsageCounter,
)

@Serializable
data class SubscriptionView(
    val tier: String,
    val displayName: String,
    val monthlyPriceUsd: Int,
    val status: String,
    val cancelAtPeriodEnd: Boolean,
)

@Serializable
data class CommunityDriver(
    val id: String,
    val name: String,
    val displayName: String,
    val description: String,
    val driverType: String,
    val busTypes: List<String>,
    val capabilities: List<String>,
    val status: String,
    val downloads: Int,
    val starsTotal: Int,
    val starsCount: Int,
)

@Serializable
data class CommunityDriverDetail(
    val id: String,
    val name: String,
    val displayName: String,
    val description: String,
    val driverType: String,
    val busTypes: List<String>,
    val capabilities: List<String>,
    val status: String,
    val downloads: Int,
    val starsTotal: Int,
    val starsCount: Int,
    val authorId: String? = null,
    val codePreview: String? = null,
)

@Serializable
data class MarketplaceResponse(
    val drivers: List<CommunityDriver>,
    val total: Int,
    val page: Int,
    val limit: Int,
)

@Serializable
data class RateRequest(val stars: Int, val review: String? = null)

@Serializable
data class FleetDevice(
    val deviceId: String,
    val name: String,
    val boardSku: String,
    val status: String,
    val activeProjectName: String? = null,
    val lastSeenAt: String? = null,
)
