//! Billing API — plan catalog, checkout, portal, subscription status, webhook.

use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
    routing::{get, post},
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::billing::{plans, quota, stripe as stripe_api, webhook as webhook_mod};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/plans",         get(list_plans))
        .route("/me",            get(get_my_subscription))
        .route("/usage",         get(get_my_usage))
        .route("/checkout",      post(create_checkout))
        .route("/portal",        post(create_portal))
        .route("/webhook",       post(stripe_webhook))
}

/// GET /api/billing/plans — public, used by marketing site & pricing page.
async fn list_plans() -> Json<Vec<plans::Plan>> {
    Json(plans::catalog())
}

/// GET /api/billing/me — current user's subscription state.
async fn get_my_subscription(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<SubscriptionView>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let tier = quota::get_plan(&state.db, &claims.sub).await.map_err(db_err)?;
    let plan = plans::for_tier(tier);

    // Fetch the full row if it exists
    let row: Option<crate::billing::SubscriptionRow> = sqlx::query_as(
        "SELECT user_id, stripe_customer_id, stripe_subscription_id,
                plan_tier, status, current_period_start, current_period_end,
                cancel_at_period_end, created_at, updated_at
         FROM subscriptions WHERE user_id = $1",
    )
    .bind(&claims.sub)
    .fetch_optional(&state.db)
    .await
    .map_err(db_err)?;

    Ok(Json(SubscriptionView {
        tier: plan.tier.as_str().to_string(),
        display_name: plan.display_name.to_string(),
        monthly_price_usd: plan.monthly_price_usd,
        status: row.as_ref().map(|r| r.status.clone()).unwrap_or_else(|| "active".into()),
        current_period_end: row.as_ref().and_then(|r| r.current_period_end),
        cancel_at_period_end: row.as_ref().map(|r| r.cancel_at_period_end).unwrap_or(false),
    }))
}

/// GET /api/billing/usage — current billing period usage + limits.
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

/// POST /api/billing/checkout — create a Stripe Checkout session.
async fn create_checkout(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<CheckoutRequest>,
) -> Result<Json<CheckoutResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let email = claims.email.clone().unwrap_or_default();

    let tier = plans::PlanTier::from_str(&req.tier);
    let price_id = tier.stripe_price_id().ok_or_else(|| upstream_err(
        "INVALID_TIER", &format!("no Stripe price ID configured for tier '{}'", req.tier)
    ))?;

    // Lazily create/fetch Stripe customer
    let customer_id = match get_customer_id(&state, &claims.sub).await.map_err(db_err)? {
        Some(id) => id,
        None => {
            let id = stripe_api::ensure_customer(&claims.sub, &email).await
                .map_err(|e| upstream_err("STRIPE_ERROR", &e.to_string()))?;
            sqlx::query(
                "INSERT INTO subscriptions (user_id, stripe_customer_id, plan_tier, status, created_at, updated_at)
                 VALUES ($1, $2, 'free', 'active', NOW(), NOW())
                 ON CONFLICT (user_id) DO UPDATE SET stripe_customer_id = $2, updated_at = NOW()"
            )
            .bind(&claims.sub)
            .bind(&id)
            .execute(&state.db)
            .await
            .map_err(db_err)?;
            id
        }
    };

    let session = stripe_api::create_checkout_session(
        &customer_id,
        &price_id,
        &req.success_url,
        &req.cancel_url,
    )
    .await
    .map_err(|e| upstream_err("STRIPE_ERROR", &e.to_string()))?;

    Ok(Json(CheckoutResponse {
        session_id: session.id,
        url: session.url.unwrap_or_default(),
    }))
}

/// POST /api/billing/portal — create a Stripe Billing Portal session.
async fn create_portal(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(req): Json<PortalRequest>,
) -> Result<Json<PortalResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    let customer_id = get_customer_id(&state, &claims.sub).await.map_err(db_err)?
        .ok_or_else(|| upstream_err("NO_CUSTOMER", "no Stripe customer for user"))?;

    let url = stripe_api::create_portal_session(&customer_id, &req.return_url).await
        .map_err(|e| upstream_err("STRIPE_ERROR", &e.to_string()))?;
    Ok(Json(PortalResponse { url }))
}

/// POST /api/billing/webhook — Stripe webhook receiver.
/// NOTE: this route is auth-less (Stripe signs the body); signature is verified instead.
async fn stripe_webhook(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: axum::body::Bytes,
) -> impl IntoResponse {
    let signature = match headers.get("Stripe-Signature").and_then(|v| v.to_str().ok()) {
        Some(s) => s,
        None => return (StatusCode::BAD_REQUEST, "missing signature").into_response(),
    };

    let secret = match std::env::var("STRIPE_WEBHOOK_SECRET") {
        Ok(s) => s,
        Err(_) => return (StatusCode::INTERNAL_SERVER_ERROR, "STRIPE_WEBHOOK_SECRET not set").into_response(),
    };

    if let Err(e) = webhook_mod::verify_signature(&body, signature, &secret) {
        tracing::warn!("webhook signature rejected: {}", e);
        return (StatusCode::UNAUTHORIZED, "bad signature").into_response();
    }

    let event: webhook_mod::StripeEvent = match serde_json::from_slice(&body) {
        Ok(e) => e,
        Err(e) => return (StatusCode::BAD_REQUEST, format!("bad JSON: {}", e)).into_response(),
    };

    match webhook_mod::handle_event(&state.db, event).await {
        Ok(()) => (StatusCode::OK, "ok").into_response(),
        Err(e) => {
            tracing::error!("webhook handler error: {}", e);
            (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()).into_response()
        }
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

async fn get_customer_id(state: &AppState, user_id: &str) -> Result<Option<String>, sqlx::Error> {
    sqlx::query_scalar(
        "SELECT stripe_customer_id FROM subscriptions WHERE user_id = $1",
    )
    .bind(user_id)
    .fetch_optional(&state.db)
    .await
    .map(|opt: Option<Option<String>>| opt.flatten())
}

fn auth(
    state: &AppState,
    headers: &HeaderMap,
) -> Result<crate::api::auth::Claims, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(headers)?;
    validate_token(&token, state)
}

fn db_err(e: sqlx::Error) -> (StatusCode, Json<ErrorBody>) {
    (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    }))
}

fn upstream_err(code: &str, msg: &str) -> (StatusCode, Json<ErrorBody>) {
    (StatusCode::BAD_GATEWAY, Json(ErrorBody {
        error: ErrorDetail { code: code.into(), message: msg.into() },
    }))
}

/* ── DTOs ────────────────────────────────────────────────────────────────── */

#[derive(Debug, Serialize)]
struct SubscriptionView {
    tier: String,
    display_name: String,
    monthly_price_usd: f32,
    status: String,
    current_period_end: Option<chrono::DateTime<chrono::Utc>>,
    cancel_at_period_end: bool,
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
struct CheckoutRequest {
    tier: String,
    success_url: String,
    cancel_url: String,
}

#[derive(Debug, Serialize)]
struct CheckoutResponse {
    session_id: String,
    url: String,
}

#[derive(Debug, Deserialize)]
struct PortalRequest {
    return_url: String,
}

#[derive(Debug, Serialize)]
struct PortalResponse {
    url: String,
}
