//! OTA firmware delivery endpoints.
//!
//! GET /api/ota/check/:deviceId   — check if a firmware update is available
//! GET /api/ota/manifest/:deviceId — return full update manifest
//! GET /api/ota/chunk/:deviceId   — stream the firmware bytecode (device-facing, no JWT)

use axum::{
    body::Body,
    extract::{Path, State},
    http::{header, HeaderMap, StatusCode},
    response::Response,
    routing::get,
    Json, Router,
};
use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/check/:device_id", get(check_update))
        .route("/manifest/:device_id", get(get_manifest))
        .route("/chunk/:device_id", get(stream_chunk))
}

// ---------------------------------------------------------------------------
// Response types
// ---------------------------------------------------------------------------

#[derive(Debug, Serialize)]
pub struct CheckResponse {
    pub has_update: bool,
    pub version: String,
    pub size: u64,
    pub hash: String,
}

#[derive(Debug, Serialize)]
pub struct ManifestResponse {
    pub url: String,
    pub hash: String,
    pub size: u64,
    pub version: String,
    pub signature: String,
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn db_err(e: sqlx::Error) -> (StatusCode, Json<ErrorBody>) {
    (
        StatusCode::INTERNAL_SERVER_ERROR,
        Json(ErrorBody {
            error: ErrorDetail {
                code: "DB_ERROR".into(),
                message: e.to_string(),
            },
        }),
    )
}

fn not_found(msg: &str) -> (StatusCode, Json<ErrorBody>) {
    (
        StatusCode::NOT_FOUND,
        Json(ErrorBody {
            error: ErrorDetail {
                code: "NOT_FOUND".into(),
                message: msg.into(),
            },
        }),
    )
}

/// Fetch `(firmware_version, device_id_text)` for a device owned by any user.
async fn fetch_device_firmware(
    db: &sqlx::PgPool,
    device_id: &str,
) -> Result<Option<(Option<String>, String)>, sqlx::Error> {
    sqlx::query_as::<_, (Option<String>, String)>(
        "SELECT firmware_version, device_id::text FROM devices WHERE device_id = $1::uuid",
    )
    .bind(device_id)
    .fetch_optional(db)
    .await
}

/// Fetch the latest project for a device: `(bytecode_hash, bytecode_b64, project_id_text)`.
async fn fetch_latest_project(
    db: &sqlx::PgPool,
    device_id: &str,
) -> Result<
    Option<(Option<String>, Option<String>, String)>,
    sqlx::Error,
> {
    sqlx::query_as::<_, (Option<String>, Option<String>, String)>(
        "SELECT bytecode_hash, bytecode_b64, project_id::text
         FROM projects
         WHERE device_id = $1::uuid AND bytecode_hash IS NOT NULL
         ORDER BY last_deployed_at DESC NULLS LAST, updated_at DESC
         LIMIT 1",
    )
    .bind(device_id)
    .fetch_optional(db)
    .await
}

// ---------------------------------------------------------------------------
// GET /api/ota/check/:deviceId — requires JWT Bearer
// ---------------------------------------------------------------------------

async fn check_update(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(device_id): Path<String>,
) -> Result<Json<CheckResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let _claims = validate_token(&token, &state)?;

    let device = fetch_device_firmware(&state.db, &device_id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("Device not found"))?;

    let current_fw = device.0.unwrap_or_default();

    let project = fetch_latest_project(&state.db, &device_id)
        .await
        .map_err(db_err)?;

    let (has_update, version, size, hash) = match project {
        None => (false, current_fw, 0u64, String::new()),
        Some((bytecode_hash, bytecode_b64, _project_id)) => {
            let hash = bytecode_hash.unwrap_or_default();
            let bytes = bytecode_b64
                .as_deref()
                .and_then(|b| base64::Engine::decode(&base64::engine::general_purpose::STANDARD, b).ok())
                .unwrap_or_default();
            let size = bytes.len() as u64;
            let has_update = hash != current_fw && !hash.is_empty();
            let version = hash.clone();
            (has_update, version, size, hash)
        }
    };

    tracing::info!(
        device_id = %device_id,
        has_update = has_update,
        "OTA check"
    );

    Ok(Json(CheckResponse {
        has_update,
        version,
        size,
        hash,
    }))
}

// ---------------------------------------------------------------------------
// GET /api/ota/manifest/:deviceId — requires JWT Bearer
// ---------------------------------------------------------------------------

async fn get_manifest(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(device_id): Path<String>,
) -> Result<Json<ManifestResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let _claims = validate_token(&token, &state)?;

    // Device must exist
    fetch_device_firmware(&state.db, &device_id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("Device not found"))?;

    let project = fetch_latest_project(&state.db, &device_id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("No compiled project found for device"))?;

    let hash = project.0.unwrap_or_default();
    let bytes = project
        .1
        .as_deref()
        .and_then(|b| base64::Engine::decode(&base64::engine::general_purpose::STANDARD, b).ok())
        .unwrap_or_default();
    let size = bytes.len() as u64;
    let version = hash.clone();

    // Signature: hex(sha256(hash + device_id))
    let mut hasher = Sha256::new();
    hasher.update(hash.as_bytes());
    hasher.update(device_id.as_bytes());
    let sig_bytes = hasher.finalize();
    let signature = hex::encode(sig_bytes);

    let url = format!("/api/ota/chunk/{}", device_id);

    tracing::info!(device_id = %device_id, "OTA manifest requested");

    Ok(Json(ManifestResponse {
        url,
        hash,
        size,
        version,
        signature,
    }))
}

// ---------------------------------------------------------------------------
// GET /api/ota/chunk/:deviceId — device-facing, no JWT, X-Device-Key required
// ---------------------------------------------------------------------------

async fn stream_chunk(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(device_id): Path<String>,
) -> Result<Response, (StatusCode, Json<ErrorBody>)> {
    // Lightweight auth: X-Device-Key must be present and non-empty
    let device_key = headers
        .get("X-Device-Key")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .trim()
        .to_owned();

    if device_key.is_empty() {
        return Err((
            StatusCode::UNAUTHORIZED,
            Json(ErrorBody {
                error: ErrorDetail {
                    code: "MISSING_DEVICE_KEY".into(),
                    message: "X-Device-Key header required".into(),
                },
            }),
        ));
    }

    // Device must exist
    fetch_device_firmware(&state.db, &device_id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("Device not found"))?;

    let project = fetch_latest_project(&state.db, &device_id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("No compiled project found for device"))?;

    let b64 = project.1.unwrap_or_default();
    let bytes = base64::Engine::decode(&base64::engine::general_purpose::STANDARD, &b64)
        .map_err(|e| {
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorBody {
                    error: ErrorDetail {
                        code: "DECODE_ERROR".into(),
                        message: e.to_string(),
                    },
                }),
            )
        })?;

    tracing::info!(
        device_id = %device_id,
        bytes = bytes.len(),
        "OTA chunk streamed"
    );

    let response = Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_TYPE, "application/octet-stream")
        .header(header::CONTENT_LENGTH, bytes.len().to_string())
        .body(Body::from(bytes))
        .map_err(|e| {
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorBody {
                    error: ErrorDetail {
                        code: "RESPONSE_BUILD_ERROR".into(),
                        message: e.to_string(),
                    },
                }),
            )
        })?;

    Ok(response)
}
