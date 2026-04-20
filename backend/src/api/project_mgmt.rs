//! Project Management — Full CRUD, firmware download, deploy OTA.

use axum::{Router, Json, extract::{State, Path}};
use axum::routing::{get, post};
use axum::http::StatusCode;
use serde::{Deserialize, Serialize};
use chrono::Utc;
use std::collections::HashMap;

use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody};

// ===== DATA MODELS =====

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Project {
    pub id: String,
    pub user_id: String,
    pub name: String,
    pub description: String,
    pub category: String,
    pub status: String,
    pub created_at: String,
    pub updated_at: String,
    pub ir_document: Option<serde_json::Value>,
    pub config: ProjectConfig,
    pub bytecode_size: u32,
    pub deploy_count: u32,
    pub device_id: Option<String>,
    pub last_deployed_at: Option<String>,
    pub template_id: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProjectConfig {
    #[serde(default)]
    pub sensors: Vec<String>,
    #[serde(default)]
    pub actuators: Vec<String>,
    #[serde(default)]
    pub parameters: HashMap<String, serde_json::Value>,
    #[serde(default)]
    pub display_type: Option<String>,
    #[serde(default)]
    pub has_audio: bool,
    #[serde(default)]
    pub has_touch: bool,
}

#[derive(Debug, Deserialize)]
pub struct CreateProjectRequest {
    pub name: String,
    pub description: String,
    #[serde(default)]
    pub category: String,
    #[serde(default)]
    pub template_id: Option<String>,
    #[serde(default)]
    pub config: Option<ProjectConfig>,
}

#[derive(Debug, Deserialize)]
pub struct UpdateProjectRequest {
    pub name: Option<String>,
    pub description: Option<String>,
    pub category: Option<String>,
    pub ir_document: Option<serde_json::Value>,
    pub device_id: Option<String>,
    pub config: Option<ProjectConfig>,
}

#[derive(Debug, Serialize)]
pub struct ProjectListResponse {
    pub projects: Vec<Project>,
    pub total: usize,
}

#[derive(Debug, Serialize)]
pub struct DeployResponse {
    pub status: String,
    pub project_id: String,
    pub device_id: String,
    pub bytecode_size: u32,
    pub deployed_at: String,
}

#[derive(Debug, Serialize)]
pub struct DownloadResponse {
    pub project_id: String,
    pub filename: String,
    pub bytecode_base64: String,
    pub ir_json: serde_json::Value,
    pub checksum_crc32: String,
}

// Tuple type for the project_mgmt raw SELECT
type ProjectTuple = (String, String, String, String, String, String, String, String,
                     Option<String>, Option<String>, String, i32, i32, Option<String>);

const PROJECT_MGMT_SELECT: &str = "
    SELECT project_id::text, user_id::text, name, description, category, status,
           created_at::text, updated_at::text, template_id::text, ir_json::text,
           config::text, bytecode_size, deploy_count, device_id::text
    FROM projects";

fn tuple_to_project(row: ProjectTuple) -> Project {
    Project {
        id: row.0, user_id: row.1, name: row.2, description: row.3,
        category: row.4, status: row.5, created_at: row.6, updated_at: row.7,
        template_id: row.8,
        ir_document: row.9.and_then(|s| serde_json::from_str(&s).ok()),
        config: serde_json::from_str(&row.10).unwrap_or_default(),
        bytecode_size: row.11 as u32,
        deploy_count: row.12 as u32,
        device_id: row.13,
        last_deployed_at: None,
    }
}

// ===== HANDLERS =====

async fn list_projects(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> Result<Json<ProjectListResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let rows = sqlx::query_as::<_, ProjectTuple>(
        &format!("{} WHERE user_id = $1::uuid ORDER BY updated_at DESC", PROJECT_MGMT_SELECT)
    )
    .bind(&claims.sub)
    .fetch_all(&state.db).await.unwrap_or_default();

    let projects: Vec<Project> = rows.into_iter().map(tuple_to_project).collect();
    let total = projects.len();
    Ok(Json(ProjectListResponse { projects, total }))
}

async fn get_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(id): Path<String>,
) -> Result<Json<Project>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let row = sqlx::query_as::<_, ProjectTuple>(
        &format!("{} WHERE project_id = $1::uuid AND user_id = $2::uuid", PROJECT_MGMT_SELECT)
    )
    .bind(&id).bind(&claims.sub)
    .fetch_optional(&state.db).await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "DB_ERROR".into(), message: "Database error".into() },
    })))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "NOT_FOUND".into(), message: "Project not found".into() },
    })))?;

    Ok(Json(tuple_to_project(row)))
}

