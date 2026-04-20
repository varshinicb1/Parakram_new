package com.vidyuthlabs.parakram.ui.screens

import androidx.compose.foundation.background
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
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.Monitor
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.StarBorder
import androidx.compose.material.icons.filled.VerifiedUser
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.vidyuthlabs.parakram.data.repository.ParakramRepository
import com.vidyuthlabs.parakram.domain.model.CommunityDriver
import com.vidyuthlabs.parakram.domain.model.MarketplaceResponse
import com.vidyuthlabs.parakram.ui.theme.VidyuthBackground
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSecondary
import com.vidyuthlabs.parakram.ui.theme.VidyuthSuccess
import com.vidyuthlabs.parakram.ui.theme.VidyuthTertiary
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

// ---- ViewModel ----

data class MarketplaceUiState(
    val drivers: List<CommunityDriver> = emptyList(),
    val featured: List<CommunityDriver> = emptyList(),
    val isLoading: Boolean = false,
    val error: String? = null,
    val search: String = "",
    val selectedType: String? = null,
    val selectedBus: String? = null,
    val page: Int = 1,
    val hasMore: Boolean = true,
    val installedIds: Set<String> = emptySet(),
    val installMessage: String? = null,
)

private val driverTypeFilters = listOf("sensor", "actuator", "display", "combo")
private val busFilters = listOf("i2c", "spi", "gpio", "uart", "onewire")

@HiltViewModel
class MarketplaceViewModel @Inject constructor(
    private val repository: ParakramRepository,
) : ViewModel() {

    private val _uiState = MutableStateFlow(MarketplaceUiState())
    val uiState: StateFlow<MarketplaceUiState> = _uiState.asStateFlow()

    private var searchJob: Job? = null

    init { load(reset = true) }

    fun setSearch(query: String) {
        _uiState.value = _uiState.value.copy(search = query)
        searchJob?.cancel()
        searchJob = viewModelScope.launch {
            delay(350)
            load(reset = true)
        }
    }

    fun setTypeFilter(type: String?) {
        _uiState.value = _uiState.value.copy(selectedType = type)
        load(reset = true)
    }

    fun setBusFilter(bus: String?) {
        _uiState.value = _uiState.value.copy(selectedBus = bus)
        load(reset = true)
    }

    fun loadMore() {
        if (!_uiState.value.hasMore || _uiState.value.isLoading) return
        load(reset = false)
    }

    fun install(driverId: String) {
        viewModelScope.launch {
            repository.installDriver(driverId).fold(
                onSuccess = {
                    _uiState.value = _uiState.value.copy(
                        installedIds = _uiState.value.installedIds + driverId,
                        installMessage = "Driver installed successfully",
                    )
                },
                onFailure = { e ->
                    _uiState.value = _uiState.value.copy(
                        installMessage = e.message ?: "Install failed",
                    )
                },
            )
        }
    }

    fun clearInstallMessage() {
        _uiState.value = _uiState.value.copy(installMessage = null)
    }

    private fun load(reset: Boolean) {
        val state = _uiState.value
        val nextPage = if (reset) 1 else state.page + 1
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, error = null)
            val result = repository.listMarketplaceDrivers(
                search = state.search.ifBlank { null },
                type = state.selectedType,
                bus = state.selectedBus,
                page = nextPage,
            )
            result.fold(
                onSuccess = { response ->
                    val existingList = if (reset) emptyList() else _uiState.value.drivers
                    val newList = existingList + response.drivers
                    _uiState.value = _uiState.value.copy(
                        drivers = newList,
                        featured = if (reset) response.drivers.take(5) else _uiState.value.featured,
                        isLoading = false,
                        page = nextPage,
                        hasMore = newList.size < response.total,
                    )
                },
                onFailure = { e ->
                    _uiState.value = _uiState.value.copy(
                        isLoading = false,
                        error = e.message ?: "Failed to load marketplace",
                    )
                },
            )
        }
    }
}

