//! System endpoints — health, config, driver registry, board registry.

use axum::{extract::State, http::StatusCode, routing::get, Json, Router, extract::Path};
use serde::Serialize;
use crate::AppState;
use crate::api::auth::{ErrorBody, ErrorDetail};

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/health",  get(health_check))
        .route("/ready",   get(readiness_check))
        .route("/metrics", get(metrics_endpoint))
        .route("/config",  get(get_config))
}

/// Prometheus text-exposition format — scraped by Prometheus / Grafana Agent.
async fn metrics_endpoint() -> ([(axum::http::HeaderName, &'static str); 1], String) {
    (
        [(axum::http::header::CONTENT_TYPE, "text/plain; version=0.0.4")],
        crate::metrics::render(),
    )
}

/// Kubernetes-style readiness probe. 200 only when DB is reachable.
async fn readiness_check(State(state): State<AppState>) -> (StatusCode, &'static str) {
    match sqlx::query("SELECT 1").execute(&state.db).await {
        Ok(_)  => (StatusCode::OK,                  "ready"),
        Err(_) => (StatusCode::SERVICE_UNAVAILABLE, "db unavailable"),
    }
}

pub fn drivers_router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_drivers))
        .route("/{driver_name}", get(get_driver))
}

pub fn boards_router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_boards))
        .route("/{board_id}", get(get_board))
}

#[derive(Debug, Serialize)]
pub struct HealthResponse {
    pub status: String,
    pub version: String,
    pub database: String,
    pub llm_available: bool,
    pub registered_drivers: usize,
}

async fn health_check(State(state): State<AppState>) -> Json<HealthResponse> {
    let db_ok = sqlx::query("SELECT 1").execute(&state.db).await.is_ok();

    Json(HealthResponse {
        status: if db_ok { "healthy".into() } else { "degraded".into() },
        version: "1.0.0".into(),
        database: if db_ok { "connected".into() } else { "disconnected".into() },
        llm_available: state.llm_api_key != "not-configured",
        registered_drivers: state.driver_registry.count(),
    })
}

#[derive(Debug, Serialize)]
pub struct SystemConfig {
    pub max_projects_per_user: u32,
    pub max_devices_per_user: u32,
    pub llm_model: String,
    pub llm_rate_limit_per_minute: u32,
    pub ir_schema_version: String,
    pub bytecode_version: u16,
}

async fn get_config(State(state): State<AppState>) -> Json<SystemConfig> {
    Json(SystemConfig {
        max_projects_per_user: 100,
        max_devices_per_user: 10,
        llm_model: state.llm_model.clone(),
        llm_rate_limit_per_minute: 10,
        ir_schema_version: "1.0".into(),
        bytecode_version: 1,
    })
}

async fn list_drivers(
    State(state): State<AppState>,
) -> Json<serde_json::Value> {
    let drivers: Vec<&crate::drivers::registry::DriverSpec> = state.driver_registry.list_all();
    let drivers_json: Vec<serde_json::Value> = drivers.iter().map(|d| serde_json::to_value(d).unwrap()).collect();
    Json(serde_json::json!({ "drivers": drivers_json, "total": drivers_json.len() }))
}

async fn get_driver(
    State(state): State<AppState>,
    Path(driver_name): Path<String>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<ErrorBody>)> {
    let driver = state.driver_registry.get_driver(&driver_name)
        .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
            error: ErrorDetail { code: "NOT_FOUND".into(), message: format!("Driver '{}' not found", driver_name) },
        })))?;

    Ok(Json(serde_json::to_value(driver).unwrap()))
}

async fn list_boards(
    State(state): State<AppState>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<ErrorBody>)> {
    let rows = sqlx::query_as::<_, (String, String)>(
        "SELECT sku, name FROM board_skus ORDER BY sku"
    )
    .fetch_all(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let boards: Vec<serde_json::Value> = rows.iter()
        .map(|(sku, name)| serde_json::json!({ "sku": sku, "name": name }))
        .collect();

    Ok(Json(serde_json::json!({ "boards": boards })))
}

async fn get_board(
    State(state): State<AppState>,
    Path(board_id): Path<String>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<ErrorBody>)> {
    let row = sqlx::query_as::<_, (String, String, String, i32, i32, serde_json::Value, serde_json::Value)>(
        "SELECT sku, name, soc, flash_mb, psram_mb, pin_map, default_devices FROM board_skus WHERE sku = $1"
    )
    .bind(&board_id)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
        error: ErrorDetail { code: "NOT_FOUND".into(), message: format!("Board '{}' not found", board_id) },
    })))?;

    Ok(Json(serde_json::json!({
        "sku": row.0, "name": row.1, "soc": row.2,
        "flash_mb": row.3, "psram_mb": row.4,
        "pin_map": row.5,
        "default_devices": row.6,
    })))
}
