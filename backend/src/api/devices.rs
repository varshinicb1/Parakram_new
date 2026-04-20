//! Device management endpoints.

use axum::{extract::{Path, State}, http::StatusCode, routing::{get, post}, Json, Router};
use serde::{Deserialize, Serialize};
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_devices))
        .route("/:device_id", get(get_device).patch(update_device).delete(unpair_device))
        .route("/pair", post(pair_device))
}

#[derive(Debug, Serialize)]
pub struct DeviceResponse {
    pub device_id: String,
    pub name: String,
    pub board_sku: String,
    pub firmware_version: Option<String>,
    pub status: String,
    pub ip_address: Option<String>,
    pub ble_address: Option<String>,
    pub active_program_id: Option<String>,
    pub error_count: i32,
    pub paired_at: String,
    pub last_seen_at: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct DeviceListResponse {
    pub devices: Vec<DeviceResponse>,
}

#[derive(Debug, Deserialize)]
pub struct PairDeviceRequest {
    pub device_uuid: String,
    pub board_sku: String,
    pub device_pubkey: String,
    #[serde(default = "default_device_name")]
    pub name: String,
}

fn default_device_name() -> String { "My Parakram Device".into() }

#[derive(Debug, Deserialize)]
pub struct UpdateDeviceRequest {
    #[serde(default)]
    pub name: Option<String>,
}

fn row_to_response(r: crate::db::models::DeviceRow) -> DeviceResponse {
    DeviceResponse {
        device_id: r.device_id, name: r.name, board_sku: r.board_sku,
        firmware_version: r.firmware_version, status: r.status,
        ip_address: r.ip_address, ble_address: r.ble_address,
        active_program_id: r.active_program_id, error_count: r.error_count,
        paired_at: r.paired_at, last_seen_at: r.last_seen_at,
    }
}

pub const DEVICE_SELECT: &str = "
    SELECT device_id::text, user_id::text, board_sku, name, device_pubkey,
           firmware_version, ip_address, ble_address, status,
           active_program_id::text, error_count,
           paired_at::text, last_seen_at::text
    FROM devices";

async fn list_devices(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> Result<Json<DeviceListResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let rows = sqlx::query_as::<_, crate::db::models::DeviceRow>(
        &format!("{} WHERE user_id = $1::uuid ORDER BY paired_at DESC", DEVICE_SELECT)
    )
    .bind(&claims.sub)
    .fetch_all(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    Ok(Json(DeviceListResponse {
        devices: rows.into_iter().map(row_to_response).collect(),
    }))
}

async fn get_device(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(device_id): Path<String>,
) -> Result<Json<DeviceResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let row = sqlx::query_as::<_, crate::db::models::DeviceRow>(
        &format!("{} WHERE device_id = $1::uuid AND user_id = $2::uuid", DEVICE_SELECT)
    )
    .bind(&device_id).bind(&claims.sub)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
        error: ErrorDetail { code: "NOT_FOUND".into(), message: "Device not found".into() },
    })))?;

    Ok(Json(row_to_response(row)))
}

async fn pair_device(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<PairDeviceRequest>,
) -> Result<(StatusCode, Json<DeviceResponse>), (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let existing: (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM devices WHERE device_id = $1::uuid"
    )
    .bind(&req.device_uuid)
    .fetch_one(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    if existing.0 > 0 {
        return Err((StatusCode::CONFLICT, Json(ErrorBody {
            error: ErrorDetail { code: "ALREADY_PAIRED".into(), message: "Device already paired".into() },
        })));
    }

    sqlx::query(
        "INSERT INTO devices (device_id, user_id, board_sku, name, device_pubkey)
         VALUES ($1::uuid, $2::uuid, $3, $4, $5)"
    )
    .bind(&req.device_uuid)
    .bind(&claims.sub)
    .bind(&req.board_sku)
    .bind(&req.name)
    .bind(&req.device_pubkey)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    Ok((StatusCode::CREATED, Json(DeviceResponse {
        device_id: req.device_uuid, name: req.name, board_sku: req.board_sku,
        firmware_version: None, status: "offline".into(),
        ip_address: None, ble_address: None, active_program_id: None,
        error_count: 0, paired_at: chrono::Utc::now().to_rfc3339(), last_seen_at: None,
    })))
}

async fn update_device(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(device_id): Path<String>,
    Json(req): Json<UpdateDeviceRequest>,
) -> Result<Json<DeviceResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    if let Some(ref name) = req.name {
        sqlx::query(
            "UPDATE devices SET name = $1 WHERE device_id = $2::uuid AND user_id = $3::uuid"
        )
        .bind(name).bind(&device_id).bind(&claims.sub)
        .execute(&state.db).await
        .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
            error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
        })))?;
    }

    get_device(State(state), headers, Path(device_id)).await
}

async fn unpair_device(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(device_id): Path<String>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    sqlx::query(
        "DELETE FROM devices WHERE device_id = $1::uuid AND user_id = $2::uuid"
    )
    .bind(&device_id).bind(&claims.sub)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    Ok(StatusCode::NO_CONTENT)
}
