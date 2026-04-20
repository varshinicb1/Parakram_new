//! Notification service — FCM (Firebase Cloud Messaging) and APNs (Apple Push Notification service).
//!
//! Credentials are read from environment variables at startup. When a credential
//! is absent the corresponding provider is silently skipped so the rest of the
//! system can call notification methods unconditionally.
//!
//! FCM:  uses the legacy HTTP endpoint (`https://fcm.googleapis.com/fcm/send`)
//!       authenticated with `Authorization: key={FCM_SERVER_KEY}`.
//!
//! APNs: uses the HTTP/1.1-compatible APNs REST endpoint authenticated with a
//!       short-lived ES256 JWT derived from `APNS_KEY_ID`, `APNS_TEAM_ID`, and
//!       `APNS_PRIVATE_KEY` (PEM-encoded ES256 private key).

use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::json;

// ── Credential struct ─────────────────────────────────────────────────────────

/// Holds optional credentials for push notification providers.
///
/// All fields are `None` when the corresponding environment variable is absent;
/// sends will be no-ops but will not panic.
#[derive(Debug, Clone)]
pub struct NotificationService {
    /// Legacy FCM server key (`FCM_SERVER_KEY`).
    fcm_server_key: Option<String>,
    /// APNs key ID (`APNS_KEY_ID`).
    apns_key_id: Option<String>,
    /// APNs team ID (`APNS_TEAM_ID`).
    apns_team_id: Option<String>,
    /// APNs private key in PEM format (`APNS_PRIVATE_KEY`).
    apns_private_key_pem: Option<String>,
    /// APNs bundle / topic ID (`APNS_BUNDLE_ID`, default: `com.vidyuthlabs.parakram`).
    apns_bundle_id: String,
    /// Shared HTTP client (keep-alive, TLS).
    client: reqwest::Client,
}

impl NotificationService {
    /// Read all credentials from the environment.
    ///
    /// Missing variables are treated as `None`; a `tracing::warn` is emitted
    /// so operators can see which providers are disabled.
    pub fn new() -> Self {
        let fcm_server_key = std::env::var("FCM_SERVER_KEY").ok().filter(|s| !s.is_empty());
        let apns_key_id = std::env::var("APNS_KEY_ID").ok().filter(|s| !s.is_empty());
        let apns_team_id = std::env::var("APNS_TEAM_ID").ok().filter(|s| !s.is_empty());
        let apns_private_key_pem = std::env::var("APNS_PRIVATE_KEY").ok().filter(|s| !s.is_empty());
        let apns_bundle_id = std::env::var("APNS_BUNDLE_ID")
            .unwrap_or_else(|_| "com.vidyuthlabs.parakram".into());

        if fcm_server_key.is_none() {
            tracing::warn!("FCM_SERVER_KEY not set — FCM push notifications disabled");
        }
        let apns_ready = apns_key_id.is_some() && apns_team_id.is_some() && apns_private_key_pem.is_some();
        if !apns_ready {
            tracing::warn!(
                "APNS_KEY_ID / APNS_TEAM_ID / APNS_PRIVATE_KEY incomplete — APNs push notifications disabled"
            );
        }

        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(10))
            .build()
            .expect("failed to build HTTP client for NotificationService");

