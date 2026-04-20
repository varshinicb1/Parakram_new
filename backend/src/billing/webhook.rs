//! Stripe webhook signature verification + event handling.
//!
//! Stripe signs every webhook with a timestamp-based HMAC-SHA256 scheme:
//!
//!     Stripe-Signature: t=<unix>,v1=<hex-sha256>,v0=<legacy>
//!
//! The signed payload is `<t>.<raw-body>`. We verify v1 against
//! STRIPE_WEBHOOK_SECRET (starts with `whsec_`).

use anyhow::{Result, anyhow};
use ring::hmac;
use serde::Deserialize;
use sqlx::PgPool;
use chrono::{DateTime, Utc, TimeZone};

use crate::billing::stripe::{get_subscription, tier_for_price};

/// Verify a Stripe webhook signature. Rejects if timestamp is >5 min old.
pub fn verify_signature(raw_body: &[u8], signature_header: &str, secret: &str) -> Result<()> {
    let mut t: Option<i64> = None;
    let mut v1: Option<String> = None;

    for part in signature_header.split(',') {
        if let Some(rest) = part.strip_prefix("t=") {
            t = rest.parse().ok();
        } else if let Some(rest) = part.strip_prefix("v1=") {
            v1 = Some(rest.to_string());
        }
    }

    let t = t.ok_or_else(|| anyhow!("no t= in signature"))?;
    let v1 = v1.ok_or_else(|| anyhow!("no v1= in signature"))?;

    let now = chrono::Utc::now().timestamp();
    if (now - t).abs() > 300 {
        return Err(anyhow!("signature timestamp drift > 5 min"));
    }

    let signed_payload = format!("{}.{}", t, std::str::from_utf8(raw_body)?);
    let key = hmac::Key::new(hmac::HMAC_SHA256, secret.as_bytes());
    let tag = hmac::sign(&key, signed_payload.as_bytes());
    let expected = hex::encode(tag.as_ref());

    // Constant-time compare
    if !constant_time_eq(expected.as_bytes(), v1.as_bytes()) {
        return Err(anyhow!("signature mismatch"));
    }
    Ok(())
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() { return false; }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

/// Stripe event envelope (minimal fields we need).
#[derive(Debug, Clone, Deserialize)]
pub struct StripeEvent {
    pub id: String,
    #[serde(rename = "type")]
    pub event_type: String,
    pub data: EventData,
}

#[derive(Debug, Clone, Deserialize)]
pub struct EventData {
    pub object: serde_json::Value,
}

/// Dispatch a parsed Stripe event to the appropriate handler.
/// Returns Ok(()) on success; errors cause 500 so Stripe retries.
pub async fn handle_event(db: &PgPool, event: StripeEvent) -> Result<()> {
    tracing::info!("Stripe event received: {} ({})", event.event_type, event.id);

    match event.event_type.as_str() {
        "checkout.session.completed" => {
            // A user just bought a plan. The session object has customer_id
            // and subscription_id; metadata has user_id.
            let session = event.data.object;
            let customer = session["customer"].as_str().unwrap_or("").to_string();
            let subscription = session["subscription"].as_str().unwrap_or("").to_string();
            let user_id = session["metadata"]["user_id"].as_str().unwrap_or("").to_string();

            if customer.is_empty() || subscription.is_empty() {
                return Err(anyhow!("checkout.session.completed missing ids"));
            }

            // Pull the subscription from Stripe to get the price -> tier
            let sub = get_subscription(&subscription).await?;
            upsert_subscription(db, &user_id, &sub).await?;
        }

        "customer.subscription.updated"
        | "customer.subscription.created"
        | "customer.subscription.deleted" => {
            let sub_obj = event.data.object;
            let sub_id = sub_obj["id"].as_str().unwrap_or("").to_string();
            if sub_id.is_empty() {
                return Err(anyhow!("subscription event missing id"));
            }

            let sub = get_subscription(&sub_id).await?;

            // Find the user_id via customer_id
            let customer_id = &sub.customer;
            let user_id: Option<String> = sqlx::query_scalar(
                "SELECT user_id FROM subscriptions WHERE stripe_customer_id = $1",
            )
            .bind(customer_id)
            .fetch_optional(db)
            .await?;

            if let Some(user_id) = user_id {
                upsert_subscription(db, &user_id, &sub).await?;
            } else {
                tracing::warn!("subscription {} has no local user mapping", sub_id);
            }
        }

        "invoice.payment_failed" => {
            let inv = event.data.object;
            let customer = inv["customer"].as_str().unwrap_or("").to_string();
            sqlx::query(
                "UPDATE subscriptions SET status = 'past_due', updated_at = NOW()
                 WHERE stripe_customer_id = $1",
            )
            .bind(&customer)
            .execute(db)
            .await?;
        }

        _ => {
            tracing::debug!("ignoring event type: {}", event.event_type);
        }
    }

    Ok(())
}

async fn upsert_subscription(
    db: &PgPool,
    user_id: &str,
    sub: &crate::billing::stripe::SubscriptionApiResp,
) -> Result<()> {
    let tier = sub
        .items
        .as_ref()
        .and_then(|it| it.data.first())
        .map(|i| tier_for_price(&i.price.id))
        .unwrap_or(crate::billing::PlanTier::Free);

    let period_start: Option<DateTime<Utc>> = sub
        .current_period_start
        .and_then(|t| Utc.timestamp_opt(t, 0).single());
    let period_end: Option<DateTime<Utc>> = sub
        .current_period_end
        .and_then(|t| Utc.timestamp_opt(t, 0).single());

    sqlx::query(
        "INSERT INTO subscriptions (
            user_id, stripe_customer_id, stripe_subscription_id,
            plan_tier, status, current_period_start, current_period_end,
            cancel_at_period_end, created_at, updated_at)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, NOW(), NOW())
         ON CONFLICT (user_id) DO UPDATE SET
            stripe_customer_id    = EXCLUDED.stripe_customer_id,
            stripe_subscription_id= EXCLUDED.stripe_subscription_id,
            plan_tier             = EXCLUDED.plan_tier,
            status                = EXCLUDED.status,
            current_period_start  = EXCLUDED.current_period_start,
            current_period_end    = EXCLUDED.current_period_end,
            cancel_at_period_end  = EXCLUDED.cancel_at_period_end,
            updated_at            = NOW()",
    )
    .bind(user_id)
    .bind(&sub.customer)
    .bind(&sub.id)
    .bind(tier.as_str())
    .bind(&sub.status)
    .bind(period_start)
    .bind(period_end)
    .bind(sub.cancel_at_period_end)
    .execute(db)
    .await?;

    tracing::info!(
        "Subscription upserted: user={} tier={} status={}",
        user_id, tier.as_str(), sub.status
    );
    Ok(())
}
