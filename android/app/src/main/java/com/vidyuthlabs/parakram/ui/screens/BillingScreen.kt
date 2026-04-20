package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.CreditCard
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import com.vidyuthlabs.parakram.domain.model.BillingPlan
import com.vidyuthlabs.parakram.domain.model.SubscriptionView
import com.vidyuthlabs.parakram.domain.model.UsageCounter
import com.vidyuthlabs.parakram.domain.model.UsageView
import com.vidyuthlabs.parakram.ui.theme.VidyuthError
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthTertiary
import com.vidyuthlabs.parakram.ui.theme.VidyuthWarning
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

// ---- ViewModel ----

data class BillingUiState(
    val subscription: SubscriptionView? = null,
    val usage: UsageView? = null,
    val plans: List<BillingPlan> = emptyList(),
    val isLoading: Boolean = false,
    val error: String? = null,
    val pendingUrl: String? = null,
)

@HiltViewModel
class BillingViewModel @Inject constructor(
    private val repository: ParakramRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(BillingUiState())
    val uiState: StateFlow<BillingUiState> = _uiState.asStateFlow()

    init { load() }

    fun load() {
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            val subResult = repository.getMySubscription()
            val usageResult = repository.getMyUsage()
            val plansResult = repository.listPlans()

            _uiState.value = _uiState.value.copy(
                subscription = subResult.getOrNull(),
                usage = usageResult.getOrNull(),
                plans = plansResult.getOrNull() ?: emptyList(),
                isLoading = false,
                error = when {
                    subResult.isFailure -> subResult.exceptionOrNull()?.message
                    else -> null
                },
            )
        }
    }

    fun upgrade(tier: String) {
        viewModelScope.launch {
            repository.createCheckout(tier).onSuccess { url ->
                _uiState.value = _uiState.value.copy(pendingUrl = url)
            }.onFailure { e ->
                _uiState.value = _uiState.value.copy(error = e.message)
            }
        }
    }

    fun manageSubscription() {
        viewModelScope.launch {
            repository.createPortal().onSuccess { url ->
                _uiState.value = _uiState.value.copy(pendingUrl = url)
            }.onFailure { e ->
                _uiState.value = _uiState.value.copy(error = e.message)
            }
        }
    }

    fun clearPendingUrl() {
        _uiState.value = _uiState.value.copy(pendingUrl = null)
    }
}

// ---- Screen ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BillingScreen(
    viewModel: BillingViewModel = hiltViewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current

    LaunchedEffect(uiState.pendingUrl) {
        val url = uiState.pendingUrl ?: return@LaunchedEffect
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
        viewModel.clearPendingUrl()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Plan & Usage", fontWeight = FontWeight.Bold) },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        if (uiState.isLoading && uiState.subscription == null) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding),
                contentAlignment = Alignment.Center,
            ) {
                CircularProgressIndicator(color = VidyuthPrimary)
            }
            return@Scaffold
        }

        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp),
        ) {
            // Current plan card
            uiState.subscription?.let { sub ->
                item(key = "plan_card") {
                    CurrentPlanCard(subscription = sub)
                }
            }

            // Usage section
            uiState.usage?.let { usage ->
                item(key = "usage_header") {
                    Text(
                        "This Month's Usage",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                    )
                }
                item(key = "usage_section") {
                    Card(
                        shape = RoundedCornerShape(16.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.surface,
                        ),
                        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
                    ) {
                        Column(
                            modifier = Modifier.padding(20.dp),
                            verticalArrangement = Arrangement.spacedBy(20.dp),
                        ) {
                            UsageRow(label = "LLM Intents", counter = usage.llmIntents)
                            UsageRow(label = "Compiles", counter = usage.compiles)
                            UsageRow(label = "Deploys", counter = usage.deploys)
                            UsageRow(label = "Active Devices", counter = usage.devicesActive)
                        }
                    }
                }
            }

            // Plan comparison
            if (uiState.plans.isNotEmpty()) {
                item(key = "plans_header") {
                    Text(
                        "Compare Plans",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                    )
                }
                item(key = "plans_row") {
                    LazyRow(
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        contentPadding = PaddingValues(end = 4.dp),
                    ) {
                        items(uiState.plans, key = { it.tier }) { plan ->
                            PlanCard(
                                plan = plan,
                                isCurrentPlan = plan.tier == uiState.subscription?.tier,
                                onUpgrade = { viewModel.upgrade(plan.tier) },
                                onManage = { viewModel.manageSubscription() },
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CurrentPlanCard(subscription: SubscriptionView) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.horizontalGradient(
                    listOf(VidyuthPrimary, Color(0xFF8B5CF6)),
                ),
            )
            .padding(24.dp),
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column {
                    Text(
                        subscription.displayName,
                        fontSize = 26.sp,
                        fontWeight = FontWeight.Bold,
                        color = Color.White,
                    )
                    if (subscription.monthlyPriceUsd == 0) {
                        Text("Free forever", color = Color.White.copy(alpha = 0.7f))
                    } else {
                        Text(
                            "\$${subscription.monthlyPriceUsd}/mo",
                            color = Color.White.copy(alpha = 0.85f),
                            fontSize = 16.sp,
                        )
                    }
                }
                Icon(
                    Icons.Default.CreditCard,
                    contentDescription = null,
                    modifier = Modifier.size(40.dp),
                    tint = Color.White.copy(alpha = 0.5f),
                )
            }
            Spacer(Modifier.height(16.dp))
            StatusBadge(status = subscription.status)
            if (subscription.cancelAtPeriodEnd) {
                Spacer(Modifier.height(8.dp))
                Text(
                    "Cancels at end of billing period",
                    color = Color.White.copy(alpha = 0.65f),
                    fontSize = 12.sp,
                )
            }
        }
    }
}

@Composable
private fun StatusBadge(status: String) {
    val color = when (status.lowercase()) {
        "active" -> VidyuthSuccess
        "trialing" -> VidyuthSecondary
        "past_due" -> VidyuthWarning
        "canceled" -> VidyuthError
        else -> Color.Gray
    }
    Surface(
        shape = RoundedCornerShape(20.dp),
        color = Color.White.copy(alpha = 0.15f),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(color),
            )
            Text(
                status.replaceFirstChar { it.uppercase() },
                color = Color.White,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium,
            )
        }
    }
}

