//! Stripe REST client — minimal surface, no async-stripe dependency.
//!
//! We talk to the Stripe API via `reqwest`, using HTTP Basic auth with the
//! secret key as username and empty password (Stripe convention).
//! Only the endpoints we need are wrapped here.

use serde::{Deserialize, Serialize};
use anyhow::{Result, anyhow};

const STRIPE_API_BASE: &str = "https://api.stripe.com/v1";

fn stripe_secret() -> Result<String> {
    std::env::var("STRIPE_SECRET_KEY").map_err(|_| anyhow!("STRIPE_SECRET_KEY not set"))
}

fn client() -> reqwest::Client {
    reqwest::Client::new()
}

/// Create (or idempotently fetch) a Stripe Customer for this user.
pub async fn ensure_customer(user_id: &str, email: &str) -> Result<String> {
    let secret = stripe_secret()?;
    let resp = client()
        .post(format!("{}/customers", STRIPE_API_BASE))
        .basic_auth(&secret, Some(""))
        // Idempotency by user_id: Stripe will return the same customer on retry.
        .header("Idempotency-Key", format!("customer:{}", user_id))
        .form(&[
            ("email", email),
            ("metadata[user_id]", user_id),
        ])
        .send()
        .await?
        .error_for_status()?;

    #[derive(Deserialize)]
    struct CustomerResp { id: String }
    let c: CustomerResp = resp.json().await?;
    Ok(c.id)
}

/// Create a Stripe Checkout Session for a subscription.
pub async fn create_checkout_session(
    customer_id: &str,
    price_id: &str,
    success_url: &str,
    cancel_url: &str,
) -> Result<CheckoutSession> {
    let secret = stripe_secret()?;
    let resp = client()
        .post(format!("{}/checkout/sessions", STRIPE_API_BASE))
        .basic_auth(&secret, Some(""))
        .form(&[
            ("mode", "subscription"),
            ("customer", customer_id),
            ("line_items[0][price]", price_id),
            ("line_items[0][quantity]", "1"),
            ("success_url", success_url),
            ("cancel_url", cancel_url),
            ("allow_promotion_codes", "true"),
        ])
        .send()
        .await?
        .error_for_status()?;

    Ok(resp.json().await?)
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CheckoutSession {
    pub id: String,
    pub url: Option<String>,
    pub customer: Option<String>,
}

/// Create a Stripe Billing Portal session so the user can manage their
/// subscription (cancel, change plan, update card).
pub async fn create_portal_session(customer_id: &str, return_url: &str) -> Result<String> {
    let secret = stripe_secret()?;
    let resp = client()
        .post(format!("{}/billing_portal/sessions", STRIPE_API_BASE))
        .basic_auth(&secret, Some(""))
        .form(&[
            ("customer", customer_id),
            ("return_url", return_url),
        ])
        .send()
        .await?
        .error_for_status()?;

    #[derive(Deserialize)]
    struct PortalResp { url: String }
    let r: PortalResp = resp.json().await?;
    Ok(r.url)
}

/// Fetch a subscription's current state directly from Stripe (source of truth).
pub async fn get_subscription(subscription_id: &str) -> Result<SubscriptionApiResp> {
    let secret = stripe_secret()?;
    let resp = client()
        .get(format!("{}/subscriptions/{}", STRIPE_API_BASE, subscription_id))
        .basic_auth(&secret, Some(""))
        .send()
        .await?
        .error_for_status()?;
    Ok(resp.json().await?)
}

#[derive(Debug, Clone, Deserialize)]
pub struct SubscriptionApiResp {
    pub id: String,
    pub customer: String,
    pub status: String,
    pub current_period_start: Option<i64>,
    pub current_period_end: Option<i64>,
    pub cancel_at_period_end: bool,
    pub items: Option<SubscriptionItems>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct SubscriptionItems {
    pub data: Vec<SubscriptionItem>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct SubscriptionItem {
    pub price: Price,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Price {
    pub id: String,
}

/// Given a Stripe price ID, figure out which PlanTier it maps to.
/// Relies on env vars STRIPE_PRICE_HOBBY / STRIPE_PRICE_PRO.
pub fn tier_for_price(price_id: &str) -> crate::billing::PlanTier {
    if Some(price_id.to_string()) == std::env::var("STRIPE_PRICE_PRO").ok() {
        crate::billing::PlanTier::Pro
    } else if Some(price_id.to_string()) == std::env::var("STRIPE_PRICE_HOBBY").ok() {
        crate::billing::PlanTier::Hobby
    } else {
        crate::billing::PlanTier::Free
    }
}