        Self {
            fcm_server_key,
            apns_key_id,
            apns_team_id,
            apns_private_key_pem,
            apns_bundle_id,
            client,
        }
    }

    // ── Public high-level methods ─────────────────────────────────────────────

    /// Notify the user that a deployment completed for `device_name`.
    pub async fn send_deploy_complete(
        &self,
        device_name: &str,
        fcm_token: Option<&str>,
        apns_token: Option<&str>,
    ) {
        let title = "Deployment complete";
        let body = format!("Your program is now running on {}.", device_name);
        self.dispatch(title, &body, fcm_token, apns_token).await;
    }

    /// Warn the user that they have consumed `percent_used`% of their quota.
    pub async fn send_quota_warning(
        &self,
        user_id: &str,
        percent_used: f64,
        fcm_token: Option<&str>,
        apns_token: Option<&str>,
    ) {
        tracing::info!(user_id = %user_id, percent_used = %percent_used, "quota warning notification");
        let title = "Quota warning";
        let body = format!(
            "You have used {:.0}% of your monthly quota. Consider upgrading your plan.",
            percent_used
        );
        self.dispatch(title, &body, fcm_token, apns_token).await;
    }

    /// Notify the user that an OTA firmware update is available.
    pub async fn send_ota_available(
        &self,
        device_name: &str,
        version: &str,
        fcm_token: Option<&str>,
        apns_token: Option<&str>,
    ) {
        let title = "Firmware update available";
        let body = format!("Version {} is ready to install on {}.", version, device_name);
        self.dispatch(title, &body, fcm_token, apns_token).await;
    }

    // ── Internal dispatch ─────────────────────────────────────────────────────

    async fn dispatch(
        &self,
        title: &str,
        body: &str,
        fcm_token: Option<&str>,
        apns_token: Option<&str>,
    ) {
        if let Some(token) = fcm_token {
            if let Err(e) = self.send_fcm_notification(token, title, body).await {
                tracing::warn!(error = %e, "FCM send failed");
            }
        }
        if let Some(token) = apns_token {
            if let Err(e) = self.send_apns_notification(token, title, body).await {
                tracing::warn!(error = %e, "APNs send failed");
            }
        }
        if fcm_token.is_none() && apns_token.is_none() {
            tracing::debug!(title = %title, "No device tokens — notification skipped");
        }
    }

    // ── FCM ───────────────────────────────────────────────────────────────────

    /// Send a notification to a single Android device via the FCM legacy HTTP endpoint.
    async fn send_fcm_notification(
        &self,
        token: &str,
        title: &str,
        body: &str,
    ) -> Result<(), String> {
        let server_key = match &self.fcm_server_key {
            Some(k) => k,
            None => {
                tracing::debug!("FCM skipped — no server key");
                return Ok(());
            }
        };

        let payload = json!({
            "to": token,
            "notification": {
                "title": title,
                "body": body,
                "sound": "default"
            },
            "data": {
                "title": title,
                "body": body
            },
            "priority": "high"
        });

        let resp = self
            .client
            .post("https://fcm.googleapis.com/fcm/send")
            .header("Authorization", format!("key={}", server_key))
            .header("Content-Type", "application/json")
            .json(&payload)
            .send()
            .await
            .map_err(|e| format!("FCM HTTP error: {}", e))?;

        let status = resp.status();
        if status.is_success() {
            tracing::debug!(
                token_prefix = %&token[..token.len().min(8)],
                "FCM notification delivered ({})", status
            );
            Ok(())
        } else {
            let text = resp.text().await.unwrap_or_default();
            Err(format!("FCM responded with {}: {}", status, text))
        }
    }

    // ── APNs ──────────────────────────────────────────────────────────────────

    /// Send a notification to a single iOS device via the APNs HTTP REST endpoint.
    async fn send_apns_notification(
        &self,
        device_token: &str,
        title: &str,
        body: &str,
    ) -> Result<(), String> {
        let jwt = match self.make_apns_jwt() {
            Some(j) => j,
            None => {
                tracing::debug!("APNs skipped — credentials incomplete");
                return Ok(());
            }
        };

        let url = format!(
            "https://api.push.apple.com/3/device/{}",
            device_token
        );

        let payload = json!({
            "aps": {
                "alert": {
                    "title": title,
                    "body": body
                },
                "sound": "default",
                "badge": 1
            }
        });

        let resp = self
            .client
            .post(&url)
            .header("authorization", format!("bearer {}", jwt))
            .header("apns-topic", &self.apns_bundle_id)
            .header("apns-push-type", "alert")
            .header("apns-priority", "10")
            .json(&payload)
            .send()
            .await
            .map_err(|e| format!("APNs HTTP error: {}", e))?;

        let status = resp.status();
        // APNs returns 200 on success; anything else is an error.
        if status.as_u16() == 200 {
            tracing::debug!(
                token_prefix = %&device_token[..device_token.len().min(8)],
                "APNs notification delivered"
            );
            Ok(())
        } else {
            let text = resp.text().await.unwrap_or_default();
            Err(format!("APNs responded with {}: {}", status, text))
        }
    }

    /// Build a short-lived ES256 JWT for APNs provider authentication.
    ///
    /// Returns `None` when any required credential is missing or the private
    /// key cannot be parsed (a warning is logged in that case).
    fn make_apns_jwt(&self) -> Option<String> {
        use jsonwebtoken::{encode, Algorithm, EncodingKey, Header};
        use serde::{Deserialize, Serialize};

        let key_id = self.apns_key_id.as_deref()?;
        let team_id = self.apns_team_id.as_deref()?;
        let pem = self.apns_private_key_pem.as_deref()?;

        #[derive(Serialize, Deserialize)]
        struct ApnsClaims {
            iss: String,
            iat: u64,
        }

        let iat = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let claims = ApnsClaims {
            iss: team_id.to_string(),
            iat,
        };

        let mut header = Header::new(Algorithm::ES256);
        header.kid = Some(key_id.to_string());

        let encoding_key = match EncodingKey::from_ec_pem(pem.as_bytes()) {
            Ok(k) => k,
            Err(e) => {
                tracing::warn!(error = %e, "APNs: failed to parse APNS_PRIVATE_KEY — APNs disabled");
                return None;
            }
        };

        match encode(&header, &claims, &encoding_key) {
            Ok(token) => Some(token),
            Err(e) => {
                tracing::warn!(error = %e, "APNs: JWT signing failed");
                None
            }
        }
    }
}

impl Default for NotificationService {
    fn default() -> Self {
        Self::new()
    }
}

/// Convenience type alias used in `AppState`.
pub type SharedNotificationService = Arc<NotificationService>;
