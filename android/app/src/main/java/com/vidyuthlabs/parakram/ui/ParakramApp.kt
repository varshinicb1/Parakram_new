package com.vidyuthlabs.parakram.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Router
import androidx.compose.material.icons.filled.Store
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.vidyuthlabs.parakram.ui.screens.BillingScreen
import com.vidyuthlabs.parakram.ui.screens.DevicesScreen
import com.vidyuthlabs.parakram.ui.screens.FleetScreen
import com.vidyuthlabs.parakram.ui.screens.ForgotPasswordScreen
import com.vidyuthlabs.parakram.ui.screens.VerifyEmailScreen
import com.vidyuthlabs.parakram.ui.screens.HomeScreen
import com.vidyuthlabs.parakram.ui.screens.LoginScreen
import com.vidyuthlabs.parakram.ui.screens.MarketplaceScreen
import com.vidyuthlabs.parakram.ui.screens.PairDeviceScreen
import com.vidyuthlabs.parakram.ui.screens.ProgramScreen
import com.vidyuthlabs.parakram.ui.screens.RegisterScreen
import com.vidyuthlabs.parakram.ui.screens.SettingsScreen
import com.vidyuthlabs.parakram.ui.screens.TelemetryScreen
import com.vidyuthlabs.parakram.ui.theme.VidyuthPrimary

// Route constants
private const val ROUTE_LOGIN = "login"
private const val ROUTE_REGISTER = "register"
private const val ROUTE_FORGOT_PASSWORD = "forgot_password"
private const val ROUTE_VERIFY_EMAIL = "verify_email/{username}"

private fun verifyEmailRoute(username: String) = "verify_email/$username"
private const val ROUTE_HOME = "home"
private const val ROUTE_DEVICES = "devices"
private const val ROUTE_PAIR_DEVICE = "pair_device"
private const val ROUTE_PROGRAM = "program"
private const val ROUTE_MARKETPLACE = "marketplace"
private const val ROUTE_SETTINGS = "settings"
private const val ROUTE_BILLING = "billing"
private const val ROUTE_FLEET = "fleet"
private const val ROUTE_TELEMETRY = "telemetry/{deviceId}"

private fun telemetryRoute(deviceId: String) = "telemetry/$deviceId"

private data class BottomNavItem(
    val route: String,
    val label: String,
    val icon: ImageVector,
)

private val bottomNavItems = listOf(
    BottomNavItem(ROUTE_HOME, "Home", Icons.Default.Home),
    BottomNavItem(ROUTE_DEVICES, "Devices", Icons.Default.Router),
    BottomNavItem(ROUTE_PROGRAM, "Build", Icons.Default.Terminal),
    BottomNavItem(ROUTE_MARKETPLACE, "Market", Icons.Default.Store),
    BottomNavItem(ROUTE_SETTINGS, "Profile", Icons.Default.Person),
)

