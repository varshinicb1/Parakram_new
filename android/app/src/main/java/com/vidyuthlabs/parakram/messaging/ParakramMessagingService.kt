package com.vidyuthlabs.parakram.messaging

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import com.vidyuthlabs.parakram.MainActivity
import com.vidyuthlabs.parakram.R
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import javax.inject.Inject

/**
 * Firebase Cloud Messaging service — handles incoming push notifications and
 * token refresh events for the Parakram Android app.
 *
 * The service is registered in AndroidManifest.xml and runs in the background.
 * It degrades silently if Firebase is not configured.
 */
@AndroidEntryPoint
class ParakramMessagingService : FirebaseMessagingService() {

    @Inject
    lateinit var repository: com.vidyuthlabs.parakram.data.repository.ParakramRepository

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    companion object {
        private const val CHANNEL_ID   = "parakram_push"
        private const val CHANNEL_NAME = "Parakram Notifications"
        private const val CHANNEL_DESC = "Deployment and quota alerts from Parakram"
    }

    // ── Token lifecycle ────────────────────────────────────────────────────────

    /**
     * Called by Firebase whenever the FCM registration token is created or
     * rotated. Re-register the new token with the Parakram backend so future
     * push messages reach this device.
     */
    override fun onNewToken(token: String) {
        super.onNewToken(token)
        serviceScope.launch {
            try {
                repository.registerPushToken()
            } catch (e: Exception) {
                // Best-effort — the token will be re-sent on next login.
            }
        }
    }

    // ── Incoming message ───────────────────────────────────────────────────────

    /**
     * Called when a message arrives while the app is in the foreground, or when
     * a data-only message arrives regardless of foreground state.
     *
     * Notification messages while the app is in the background are displayed
     * automatically by the Firebase SDK and this method is NOT called.
     */
    override fun onMessageReceived(message: RemoteMessage) {
        super.onMessageReceived(message)

        val title = message.notification?.title
            ?: message.data["title"]
            ?: "Parakram"

        val body = message.notification?.body
            ?: message.data["body"]
            ?: return // nothing to show

        showLocalNotification(title, body)
    }

    // ── Local notification helper ──────────────────────────────────────────────

    private fun showLocalNotification(title: String, body: String) {
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        // Create notification channel (required on API 26+, no-op on older).
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_DEFAULT,
            ).apply { description = CHANNEL_DESC }
            manager.createNotificationChannel(channel)
        }

        // Tap action — open MainActivity.
        val tapIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val pendingFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        else
            PendingIntent.FLAG_UPDATE_CURRENT

        val pendingIntent = PendingIntent.getActivity(this, 0, tapIntent, pendingFlags)

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(NotificationCompat.BigTextStyle().bigText(body))
            .setAutoCancel(true)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)
            .setContentIntent(pendingIntent)
            .build()

        // Use a stable ID based on the title so duplicate messages collapse.
        manager.notify(title.hashCode(), notification)
    }
}
