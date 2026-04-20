//! Users admin endpoint — list registered users with device/project counts.
//! Requires a valid JWT. No admin-role check yet (future: add role claim gate).

use axum::{extract::State, http::StatusCode, routing::get, Json, Router};
use serde::Serialize;
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/", get(list_users))
}

#[derive(Debug, Serialize)]
pub struct UserRow {
    pub user_id: String,
    pub username: String,
    pub email: Option<String>,
    pub device_count: i64,
    pub project_count: i64,
    pub created_at: String,
    pub last_login_at: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct UsersResponse {
    pub total: i64,
    pub users: Vec<UserRow>,
}

type HandlerError = (StatusCode, Json<ErrorBody>);

fn err(status: StatusCode, code: &str, message: impl Into<String>) -> HandlerError {
    (status, Json(ErrorBody {
        error: ErrorDetail { code: code.into(), message: message.into() },
    }))
}

async fn list_users(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> Result<Json<UsersResponse>, HandlerError> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    if claims.role.as_deref() != Some("admin") {
        return Err(err(StatusCode::FORBIDDEN, "FORBIDDEN", "Admin role required"));
    }

    let rows: Vec<(String, String, Option<String>, i64, i64, String, Option<String>)> =
        sqlx::query_as(
            r#"
            SELECT
                u.user_id::text,
                u.username,
                u.email,
                COUNT(DISTINCT d.device_id) AS device_count,
                COUNT(DISTINCT p.project_id) AS project_count,
                u.created_at::text,
                u.last_login_at::text
            FROM users u
            LEFT JOIN devices d ON d.user_id = u.user_id
            LEFT JOIN projects p ON p.user_id = u.user_id
            GROUP BY u.user_id, u.username, u.email, u.created_at, u.last_login_at
            ORDER BY u.created_at DESC
            LIMIT 500
            "#,
        )
        .fetch_all(&state.db)
        .await
        .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    let total = rows.len() as i64;
    let users = rows
        .into_iter()
        .map(|(user_id, username, email, device_count, project_count, created_at, last_login_at)| {
            UserRow { user_id, username, email, device_count, project_count, created_at, last_login_at }
        })
        .collect();

    Ok(Json(UsersResponse { total, users }))
}
