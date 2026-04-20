//! Project CRUD endpoints (auth-gated, per-user).

use axum::{extract::{Path, Query, State}, http::StatusCode, routing::get, Json, Router};
use serde::{Deserialize, Serialize};
use uuid::Uuid;
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_projects).post(create_project))
        .route("/:project_id", get(get_project).put(update_project).delete(delete_project))
}

#[derive(Debug, Deserialize)]
pub struct ListParams {
    #[serde(default = "default_limit")]
    pub limit: i64,
    #[serde(default)]
    pub offset: i64,
    pub device_id: Option<String>,
}

fn default_limit() -> i64 { 50 }

#[derive(Debug, Deserialize)]
pub struct CreateProjectRequest {
    pub name: String,
    pub device_id: String,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub ir: Option<serde_json::Value>,
}

#[derive(Debug, Deserialize)]
pub struct UpdateProjectRequest {
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub ir: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct ProjectResponse {
    pub project_id: String,
    pub user_id: String,
    pub device_id: String,
    pub name: String,
    pub description: Option<String>,
    pub ir: Option<serde_json::Value>,
    pub bytecode_hash: Option<String>,
    pub created_at: String,
    pub updated_at: String,
    pub deployed_at: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct ProjectListResponse {
    pub projects: Vec<ProjectResponse>,
    pub total: i64,
    pub limit: i64,
    pub offset: i64,
}

const PROJECT_SELECT: &str = "
    SELECT project_id::text, user_id::text,
           COALESCE(device_id::text, '') AS device_id,
           name, description, ir_json::text, bytecode_hash,
           created_at::text, updated_at::text,
           last_deployed_at::text AS deployed_at
    FROM projects";

fn row_to_response(r: crate::db::models::ProjectRow) -> ProjectResponse {
    ProjectResponse {
        project_id: r.project_id, user_id: r.user_id, device_id: r.device_id,
        name: r.name, description: r.description,
        ir: r.ir_json.and_then(|j| serde_json::from_str(&j).ok()),
        bytecode_hash: r.bytecode_hash, created_at: r.created_at,
        updated_at: r.updated_at, deployed_at: r.deployed_at,
    }
}

async fn list_projects(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Query(params): Query<ListParams>,
) -> Result<Json<ProjectListResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let rows = if let Some(ref device_id) = params.device_id {
        sqlx::query_as::<_, crate::db::models::ProjectRow>(
            &format!("{} WHERE user_id = $1::uuid AND device_id = $2::uuid ORDER BY updated_at DESC LIMIT $3 OFFSET $4", PROJECT_SELECT)
        )
        .bind(&claims.sub).bind(device_id)
        .bind(params.limit).bind(params.offset)
        .fetch_all(&state.db).await
    } else {
        sqlx::query_as::<_, crate::db::models::ProjectRow>(
            &format!("{} WHERE user_id = $1::uuid ORDER BY updated_at DESC LIMIT $2 OFFSET $3", PROJECT_SELECT)
        )
        .bind(&claims.sub)
        .bind(params.limit).bind(params.offset)
        .fetch_all(&state.db).await
    }.map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let total: (i64,) = sqlx::query_as(
        "SELECT COUNT(*) FROM projects WHERE user_id = $1::uuid"
    )
    .bind(&claims.sub)
    .fetch_one(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    Ok(Json(ProjectListResponse {
        projects: rows.into_iter().map(row_to_response).collect(),
        total: total.0, limit: params.limit, offset: params.offset,
    }))
}

async fn create_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<CreateProjectRequest>,
) -> Result<(StatusCode, Json<ProjectResponse>), (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let project_id = Uuid::new_v4().to_string();
    let ir_value: Option<serde_json::Value> = req.ir.clone();

    sqlx::query(
        "INSERT INTO projects (project_id, user_id, device_id, name, description, ir_json)
         VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6::jsonb)"
    )
    .bind(&project_id)
    .bind(&claims.sub)
    .bind(&req.device_id)
    .bind(&req.name)
    .bind(&req.description)
    .bind(&ir_value)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let now = chrono::Utc::now().to_rfc3339();
    Ok((StatusCode::CREATED, Json(ProjectResponse {
        project_id, user_id: claims.sub, device_id: req.device_id,
        name: req.name, description: req.description,
        ir: req.ir, bytecode_hash: None,
        created_at: now.clone(), updated_at: now, deployed_at: None,
    })))
}

async fn get_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(project_id): Path<String>,
) -> Result<Json<ProjectResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let row = sqlx::query_as::<_, crate::db::models::ProjectRow>(
        &format!("{} WHERE project_id = $1::uuid AND user_id = $2::uuid", PROJECT_SELECT)
    )
    .bind(&project_id).bind(&claims.sub)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
        error: ErrorDetail { code: "NOT_FOUND".into(), message: "Project not found".into() },
    })))?;

    Ok(Json(row_to_response(row)))
}

async fn update_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(project_id): Path<String>,
    Json(req): Json<UpdateProjectRequest>,
) -> Result<Json<ProjectResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    // Single update query with all optional fields
    sqlx::query(
        "UPDATE projects SET
            name        = COALESCE($1, name),
            description = COALESCE($2, description),
            ir_json     = COALESCE($3::jsonb, ir_json)
         WHERE project_id = $4::uuid AND user_id = $5::uuid"
    )
    .bind(&req.name)
    .bind(&req.description)
    .bind(&req.ir)
    .bind(&project_id)
    .bind(&claims.sub)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    get_project(State(state), headers, Path(project_id)).await
}

async fn delete_project(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(project_id): Path<String>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    sqlx::query(
        "DELETE FROM projects WHERE project_id = $1::uuid AND user_id = $2::uuid"
    )
    .bind(&project_id).bind(&claims.sub)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    Ok(StatusCode::NO_CONTENT)
}
