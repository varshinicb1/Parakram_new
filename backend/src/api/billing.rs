//! UPI Billing API — zero-cost payment processing for India.
//!
//! Routes (mounted under /api/billing):
//!   GET  /plans         — plan catalog (public)
//!   GET  /upi-link      — generate UPI payment deep link (auth)
//!   POST /claim         — submit UTR after payment (auth)
//!   GET  /me            — current subscription status (auth)
//!   GET  /usage         — current usage counters (auth)
//!   GET  /admin/pending — list pending payment claims (admin)
//!   POST /admin/approve — approve a payment claim (admin)
//!   POST /admin/reject  — reject a payment claim (admin)

use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::{get, post},
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::billing::{self, plans, quota};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/plans",          get(list_plans))
        .route("/upi-link",      get(get_upi_link))
        .route("/claim",         post(submit_payment_claim))
        .route("/me",            get(get_my_subscription))
        .route("/usage",         get(get_my_usage))
        .route("/admin/pending", get(admin_list_pending))
        .route("/admin/approve", post(admin_approve_claim))
        .route("/admin/reject",  post(admin_reject_claim))
}

// ── Public ──────────────────────────────────────────────────

async fn list_plans() -> Json<Vec<plans::Plan>> {
    Json(plans::catalog())
}

// ── Authenticated ───────────────────────────────────────────

async fn get_upi_link(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<UpiLinkResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let link = billing::upi_payment_link(&claims.sub);
    let qr = billing::upi_qr_data(&claims.sub);

    Ok(Json(UpiLinkResponse {
        upi_link: link,
        qr_data: qr,
        amount_inr: billing::UPI_AMOUNT_INR,
        payee_name: billing::UPI_PAYEE_NAME.to_string(),
        upi_id: billing::UPI_VPA.to_string(),
        instructions: "1. Pay ₹125 via any UPI app (GPay/PhonePe/Paytm)\n\
                        2. Copy the UTR (transaction reference) from your payment app\n\
                        3. Submit the UTR below to activate Maker tier".to_string(),
    }))
}

async fn submit_payment_claim(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<ClaimRequest>,
) -> Result<Json<ClaimResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;

    if req.upi_utr.trim().len() < 8 {
        return Err((StatusCode::BAD_REQUEST, Json(ErrorBody {
            error: ErrorDetail {
                code: "INVALID_UTR".into(),
                message: "UTR must be at least 8 characters".into(),
            },
        })));
    }

    // Check for duplicate UTR
    let existing: Option<(i64,)> = sqlx::query_as(
        "SELECT id FROM payment_claims WHERE upi_utr = $1"
    )
    .bind(&req.upi_utr)
    .fetch_optional(&state.db)
    .await
    .map_err(db_err)?;

    if existing.is_some() {
        return Err((StatusCode::CONFLICT, Json(ErrorBody {
            error: ErrorDetail {
                code: "DUPLICATE_UTR".into(),
                message: "This UTR has already been submitted".into(),
            },
        })));
    }

    sqlx::query(
        "INSERT INTO payment_claims (user_id, upi_utr, amount_inr, status, submitted_at)
         VALUES ($1, $2, $3, 'pending', NOW())"
    )
    .bind(&claims.sub)
    .bind(&req.upi_utr)
    .bind(billing::UPI_AMOUNT_INR as i32)
    .execute(&state.db)
    .await
    .map_err(db_err)?;

    tracing::info!(
        user = %claims.sub,
        utr = %req.upi_utr,
        "Payment claim submitted — pending admin verification"
    );

    Ok(Json(ClaimResponse {
        status: "pending".into(),
        message: "Payment claim submitted! Your Maker tier will be activated within 24 hours after verification.".into(),
    }))
}

async fn get_my_subscription(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<SubscriptionView>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let tier = quota::get_plan(&state.db, &claims.sub).await.map_err(db_err)?;
    let plan = plans::for_tier(tier);

    Ok(Json(SubscriptionView {
        tier: plan.tier.as_str().to_string(),
        display_name: plan.display_name.to_string(),
        monthly_price_inr: billing::UPI_AMOUNT_INR,
        max_projects: plan.max_projects,
        max_devices: plan.max_devices,
    }))
}

async fn get_my_usage(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<UsageView>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let tier = quota::get_plan(&state.db, &claims.sub).await.map_err(db_err)?;
    let plan = plans::for_tier(tier);
    let usage = quota::get_or_create_usage(&state.db, &claims.sub).await.map_err(db_err)?;

    Ok(Json(UsageView {
        period_start: usage.period_start,
        llm_intents:    Counter { used: usage.llm_intents,    limit: plan.llm_intents_per_month },
        compiles:       Counter { used: usage.compiles,       limit: plan.compiles_per_month },
        deploys:        Counter { used: usage.deploys,        limit: plan.deploys_per_month },
        devices_active: Counter { used: usage.devices_active, limit: plan.max_devices },
    }))
}

// ── Admin ───────────────────────────────────────────────────

