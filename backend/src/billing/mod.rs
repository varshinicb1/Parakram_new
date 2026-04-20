//! Billing — UPI-based payment system (₹0 fees).
//!
//! Flow:
//! 1. User sees UPI payment link/QR for ₹125/month
//! 2. Pays via any UPI app (GPay, PhonePe, Paytm, etc.)
//! 3. Submits UTR (UPI Transaction Reference) via API
//! 4. Admin verifies and approves → user gets Maker tier
//!
//! Zero payment gateway fees. Zero monthly charges.

pub mod plans;
pub mod quota;

// Re-export quota types for backwards compatibility
pub use quota::{check_quota, increment_usage, QuotaKind, QuotaError};

use serde::{Deserialize, Serialize};
use sqlx::FromRow;
use chrono::{DateTime, Utc};

/// UPI payment configuration
pub const UPI_VPA: &str = "varshinicb@okicici"; // Your UPI ID
pub const UPI_PAYEE_NAME: &str = "Vidyuthlabs";
pub const UPI_AMOUNT_INR: u32 = 125; // ₹125 ≈ $1.50
pub const UPI_CURRENCY: &str = "INR";

/// Generate a UPI deep link for payment
pub fn upi_payment_link(user_id: &str) -> String {
    format!(
        "upi://pay?pa={}&pn={}&am={}&cu={}&tn=Parakram Maker - {}",
        UPI_VPA, UPI_PAYEE_NAME, UPI_AMOUNT_INR, UPI_CURRENCY, user_id
    )
}

/// Generate UPI QR code data (same format, apps can scan it)
pub fn upi_qr_data(user_id: &str) -> String {
    upi_payment_link(user_id)
}

#[derive(Debug, Clone, Serialize, Deserialize, FromRow)]
pub struct SubscriptionRow {
    pub user_id: String,
    pub plan_tier: String,
    pub status: String,
    pub upi_utr: Option<String>,
    pub payment_verified: bool,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub expires_at: Option<DateTime<Utc>>,
}

#[derive(Debug, Clone, Serialize, Deserialize, FromRow)]
pub struct UsageRow {
    pub user_id: String,
    pub period_start: DateTime<Utc>,
    pub llm_intents: i32,
    pub compiles: i32,
    pub deploys: i32,
    pub devices_active: i32,
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Clone, Serialize, Deserialize, FromRow)]
pub struct PaymentClaim {
    pub id: i64,
    pub user_id: String,
    pub upi_utr: String,
    pub amount_inr: i32,
    pub status: String, // "pending", "approved", "rejected"
    pub submitted_at: DateTime<Utc>,
    pub reviewed_at: Option<DateTime<Utc>>,
    pub reviewed_by: Option<String>,
}