private val bottomNavRoutes = bottomNavItems.map { it.route }.toSet()

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ParakramApp() {
    val navController = rememberNavController()

    // Track login state (real implementation would use a ViewModel or DataStore)
    var isLoggedIn by remember { mutableStateOf(false) }

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination
    val currentRoute = currentDestination?.route

    val showBottomBar = isLoggedIn && currentRoute in bottomNavRoutes

    Scaffold(
        bottomBar = {
            AnimatedVisibility(
                visible = showBottomBar,
                enter = fadeIn(),
                exit = fadeOut(),
            ) {
                NavigationBar {
                    bottomNavItems.forEach { item ->
                        val selected = currentDestination?.hierarchy?.any { it.route == item.route } == true
                        NavigationBarItem(
                            icon = { Icon(item.icon, contentDescription = item.label) },
                            label = {
                                Text(
                                    item.label,
                                    fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
                                )
                            },
                            selected = selected,
                            onClick = {
                                navController.navigate(item.route) {
                                    popUpTo(navController.graph.findStartDestination().id) {
                                        saveState = true
                                    }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = VidyuthPrimary,
                                selectedTextColor = VidyuthPrimary,
                                indicatorColor = VidyuthPrimary.copy(alpha = 0.12f),
                            ),
                        )
                    }
                }
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = if (isLoggedIn) ROUTE_HOME else ROUTE_LOGIN,
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            // Login — shown when not authenticated
            composable(ROUTE_LOGIN) {
                LoginScreen(
                    onLoginSuccess = {
                        isLoggedIn = true
                        navController.navigate(ROUTE_HOME) {
                            popUpTo(ROUTE_LOGIN) { inclusive = true }
                        }
                    },
                    onNavigateToSignUp = {
                        navController.navigate(ROUTE_REGISTER)
                    },
                    onNavigateToForgotPassword = {
                        navController.navigate(ROUTE_FORGOT_PASSWORD)
                    },
                )
            }

            // Register
            composable(ROUTE_REGISTER) {
                RegisterScreen(
                    onRegisterSuccess = {
                        isLoggedIn = true
                        navController.navigate(ROUTE_HOME) {
                            popUpTo(0) { inclusive = true }
                        }
                    },
                    onNeedsVerification = { username ->
                        navController.navigate(verifyEmailRoute(username)) {
                            popUpTo(ROUTE_REGISTER) { inclusive = true }
                        }
                    },
                    onNavigateToLogin = {
                        navController.popBackStack()
                    },
                )
            }

            // Verify email (after registration with email)
            composable(ROUTE_VERIFY_EMAIL) { backStackEntry ->
                val username = backStackEntry.arguments?.getString("username") ?: ""
                VerifyEmailScreen(
                    username = username,
                    onVerified = {
                        navController.navigate(ROUTE_LOGIN) {
                            popUpTo(0) { inclusive = true }
                        }
                    },
                    onNavigateToLogin = {
                        navController.navigate(ROUTE_LOGIN) {
                            popUpTo(0) { inclusive = true }
                        }
                    },
                )
            }

            // Forgot password
            composable(ROUTE_FORGOT_PASSWORD) {
                ForgotPasswordScreen(
                    onNavigateToLogin = {
                        navController.navigate(ROUTE_LOGIN) {
                            popUpTo(ROUTE_FORGOT_PASSWORD) { inclusive = true }
                        }
                    },
                )
            }

            // Main tabs
            composable(ROUTE_HOME) {
                HomeScreen(
                    onNavigateToProgram = { navController.navigate(ROUTE_PROGRAM) },
                    onNavigateToDevices = { navController.navigate(ROUTE_DEVICES) },
                    onNavigateToSettings = { navController.navigate(ROUTE_SETTINGS) },
                    onNavigateToMarketplace = { navController.navigate(ROUTE_MARKETPLACE) },
                    onNavigateToBilling = { navController.navigate(ROUTE_BILLING) },
                    onNavigateToFleet = { navController.navigate(ROUTE_FLEET) },
                )
            }

            composable(ROUTE_DEVICES) {
                DevicesScreen(
                    onNavigateBack = { navController.popBackStack() },
                    onNavigateToTelemetry = { deviceId ->
                        navController.navigate(telemetryRoute(deviceId))
                    },
                )
            }

            composable(ROUTE_PROGRAM) {
                ProgramScreen(
                    onNavigateBack = { navController.popBackStack() },
                    onNavigateToDevices = { navController.navigate(ROUTE_DEVICES) },
                )
            }

            composable(ROUTE_MARKETPLACE) {
                MarketplaceScreen()
            }

            composable(ROUTE_SETTINGS) {
                SettingsScreen(
                    onNavigateBack = { navController.popBackStack() },
                    onSignOut = {
                        isLoggedIn = false
                        navController.navigate(ROUTE_LOGIN) {
                            popUpTo(0) { inclusive = true }
                        }
                    },
                    onNavigateToSubmissions = {
                        navController.navigate(ROUTE_MARKETPLACE)
                    },
                )
            }

            // Secondary screens (no bottom bar)
            composable(ROUTE_BILLING) {
                BillingScreen()
            }

            composable(ROUTE_FLEET) {
                FleetScreen(
                    onNavigateToTelemetry = { deviceId ->
                        navController.navigate(telemetryRoute(deviceId))
                    },
                )
            }

            composable(ROUTE_PAIR_DEVICE) {
                PairDeviceScreen(
                    onNavigateBack = { navController.popBackStack() },
                    onPairSuccess = {
                        navController.navigate(ROUTE_DEVICES) {
                            popUpTo(ROUTE_PAIR_DEVICE) { inclusive = true }
                        }
                    },
                )
            }

            composable(ROUTE_TELEMETRY) { backStackEntry ->
                val deviceId = backStackEntry.arguments?.getString("deviceId") ?: ""
                TelemetryScreen(
                    deviceId = deviceId,
                    onNavigateBack = { navController.popBackStack() },
                )
            }
        }
    }
}
