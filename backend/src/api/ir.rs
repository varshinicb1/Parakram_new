//! IR endpoints — validate, compile, deploy.

use axum::{extract::{Path, State}, http::StatusCode, routing::post, Json, Router};
use serde::{Deserialize, Serialize};
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::api::devices::DEVICE_SELECT;
use crate::ir::types::IRDocument;
use crate::ir::validator;
use crate::compiler;

/// Fire a quota-warning push notification if the user is at ≥ 80 % of a limit.
///
/// Runs in a detached task so it never blocks the HTTP response.
fn maybe_send_quota_warning(state: &AppState, user_id: String) {
    let svc = state.notification_svc.clone();
    let db = state.db.clone();
    tokio::spawn(async move {
        // Fetch current usage and plan limits.
        let tier = match crate::billing::quota::get_plan(&db, &user_id).await {
            Ok(t) => t,
            Err(_) => return,
        };
        let plan = crate::billing::plans::for_tier(tier);
        let usage = match crate::billing::quota::get_or_create_usage(&db, &user_id).await {
            Ok(u) => u,
            Err(_) => return,
        };

        // Compute the highest usage fraction across all metered dimensions.
        let fractions: &[(i32, i32)] = &[
            (usage.llm_intents,    plan.llm_intents_per_month),
            (usage.compiles,       plan.compiles_per_month),
            (usage.deploys,        plan.deploys_per_month),
            (usage.devices_active, plan.max_devices),
        ];
        let max_pct = fractions
            .iter()
            .filter(|(_, limit)| *limit > 0) // skip unlimited (-1)
            .map(|(used, limit)| (*used as f64 / *limit as f64) * 100.0)
            .fold(0.0_f64, f64::max);

        if max_pct < 80.0 {
            return; // nothing to warn about
        }

        // Look up push tokens.
        let fcm_token: Option<String> = sqlx::query_scalar(
            "SELECT token FROM push_tokens WHERE user_id = $1::uuid AND platform = 'android'",
        )
        .bind(&user_id)
        .fetch_optional(&db)
        .await
        .unwrap_or(None);

        let apns_token: Option<String> = sqlx::query_scalar(
            "SELECT token FROM push_tokens WHERE user_id = $1::uuid AND platform = 'ios'",
        )
        .bind(&user_id)
        .fetch_optional(&db)
        .await
        .unwrap_or(None);

        svc.send_quota_warning(&user_id, max_pct, fcm_token.as_deref(), apns_token.as_deref())
            .await;
    });
}

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/validate", post(validate))
        .route("/compile", post(compile))
        .route("/deploy/:device_id", post(deploy))
}

async fn validate(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(ir): Json<IRDocument>,
) -> Result<Json<validator::ValidationResult>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let _claims = validate_token(&token, &state)?;

    let result = validator::validate_ir(&ir, &state.driver_registry);
    Ok(Json(result))
}

#[derive(Debug, Deserialize)]
pub struct CompileRequest {
    pub ir: IRDocument,
    pub device_id: String,
}

async fn compile(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<CompileRequest>,
) -> Result<Json<compiler::CompileResult>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    // Enforce monthly compile quota.
    if let Err(e) = crate::billing::check_quota(
        &state.db, &claims.sub, crate::billing::QuotaKind::Compile,
    ).await {
        let (code, msg) = crate::billing::quota::quota_error_response(e);
        return Err((code, Json(ErrorBody {
            error: ErrorDetail { code: "QUOTA_EXCEEDED".into(), message: msg },
        })));
    }

    let device_id_hash = crc32fast::hash(req.device_id.as_bytes());

    match compiler::compile_ir(&req.ir, device_id_hash, &state.driver_registry, &state.signer) {
        Ok(result) => {
            let _ = crate::billing::increment_usage(
                &state.db, &claims.sub, crate::billing::QuotaKind::Compile,
            ).await;
            crate::metrics::COMPILES_TOTAL.inc();
            maybe_send_quota_warning(&state, claims.sub.clone());
            Ok(Json(result))
        }
        Err(compiler::CompileError::ValidationFailed(validation)) => {
            let msgs: Vec<String> = validation.errors.iter()
                .map(|e| format!("{}: {}", e.field_path, e.message))
                .collect();
            Err((StatusCode::BAD_REQUEST, Json(ErrorBody {
                error: ErrorDetail {
                    code: "VALIDATION_FAILED".into(),
                    message: format!("Validation Failed: {:?}", msgs),
                },
            })))
        }
        Err(e) => {
            Err((StatusCode::UNPROCESSABLE_ENTITY, Json(ErrorBody {
                error: ErrorDetail { code: "COMPILATION_ERROR".into(), message: e.to_string() },
            })))
        }
    }
}

