package com.vidyuthlabs.parakram.data.repository

import com.vidyuthlabs.parakram.data.api.ParakramApi
import com.vidyuthlabs.parakram.domain.model.FleetDevice
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class FleetRepository @Inject constructor(
    private val api: ParakramApi,
    private val parakramRepository: ParakramRepository,
) {
    private fun bearer() = parakramRepository.currentBearer()

    suspend fun getFleetOverview(): Result<Map<String, Any>> = try {
        val response = api.getFleetOverview(bearer())
        if (response.isSuccessful) Result.success(response.body() ?: emptyMap())
        else Result.failure(Exception("Fleet overview failed: ${response.code()}"))
    } catch (e: Exception) {
        Result.failure(e)
    }

    suspend fun getFleetDevices(): Result<List<FleetDevice>> = try {
        val response = api.getFleetDevices(bearer())
        if (response.isSuccessful) Result.success(response.body() ?: emptyList())
        else Result.failure(Exception("Fleet devices failed: ${response.code()}"))
    } catch (e: Exception) {
        Result.failure(e)
    }
}