@Composable
private fun UsageRow(label: String, counter: UsageCounter) {
    val ratio = if (counter.limit > 0) counter.used.toFloat() / counter.limit else 0f
    val progressColor = when {
        ratio >= 0.90f -> VidyuthError
        ratio >= 0.70f -> VidyuthWarning
        else -> VidyuthSuccess
    }

    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                label,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium,
            )
            Text(
                "${formatUsageNumber(counter.used)} / ${if (counter.limit == Int.MAX_VALUE) "∞" else formatUsageNumber(counter.limit)}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            )
        }
        LinearProgressIndicator(
            progress = ratio.coerceIn(0f, 1f),
            modifier = Modifier
                .fillMaxWidth()
                .height(8.dp)
                .clip(RoundedCornerShape(4.dp)),
            color = progressColor,
            trackColor = progressColor.copy(alpha = 0.15f),
        )
    }
}

@Composable
private fun PlanCard(
    plan: BillingPlan,
    isCurrentPlan: Boolean,
    onUpgrade: () -> Unit = {},
    onManage: () -> Unit = {},
) {
    val borderMod = if (isCurrentPlan) {
        Modifier.border(2.dp, VidyuthPrimary, RoundedCornerShape(16.dp))
    } else {
        Modifier
    }

    Card(
        modifier = Modifier
            .width(200.dp)
            .then(borderMod),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (isCurrentPlan)
                VidyuthPrimary.copy(alpha = 0.08f)
            else
                MaterialTheme.colorScheme.surface,
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            if (isCurrentPlan) {
                Surface(
                    shape = RoundedCornerShape(8.dp),
                    color = VidyuthPrimary.copy(alpha = 0.15f),
                    modifier = Modifier.padding(bottom = 8.dp),
                ) {
                    Text(
                        "Current",
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                        fontSize = 10.sp,
                        color = VidyuthPrimary,
                        fontWeight = FontWeight.Bold,
                    )
                }
            }

            Text(
                plan.displayName,
                fontWeight = FontWeight.Bold,
                fontSize = 18.sp,
            )
            Spacer(Modifier.height(4.dp))
            Text(
                if (plan.monthlyPriceUsd == 0) "Free" else "\$${plan.monthlyPriceUsd}/mo",
                color = VidyuthPrimary,
                fontWeight = FontWeight.Bold,
                fontSize = 22.sp,
            )

            Spacer(Modifier.height(12.dp))

            // Key limits
            PlanLimitRow(label = "${formatUsageNumber(plan.llmIntentsPerMonth)} intents/mo")
            PlanLimitRow(label = "${formatUsageNumber(plan.compilesPerMonth)} compiles/mo")
            PlanLimitRow(label = "${plan.maxDevices} devices")

            Spacer(Modifier.height(16.dp))

            if (isCurrentPlan) {
                OutlinedButton(
                    onClick = onManage,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                ) {
                    Text("Manage", fontSize = 12.sp)
                }
            } else {
                Button(
                    onClick = onUpgrade,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = VidyuthPrimary),
                ) {
                    Text("Upgrade", fontSize = 12.sp)
                }
            }
        }
    }
}

@Composable
private fun PlanLimitRow(label: String) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.padding(vertical = 2.dp),
    ) {
        Icon(
            Icons.Default.CheckCircle,
            contentDescription = null,
            modifier = Modifier.size(14.dp),
            tint = VidyuthSuccess,
        )
        Spacer(Modifier.width(6.dp))
        Text(
            label,
            fontSize = 12.sp,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.75f),
        )
    }
}

private fun formatUsageNumber(n: Int): String = when {
    n >= 1_000_000 -> "${n / 1_000_000}M"
    n >= 1_000 -> "${n / 1_000}k"
    else -> n.toString()
}