// ---- Screen ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MarketplaceScreen(
    viewModel: MarketplaceViewModel = hiltViewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(uiState.installMessage) {
        val msg = uiState.installMessage ?: return@LaunchedEffect
        snackbarHostState.showSnackbar(msg)
        viewModel.clearInstallMessage()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text("Marketplace", fontWeight = FontWeight.Bold)
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                ),
            )
        },
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState) { data ->
                Snackbar(snackbarData = data, shape = RoundedCornerShape(12.dp))
            }
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(bottom = 24.dp),
        ) {
            // Search bar
            item(key = "search") {
                MarketplaceSearchBar(
                    query = uiState.search,
                    onQueryChange = viewModel::setSearch,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                )
            }

            // Filter chips — type
            item(key = "type_filters") {
                LazyRow(
                    contentPadding = PaddingValues(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    item {
                        FilterChip(
                            selected = uiState.selectedType == null,
                            onClick = { viewModel.setTypeFilter(null) },
                            label = { Text("All Types") },
                            colors = filterChipColors(),
                        )
                    }
                    items(driverTypeFilters) { type ->
                        FilterChip(
                            selected = uiState.selectedType == type,
                            onClick = {
                                viewModel.setTypeFilter(if (uiState.selectedType == type) null else type)
                            },
                            label = { Text(type.replaceFirstChar { it.uppercase() }) },
                            colors = filterChipColors(),
                        )
                    }
                }
            }

            // Filter chips — bus
            item(key = "bus_filters") {
                LazyRow(
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    item {
                        FilterChip(
                            selected = uiState.selectedBus == null,
                            onClick = { viewModel.setBusFilter(null) },
                            label = { Text("All Buses") },
                            colors = filterChipColors(),
                        )
                    }
                    items(busFilters) { bus ->
                        FilterChip(
                            selected = uiState.selectedBus == bus,
                            onClick = {
                                viewModel.setBusFilter(if (uiState.selectedBus == bus) null else bus)
                            },
                            label = { Text(bus.uppercase()) },
                            colors = filterChipColors(),
                        )
                    }
                }
            }

            // Featured section
            if (uiState.featured.isNotEmpty()) {
                item(key = "featured_header") {
                    Text(
                        "Featured",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 8.dp),
                    )
                }
                item(key = "featured_row") {
                    LazyRow(
                        contentPadding = PaddingValues(horizontal = 16.dp),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        items(uiState.featured, key = { "featured_${it.id}" }) { driver ->
                            FeaturedDriverCard(
                                driver = driver,
                                installed = driver.id in uiState.installedIds,
                                onInstall = { viewModel.install(driver.id) },
                            )
                        }
                    }
                }
            }

            // Grid header
            item(key = "grid_header") {
                Text(
                    "All Drivers",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(start = 16.dp, top = 20.dp, bottom = 8.dp),
                )
            }

            // Driver grid — emulated with two-per-row using chunked rows
            val chunked = uiState.drivers.chunked(2)
            items(chunked, key = { row -> "row_${row.firstOrNull()?.id}" }) { rowDrivers ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    rowDrivers.forEach { driver ->
                        DriverCard(
                            driver = driver,
                            modifier = Modifier.weight(1f),
                            installed = driver.id in uiState.installedIds,
                            onInstall = { viewModel.install(driver.id) },
                        )
                    }
                    // Fill empty cell if odd number
                    if (rowDrivers.size == 1) Spacer(Modifier.weight(1f))
                }
                Spacer(Modifier.height(12.dp))
            }

            // Empty state
            if (!uiState.isLoading && uiState.drivers.isEmpty()) {
                item(key = "empty") {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(48.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Icon(
                            Icons.Default.Search,
                            contentDescription = null,
                            modifier = Modifier.size(56.dp),
                            tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.25f),
                        )
                        Text(
                            "No drivers found",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        )
                        Text(
                            "Try adjusting your filters",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
                        )
                    }
                }
            }

            // Loading shimmer placeholder
            if (uiState.isLoading && uiState.drivers.isEmpty()) {
                items(3, key = { "shimmer_$it" }) {
                    ShimmerCard()
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MarketplaceSearchBar(
    query: String,
    onQueryChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    OutlinedTextField(
        value = query,
        onValueChange = onQueryChange,
        modifier = modifier,
        placeholder = { Text("Search drivers...") },
        leadingIcon = { Icon(Icons.Default.Search, contentDescription = "Search") },
        shape = RoundedCornerShape(16.dp),
        singleLine = true,
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = VidyuthPrimary,
            unfocusedBorderColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.2f),
        ),
    )
}