async fn admin_list_pending(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<Vec<billing::PaymentClaim>>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_admin(&claims)?;

    let rows: Vec<billing::PaymentClaim> = sqlx::query_as(
        "SELECT id, user_id, upi_utr, amount_inr, status, submitted_at, reviewed_at, reviewed_by
         FROM payment_claims WHERE status = 'pending' ORDER BY submitted_at ASC"
    )
    .fetch_all(&state.db)
    .await
    .map_err(db_err)?;

    Ok(Json(rows))
}

async fn admin_approve_claim(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<AdminClaimAction>,
) -> Result<Json<ClaimResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_admin(&claims)?;

    // Get the claim to find the user
    let claim: Option<(String,)> = sqlx::query_as(
        "SELECT user_id FROM payment_claims WHERE id = $1 AND status = 'pending'"
    )
    .bind(req.claim_id)
    .fetch_optional(&state.db)
    .await
    .map_err(db_err)?;

    let user_id = match claim {
        Some((uid,)) => uid,
        None => return Err((StatusCode::NOT_FOUND, Json(ErrorBody {
            error: ErrorDetail {
                code: "NOT_FOUND".into(),
                message: "Claim not found or already processed".into(),
            },
        }))),
    };

    // Approve the claim
    sqlx::query(
        "UPDATE payment_claims SET status = 'approved', reviewed_at = NOW(), reviewed_by = $1
         WHERE id = $2"
    )
    .bind(&claims.sub)
    .bind(req.claim_id)
    .execute(&state.db)
    .await
    .map_err(db_err)?;

    // Upgrade user to Maker tier (30 days from now)
    sqlx::query(
        "INSERT INTO subscriptions (user_id, plan_tier, status, payment_verified, created_at, updated_at, expires_at)
         VALUES ($1, 'maker', 'active', true, NOW(), NOW(), NOW() + INTERVAL '30 days')
         ON CONFLICT (user_id) DO UPDATE SET
            plan_tier = 'maker', status = 'active', payment_verified = true,
            updated_at = NOW(), expires_at = NOW() + INTERVAL '30 days'"
    )
    .bind(&user_id)
    .execute(&state.db)
    .await
    .map_err(db_err)?;

    tracing::info!(claim_id = req.claim_id, user = %user_id, admin = %claims.sub, "Payment approved — Maker tier activated");

    Ok(Json(ClaimResponse {
        status: "approved".into(),
        message: format!("User {} upgraded to Maker tier for 30 days", user_id),
    }))
}

async fn admin_reject_claim(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<AdminClaimAction>,
) -> Result<Json<ClaimResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_admin(&claims)?;

    sqlx::query(
        "UPDATE payment_claims SET status = 'rejected', reviewed_at = NOW(), reviewed_by = $1
         WHERE id = $2 AND status = 'pending'"
    )
    .bind(&claims.sub)
    .bind(req.claim_id)
    .execute(&state.db)
    .await
    .map_err(db_err)?;

    Ok(Json(ClaimResponse {
        status: "rejected".into(),
        message: "Payment claim rejected".into(),
    }))
}

// ── Helpers ─────────────────────────────────────────────────

fn auth(
    state: &AppState,
    headers: &HeaderMap,
) -> Result<crate::api::auth::Claims, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(headers)?;
    validate_token(&token, state)
}

fn require_admin(claims: &crate::api::auth::Claims) -> Result<(), (StatusCode, Json<ErrorBody>)> {
    if claims.role.as_deref() == Some("admin") {
        Ok(())
    } else {
        Err((StatusCode::FORBIDDEN, Json(ErrorBody {
            error: ErrorDetail { code: "FORBIDDEN".into(), message: "admin role required".into() },
        })))
    }
}

fn db_err(e: sqlx::Error) -> (StatusCode, Json<ErrorBody>) {
    tracing::error!("billing db error: {}", e);
    (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    }))
}

// ── DTOs ────────────────────────────────────────────────────

#[derive(Debug, Serialize)]
struct UpiLinkResponse {
    upi_link: String,
    qr_data: String,
    amount_inr: u32,
    payee_name: String,
    upi_id: String,
    instructions: String,
}

#[derive(Debug, Deserialize)]
struct ClaimRequest {
    upi_utr: String,
}

#[derive(Debug, Serialize)]
struct ClaimResponse {
    status: String,
    message: String,
}

#[derive(Debug, Serialize)]
struct SubscriptionView {
    tier: String,
    display_name: String,
    monthly_price_inr: u32,
    max_projects: i32,
    max_devices: i32,
}

#[derive(Debug, Serialize)]
struct UsageView {
    period_start: chrono::DateTime<chrono::Utc>,
    llm_intents:    Counter,
    compiles:       Counter,
    deploys:        Counter,
    devices_active: Counter,
}

#[derive(Debug, Serialize)]
struct Counter {
    used: i32,
    limit: i32,
}

#[derive(Debug, Deserialize)]
struct AdminClaimAction {
    claim_id: i64,
}
