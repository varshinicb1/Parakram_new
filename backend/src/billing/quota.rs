//! Quota enforcement — per-user monthly limits.
//!
//! Every metered call (LLM intent, compile, deploy) goes through
//! `check_quota` first, then `increment_usage` after the work succeeds.
//! Usage rolls over at the start of each billing period.

use sqlx::PgPool;
use thiserror::Error;
use chrono::{DateTime, Utc, Datelike, TimeZone};

use crate::billing::plans::{self, PlanTier};
use crate::billing::UsageRow;

#[derive(Debug, Clone, Copy)]
pub enum QuotaKind {
    LlmIntent,
    Compile,
    Deploy,
    Device,
}

#[derive(Debug, Error)]
pub enum QuotaError {
    #[error("quota exceeded for {kind:?}: {used}/{limit} on plan '{plan}'")]
    Exceeded {
        kind: &'static str,
        used: i32,
        limit: i32,
        plan: String,
    },
    #[error("database error: {0}")]
    Db(#[from] sqlx::Error),
}

/// Current monthly period start (UTC, first day of month at 00:00).
fn current_period_start() -> DateTime<Utc> {
    let now = Utc::now();
    Utc.with_ymd_and_hms(now.year(), now.month(), 1, 0, 0, 0).unwrap()
}

/// Fetch the user's plan tier. Defaults to Free if no subscription row exists.
pub async fn get_plan(db: &PgPool, user_id: &str) -> Result<PlanTier, sqlx::Error> {
    let row: Option<(String, String)> = sqlx::query_as(
        "SELECT plan_tier, status FROM subscriptions WHERE user_id::text = $1",
    )
    .bind(user_id)
    .fetch_optional(db)
    .await?;

    Ok(match row {
        Some((tier, status)) if status == "active" || status == "trialing" => {
            PlanTier::from_str(&tier)
        }
        _ => PlanTier::Free,
    })
}

/// Fetch (or lazily create) the current period's usage row.
pub async fn get_or_create_usage(db: &PgPool, user_id: &str) -> Result<UsageRow, sqlx::Error> {
    let period = current_period_start();

    sqlx::query(
        "INSERT INTO usage_counters (user_id, period_start, llm_intents, compiles, deploys, devices_active, updated_at)
         VALUES ($1, $2, 0, 0, 0, 0, NOW())
         ON CONFLICT (user_id, period_start) DO NOTHING",
    )
    .bind(user_id)
    .bind(period)
    .execute(db)
    .await?;

    sqlx::query_as::<_, UsageRow>(
        "SELECT user_id::text as user_id, period_start, llm_intents, compiles, deploys, devices_active, updated_at
         FROM usage_counters
         WHERE user_id::text = $1 AND period_start = $2",
    )
    .bind(user_id)
    .bind(period)
    .fetch_one(db)
    .await
}

/// Guard: fail fast if the user is already at their plan's limit.
/// Call this BEFORE doing expensive work.
pub async fn check_quota(
    db: &PgPool,
    user_id: &str,
    kind: QuotaKind,
) -> Result<(), QuotaError> {
    let tier = get_plan(db, user_id).await?;
    let plan = plans::for_tier(tier);
    let usage = get_or_create_usage(db, user_id).await?;

    let (used, limit, label) = match kind {
        QuotaKind::LlmIntent => (usage.llm_intents,    plan.llm_intents_per_month, "llm_intents"),
        QuotaKind::Compile   => (usage.compiles,       plan.compiles_per_month,    "compiles"),
        QuotaKind::Deploy    => (usage.deploys,        plan.deploys_per_month,     "deploys"),
        QuotaKind::Device    => (usage.devices_active, plan.max_devices,           "devices"),
    };

    // -1 means unlimited (Enterprise)
    if limit < 0 {
        return Ok(());
    }
    if used >= limit {
        return Err(QuotaError::Exceeded {
            kind: label,
            used,
            limit,
            plan: plan.tier.as_str().to_string(),
        });
    }
    Ok(())
}

/// Record that a metered call was consumed. Call AFTER the work succeeds
/// so we don't bill users for failures.
pub async fn increment_usage(
    db: &PgPool,
    user_id: &str,
    kind: QuotaKind,
) -> Result<(), sqlx::Error> {
    let period = current_period_start();
    let column = match kind {
        QuotaKind::LlmIntent => "llm_intents",
        QuotaKind::Compile   => "compiles",
        QuotaKind::Deploy    => "deploys",
        QuotaKind::Device    => "devices_active",
    };

    let sql = format!(
        "UPDATE usage_counters
         SET {col} = {col} + 1, updated_at = NOW()
         WHERE user_id::text = $1 AND period_start = $2",
        col = column
    );
    sqlx::query(&sql)
        .bind(user_id)
        .bind(period)
        .execute(db)
        .await?;
    Ok(())
}

/// Map a QuotaError into an Axum-compatible (StatusCode, message) pair.
pub fn quota_error_response(e: QuotaError) -> (axum::http::StatusCode, String) {
    match e {
        QuotaError::Exceeded { .. } => (axum::http::StatusCode::PAYMENT_REQUIRED, e.to_string()),
        QuotaError::Db(_)           => (axum::http::StatusCode::INTERNAL_SERVER_ERROR, e.to_string()),
    }
}