@Composable
private fun FeaturedDriverCard(
    driver: CommunityDriver,
    installed: Boolean = false,
    onInstall: () -> Unit = {},
) {
    val gradient = driverGradient(driver.driverType)
    Card(
        modifier = Modifier
            .width(220.dp)
            .height(180.dp),
        shape = RoundedCornerShape(20.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Brush.verticalGradient(gradient))
                .padding(20.dp),
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        driverTypeIcon(driver.driverType),
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(24.dp),
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        driver.displayName,
                        fontWeight = FontWeight.Bold,
                        color = Color.White,
                        fontSize = 16.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    driver.description,
                    color = Color.White.copy(alpha = 0.75f),
                    fontSize = 12.sp,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                Button(
                    onClick = onInstall,
                    enabled = !installed,
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color.White.copy(alpha = 0.2f),
                        contentColor = Color.White,
                        disabledContainerColor = Color.White.copy(alpha = 0.1f),
                        disabledContentColor = Color.White.copy(alpha = 0.5f),
                    ),
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp),
                ) {
                    Text(if (installed) "Installed" else "Install", fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                }
            }
        }
    }
}

@Composable
private fun DriverCard(
    driver: CommunityDriver,
    modifier: Modifier = Modifier,
    installed: Boolean = false,
    onInstall: () -> Unit = {},
) {
    val avgStars = if (driver.starsCount > 0) driver.starsTotal / driver.starsCount else 0

    Card(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            // Driver icon
            Box(
                modifier = Modifier
                    .size(44.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(driverGradient(driver.driverType).let { Brush.verticalGradient(it) }),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    driverTypeIcon(driver.driverType),
                    contentDescription = null,
                    tint = Color.White,
                    modifier = Modifier.size(24.dp),
                )
            }

            Spacer(Modifier.height(10.dp))

            Text(
                driver.displayName,
                fontWeight = FontWeight.Bold,
                fontSize = 13.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = MaterialTheme.colorScheme.onSurface,
            )
            Text(
                driver.name,
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )

            Spacer(Modifier.height(6.dp))

            // Bus chips
            LazyRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                items(driver.busTypes) { bus ->
                    Surface(
                        shape = RoundedCornerShape(6.dp),
                        color = VidyuthPrimary.copy(alpha = 0.12f),
                    ) {
                        Text(
                            bus.uppercase(),
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                            fontSize = 9.sp,
                            color = VidyuthPrimary,
                            fontWeight = FontWeight.Medium,
                        )
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            // Star rating
            Row(verticalAlignment = Alignment.CenterVertically) {
                repeat(5) { index ->
                    Icon(
                        if (index < avgStars) Icons.Default.Star else Icons.Default.StarBorder,
                        contentDescription = null,
                        modifier = Modifier.size(12.dp),
                        tint = if (index < avgStars) Color(0xFFFFAB00) else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.3f),
                    )
                }
                Spacer(Modifier.width(4.dp))
                Text(
                    "(${driver.starsCount})",
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
                )
            }

            // Download count
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.padding(top = 2.dp),
            ) {
                Icon(
                    Icons.Default.Download,
                    contentDescription = null,
                    modifier = Modifier.size(12.dp),
                    tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
                )
                Spacer(Modifier.width(3.dp))
                Text(
                    formatCount(driver.downloads),
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
                )
            }

            Spacer(Modifier.height(10.dp))

            Button(
                onClick = onInstall,
                enabled = !installed,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(32.dp),
                shape = RoundedCornerShape(10.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = VidyuthPrimary,
                    disabledContainerColor = VidyuthPrimary.copy(alpha = 0.35f),
                ),
            ) {
                Text(if (installed) "Installed" else "Install", fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            }
        }
    }
}

@Composable
private fun ShimmerCard() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        repeat(2) {
            Card(
                modifier = Modifier
                    .weight(1f)
                    .height(200.dp),
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                ),
            ) {}
        }
    }
}

private fun driverTypeIcon(type: String): ImageVector = when (type) {
    "sensor" -> Icons.Default.Sensors
    "actuator" -> Icons.Default.FlashOn
    "display" -> Icons.Default.Monitor
    else -> Icons.Default.Bolt
}

private fun driverGradient(type: String): List<Color> = when (type) {
    "sensor" -> listOf(Color(0xFF6C63FF), Color(0xFF8B5CF6))
    "actuator" -> listOf(Color(0xFFFF6584), Color(0xFFFF8C42))
    "display" -> listOf(Color(0xFF00D9FF), Color(0xFF0EA5E9))
    else -> listOf(Color(0xFF00E676), Color(0xFF00C853))
}

private fun formatCount(count: Int): String = when {
    count >= 1_000 -> "${count / 1_000}k"
    else -> count.toString()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun filterChipColors() = FilterChipDefaults.filterChipColors(
    selectedContainerColor = VidyuthPrimary.copy(alpha = 0.18f),
    selectedLabelColor = VidyuthPrimary,
    selectedLeadingIconColor = VidyuthPrimary,
)
