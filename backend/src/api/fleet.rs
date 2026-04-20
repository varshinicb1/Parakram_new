//! Fleet overview endpoints.
//!
//! GET  /api/fleet/overview                   — aggregated stats
//! GET  /api/fleet/devices                    — devices with active project name
//! POST /api/fleet/devices/:deviceId/ping     — update last_seen_at

use axum::{
    extract::{Path, State},
    http::{HeaderMap, StatusCode},
    routing::{get, post},
    Json, Router,
};
use serde::Serialize;

use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/overview", get(fleet_overview))
        .route("/devices", get(fleet_devices))
        .route("/devices/:device_id/ping", post(device_ping))
}

// ---------------------------------------------------------------------------
// Response types
// ---------------------------------------------------------------------------

#[derive(Debug, Serialize)]
pub struct RecentDeploy {
    pub device_name: String,
    pub project_name: String,
    pub deployed_at: String,
}

#[derive(Debug, Serialize)]
pub struct FleetOverviewResponse {
    pub total_devices: i64,
    pub online_devices: i64,
    pub total_projects: i64,
    pub deployed_projects: i64,
    pub recent_deploys: Vec<RecentDeploy>,
}

#[derive(Debug, Serialize, sqlx::FromRow)]
pub struct FleetDevice {
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
    pub active_project_name: Option<String>,
}

// ---------------------------------------------------------------------------
// sqlx row types for manual queries
// ---------------------------------------------------------------------------

#[derive(sqlx::FromRow)]
struct RecentDeployRow {
    device_name: String,
    project_name: String,
    deployed_at: String,
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

// ---------------------------------------------------------------------------
// GET /api/fleet/overview
// ---------------------------------------------------------------------------

async fn fleet_overview(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<FleetOverviewResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;
    let user_id = &claims.sub;

    // Total devices
    let (total_devices,): (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM devices WHERE user_id = $1::uuid",
    )
    .bind(user_id)
    .fetch_one(&state.db)
    .await
    .map_err(db_err)?;

    // Online devices (status = 'online')
    let (online_devices,): (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM devices WHERE user_id = $1::uuid AND status = 'online'",
    )
    .bind(user_id)
    .fetch_one(&state.db)
    .await
    .map_err(db_err)?;

    // Total projects
    let (total_projects,): (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM projects WHERE user_id = $1::uuid",
    )
    .bind(user_id)
    .fetch_one(&state.db)
    .await
    .map_err(db_err)?;

    // Deployed projects (last_deployed_at IS NOT NULL)
    let (deployed_projects,): (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM projects WHERE user_id = $1::uuid AND last_deployed_at IS NOT NULL",
    )
    .bind(user_id)
    .fetch_one(&state.db)
    .await
    .map_err(db_err)?;

    // Recent deploys (last 10)
    let rows = sqlx::query_as::<_, RecentDeployRow>(
        "SELECT d.name AS device_name,
                p.name AS project_name,
                p.last_deployed_at::text AS deployed_at
         FROM projects p
         JOIN devices d ON p.device_id = d.device_id
         WHERE p.user_id = $1::uuid AND p.last_deployed_at IS NOT NULL
         ORDER BY p.last_deployed_at DESC
         LIMIT 10",
    )
    .bind(user_id)
    .fetch_all(&state.db)
    .await
    .map_err(db_err)?;

    let recent_deploys = rows
        .into_iter()
        .map(|r| RecentDeploy {
            device_name: r.device_name,
            project_name: r.project_name,
            deployed_at: r.deployed_at,
        })
        .collect();

    tracing::info!(user_id = %user_id, "Fleet overview requested");

    Ok(Json(FleetOverviewResponse {
        total_devices,
        online_devices,
        total_projects,
        deployed_projects,
        recent_deploys,
    }))
}

// ---------------------------------------------------------------------------
// GET /api/fleet/devices
// ---------------------------------------------------------------------------

async fn fleet_devices(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<Vec<FleetDevice>>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;
    let user_id = &claims.sub;

    let devices = sqlx::query_as::<_, FleetDevice>(
        "SELECT d.device_id::text,
                d.name,
                d.board_sku,
                d.firmware_version,
                d.status,
                d.ip_address,
                d.ble_address,
                d.active_program_id::text,
                d.error_count,
                d.paired_at::text,
                d.last_seen_at::text,
                p.name AS active_project_name
         FROM devices d
         LEFT JOIN projects p ON d.active_program_id = p.project_id
         WHERE d.user_id = $1::uuid
         ORDER BY d.paired_at DESC",
    )
    .bind(user_id)
    .fetch_all(&state.db)
    .await
    .map_err(db_err)?;

    tracing::info!(user_id = %user_id, count = devices.len(), "Fleet devices listed");

    Ok(Json(devices))
}

// ---------------------------------------------------------------------------
// POST /api/fleet/devices/:deviceId/ping
// ---------------------------------------------------------------------------

#[derive(Debug, Serialize)]
struct PingResponse {
    ok: bool,
}

async fn device_ping(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(device_id): Path<String>,
) -> Result<Json<PingResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;
    let user_id = &claims.sub;

    let result = sqlx::query(
        "UPDATE devices
         SET last_seen_at = NOW()
         WHERE device_id = $1::uuid AND user_id = $2::uuid",
    )
    .bind(&device_id)
    .bind(user_id)
    .execute(&state.db)
    .await
    .map_err(db_err)?;

    if result.rows_affected() == 0 {
        return Err((
            StatusCode::NOT_FOUND,
            Json(ErrorBody {
                error: ErrorDetail {
                    code: "NOT_FOUND".into(),
                    message: "Device not found or not owned by user".into(),
                },
            }),
        ));
    }

    tracing::info!(device_id = %device_id, user_id = %user_id, "Device pinged");

    Ok(Json(PingResponse { ok: true }))
}