#[derive(Debug, Deserialize)]
pub struct DeployRequest {
    pub bytecode_b64: String,
    pub project_id: String,
    #[serde(default = "default_transfer")]
    pub transfer_method: String,
}

fn default_transfer() -> String { "wifi".into() }

#[derive(Debug, Serialize)]
pub struct DeployResponse {
    pub status: String,
    pub device_id: String,
    pub transfer_method: String,
    pub message: String,
}

async fn deploy(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Path(device_id): Path<String>,
    Json(req): Json<DeployRequest>,
) -> Result<Json<DeployResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let device = sqlx::query_as::<_, crate::db::models::DeviceRow>(
        &format!("{} WHERE device_id = $1::uuid AND user_id = $2::uuid", DEVICE_SELECT)
    )
    .bind(&device_id).bind(&claims.sub)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, Json(ErrorBody {
        error: ErrorDetail { code: "DEVICE_NOT_FOUND".into(), message: "Device not found".into() },
    })))?;

    if device.status == "deploying" {
        return Err((StatusCode::CONFLICT, Json(ErrorBody {
            error: ErrorDetail { code: "DEVICE_BUSY".into(), message: "Device is busy with another deployment".into() },
        })));
    }

    let bytecode_hash = {
        use sha2::{Sha256, Digest};
        use base64::Engine;
        let bytes = base64::engine::general_purpose::STANDARD.decode(&req.bytecode_b64)
            .map_err(|e| (StatusCode::BAD_REQUEST, Json(ErrorBody {
                error: ErrorDetail { code: "INVALID_PAYLOAD".into(), message: e.to_string() },
            })))?;
        let mut hasher = Sha256::new();
        hasher.update(&bytes);
        format!("{:x}", hasher.finalize())
    };

    sqlx::query(
        "INSERT INTO deployments (project_id, device_id, bytecode_hash, transfer_method, status)
         VALUES ($1::uuid, $2::uuid, $3, $4, 'success')"
    )
    .bind(&req.project_id).bind(&device_id)
    .bind(&bytecode_hash).bind(&req.transfer_method)
    .execute(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let _ = sqlx::query(
        "UPDATE projects SET last_deployed_at = NOW(), bytecode_hash = $1
         WHERE project_id = $2::uuid"
    )
    .bind(&bytecode_hash).bind(&req.project_id)
    .execute(&state.db).await;

    let _ = sqlx::query(
        "UPDATE devices SET active_program_id = $1::uuid, last_seen_at = NOW()
         WHERE device_id = $2::uuid"
    )
    .bind(&req.project_id).bind(&device_id)
    .execute(&state.db).await;

    // Fire quota-warning notification if the user is approaching their deploy limit.
    maybe_send_quota_warning(&state, claims.sub.clone());

    // Fire push notification — look up any registered tokens for this user.
    let device_name = device.name.clone();
    {
        let svc = state.notification_svc.clone();
        let db = state.db.clone();
        let user_id = claims.sub.clone();
        tokio::spawn(async move {
            let fcm_token: Option<String> = sqlx::query_scalar(
                "SELECT token FROM push_tokens WHERE user_id = $1::uuid AND platform = 'android'",
            )
            .bind(&user_id)
            .fetch_optional(&db)
            .await
            .unwrap_or(None);

            let apns_token: Option<String> = sqlx::query_scalar(
                "SELECT token FROM push_tokens WHERE user_id = $1::uuid AND platform = 'ios'",
            )
            .bind(&user_id)
            .fetch_optional(&db)
            .await
            .unwrap_or(None);

            svc.send_deploy_complete(
                &device_name,
                fcm_token.as_deref(),
                apns_token.as_deref(),
            )
            .await;
        });
    }

    Ok(Json(DeployResponse {
        status: "deployed".into(),
        device_id,
        transfer_method: req.transfer_method,
        message: "Bytecode payload ready for device transfer".into(),
    }))
}
