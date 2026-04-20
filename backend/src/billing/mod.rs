//! Billing module — Stripe subscription management and quota enforcement.
//!
//! The module is LLM-free and Stripe-free at compile time: we talk to Stripe
//! via HTTPS with `reqwest` rather than pulling in the full `async-stripe` SDK.
//! This keeps the binary small and avoids version-locking against a fast-moving
//! upstream.
//!
//! Plans are defined in `plans.rs` (single source of truth). Quota enforcement
//! happens in `quota.rs` (middleware). Stripe REST calls live in `stripe.rs`.

pub mod plans;
pub mod quota;
pub mod stripe;
pub mod webhook;

use serde::{Deserialize, Serialize};

pub use plans::PlanTier;
pub use quota::{check_quota, increment_usage, QuotaKind};

/// Persistent subscription row mirroring the Stripe source of truth.
#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct SubscriptionRow {
    pub user_id: String,
    pub stripe_customer_id: Option<String>,
    pub stripe_subscription_id: Option<String>,
    pub plan_tier: String,   // "free" | "hobby" | "pro" | "enterprise"
    pub status: String,      // "active" | "trialing" | "past_due" | "canceled"
    pub current_period_start: Option<chrono::DateTime<chrono::Utc>>,
    pub current_period_end: Option<chrono::DateTime<chrono::Utc>>,
    pub cancel_at_period_end: bool,
    pub created_at: chrono::DateTime<chrono::Utc>,
    pub updated_at: chrono::DateTime<chrono::Utc>,
}

/// Rolling monthly usage counter for a given user.
#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct UsageRow {
    pub user_id: String,
    pub period_start: chrono::DateTime<chrono::Utc>,
    pub llm_intents: i32,
    pub compiles: i32,
    pub deploys: i32,
    pub devices_active: i32,
    pub updated_at: chrono::DateTime<chrono::Utc>,
}
