//! Notifications API — device push-token registration.
//!
//! `POST /api/notifications/token` — upsert a device push token for the
//! authenticated user.  Called by the mobile apps whenever the OS issues or
//! rotates a FCM (Android) or APNs (iOS) registration token.

use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::post,
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new().route("/token", post(register_token))
}

// ── DTOs ──────────────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
struct RegisterTokenRequest {
    /// `"android"` or `"ios"`.
    platform: String,
    /// FCM registration token (Android) or hex-encoded APNs device token (iOS).
    token: String,
}

#[derive(Debug, Serialize)]
struct RegisterTokenResponse {
    ok: bool,
}

// ── Handler ───────────────────────────────────────────────────────────────────

/// `POST /api/notifications/token`
///
/// Upsert the device push token for the authenticated user.  If a row already
/// exists for `(user_id, platform)` it is updated in-place so the token
/// stays current when the OS rotates it.
async fn register_token(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<RegisterTokenRequest>,
) -> Result<Json<RegisterTokenResponse>, (StatusCode, Json<ErrorBody>)> {
    // Require authentication.
    let bearer = extract_bearer_token(&headers)?;
    let claims = validate_token(&bearer, &state)?;

    // Validate platform value before hitting the DB (the CHECK constraint would
    // catch it too, but this gives a nicer error message).
    if req.platform != "android" && req.platform != "ios" {
        return Err((
            StatusCode::BAD_REQUEST,
            Json(ErrorBody {
                error: ErrorDetail {
                    code: "INVALID_PLATFORM".into(),
                    message: "platform must be \"android\" or \"ios\"".into(),
                },
            }),
        ));
    }

    if req.token.is_empty() {
        return Err((
            StatusCode::BAD_REQUEST,
            Json(ErrorBody {
                error: ErrorDetail {
                    code: "INVALID_TOKEN".into(),
                    message: "token must not be empty".into(),
                },
            }),
        ));
    }

    sqlx::query(
        "INSERT INTO push_tokens (user_id, platform, token, updated_at)
         VALUES ($1::uuid, $2, $3, NOW())
         ON CONFLICT (user_id, platform)
         DO UPDATE SET token = EXCLUDED.token, updated_at = NOW()",
    )
    .bind(&claims.sub)
    .bind(&req.platform)
    .bind(&req.token)
    .execute(&state.db)
    .await
    .map_err(|e| {
        tracing::error!(error = %e, "push_tokens upsert failed");
        (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(ErrorBody {
                error: ErrorDetail {
                    code: "DB_ERROR".into(),
                    message: e.to_string(),
                },
            }),
        )
    })?;

    tracing::info!(
        user_id = %claims.sub,
        platform = %req.platform,
        "Push token registered"
    );

    Ok(Json(RegisterTokenResponse { ok: true }))
}