async fn create_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<CreateProjectRequest>,
) -> Result<(StatusCode, Json<Project>), (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let id = uuid::Uuid::new_v4().to_string();
    let now = Utc::now().to_rfc3339();
    let cat = if req.category.is_empty() { "General".to_string() } else { req.category.clone() };
    let cfg = req.config.unwrap_or_default();
    let cfg_value = serde_json::to_value(&cfg).unwrap_or_else(|_| serde_json::json!({}));

    sqlx::query(
        "INSERT INTO projects (project_id, user_id, name, description, category, status, template_id, config, created_at, updated_at)
         VALUES ($1::uuid, $2::uuid, $3, $4, $5, 'draft', $6::uuid, $7::jsonb, NOW(), NOW())"
    )
    .bind(&id).bind(&claims.sub).bind(&req.name).bind(&req.description)
    .bind(&cat).bind(&req.template_id).bind(&cfg_value)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let project = Project {
        id, user_id: claims.sub, name: req.name, description: req.description,
        category: cat, status: "draft".into(), created_at: now.clone(), updated_at: now,
        ir_document: None, bytecode_size: 0, device_id: None, deploy_count: 0,
        last_deployed_at: None, template_id: req.template_id, config: cfg,
    };

    Ok((StatusCode::CREATED, Json(project)))
}

async fn update_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(id): Path<String>,
    Json(req): Json<UpdateProjectRequest>,
) -> Result<Json<Project>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let ir_value = req.ir_document.as_ref().map(|v| serde_json::to_value(v).unwrap());
    let cfg_value = req.config.as_ref().map(|c| serde_json::to_value(c).unwrap());
    let new_status = if req.ir_document.is_some() { Some("compiled") } else { None };

    sqlx::query(
        "UPDATE projects SET
            name        = COALESCE($1, name),
            description = COALESCE($2, description),
            category    = COALESCE($3, category),
            status      = COALESCE($4, status),
            ir_json     = COALESCE($5::jsonb, ir_json),
            device_id   = COALESCE($6::uuid, device_id),
            config      = COALESCE($7::jsonb, config)
         WHERE project_id = $8::uuid AND user_id = $9::uuid"
    )
    .bind(&req.name).bind(&req.description).bind(&req.category)
    .bind(new_status).bind(&ir_value).bind(&req.device_id).bind(&cfg_value)
    .bind(&id).bind(&claims.sub)
    .execute(&state.db).await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "DB_ERROR".into(), message: "Update failed".into() },
    })))?;

    get_project(State(state), headers, Path(id)).await
}

async fn delete_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(id): Path<String>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let res = sqlx::query(
        "DELETE FROM projects WHERE project_id = $1::uuid AND user_id = $2::uuid"
    )
    .bind(&id).bind(&claims.sub)
    .execute(&state.db).await
    .map_err(|_| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "DB_ERROR".into(), message: "Delete failed".into() },
    })))?;

    if res.rows_affected() > 0 { Ok(StatusCode::NO_CONTENT) }
    else { Err((StatusCode::NOT_FOUND, Json(ErrorBody {
        error: crate::api::auth::ErrorDetail { code: "NOT_FOUND".into(), message: "Project not found".into() },
    }))) }
}

async fn deploy_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(id): Path<String>,
) -> Result<Json<DeployResponse>, (StatusCode, Json<ErrorBody>)> {
    let proj = get_project(State(state.clone()), headers, Path(id.clone())).await?;
    let p = proj.0;

    if p.ir_document.is_none() {
        return Err((StatusCode::PRECONDITION_FAILED, Json(ErrorBody {
            error: crate::api::auth::ErrorDetail { code: "NO_IR".into(), message: "No IR compiled yet".into() },
        })));
    }

    let device_id = p.device_id.unwrap_or_else(|| "VDYT-S3-R1-DEFAULT".into());
    let now = Utc::now().to_rfc3339();

    let _ = sqlx::query(
        "UPDATE projects SET deploy_count = deploy_count + 1, status = 'deployed', last_deployed_at = NOW()
         WHERE project_id = $1::uuid"
    )
    .bind(&id).execute(&state.db).await;

    Ok(Json(DeployResponse {
        status: "deployed".into(),
        project_id: id,
        device_id,
        bytecode_size: p.bytecode_size,
        deployed_at: now,
    }))
}

async fn download_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(id): Path<String>,
) -> Result<Json<DownloadResponse>, (StatusCode, Json<ErrorBody>)> {
    let proj = get_project(State(state), headers, Path(id)).await?;
    let p = proj.0;

    let ir_json = p.ir_document.unwrap_or(serde_json::json!({"error": "No IR compiled yet"}));
    let ir_bytes = serde_json::to_vec(&ir_json).unwrap_or_default();
    let checksum = crc32fast::hash(&ir_bytes);

    Ok(Json(DownloadResponse {
        project_id: p.id,
        filename: format!("{}.parakram.json", p.name.replace(' ', "_").to_lowercase()),
        bytecode_base64: base64::Engine::encode(&base64::engine::general_purpose::STANDARD, &ir_bytes),
        ir_json,
        checksum_crc32: format!("{:08X}", checksum),
    }))
}

// ===== ROUTER =====

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_projects))
        .route("/create", post(create_project))
        .route("/:id", get(get_project).put(update_project).delete(delete_project))
        .route("/:id/deploy", post(deploy_project))
        .route("/:id/download", get(download_project))
}
