//! Plan definitions — single source of truth for quotas and pricing.
//!
//! Parakram pricing: Free (2 projects) + Maker ($1.50/mo, unlimited).
//! Stripe price ID read from STRIPE_PRICE_MAKER env var.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PlanTier {
    Free,
    Maker,
}

impl PlanTier {
    pub fn as_str(&self) -> &'static str {
        match self {
            PlanTier::Free  => "free",
            PlanTier::Maker => "maker",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s {
            "maker" => PlanTier::Maker,
            _       => PlanTier::Free,
        }
    }

    /// Stripe price ID, sourced from env vars.
    pub fn stripe_price_id(&self) -> Option<String> {
        match self {
            PlanTier::Maker => std::env::var("STRIPE_PRICE_MAKER").ok(),
            _               => None,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct Plan {
    pub tier: PlanTier,
    pub display_name: &'static str,
    pub monthly_price_usd: f32,
    pub max_projects: i32,
    pub llm_intents_per_month: i32,
    pub compiles_per_month: i32,
    pub deploys_per_month: i32,
    pub max_devices: i32,
    pub support: &'static str,
    pub features: &'static [&'static str],
}

/// Canonical plan catalog.
pub fn catalog() -> Vec<Plan> {
    vec![
        Plan {
            tier: PlanTier::Free,
            display_name: "Free",
            monthly_price_usd: 0.0,
            max_projects: 2,
            llm_intents_per_month: 50,
            compiles_per_month: 100,
            deploys_per_month: 20,
            max_devices: 2,
            support: "Community (GitHub Issues)",
            features: &[
                "2 projects",
                "2 devices",
                "All 63 drivers",
                "Bytecode compilation",
                "LVGL Visual Designer",
                "WebSerial flasher",
            ],
        },
        Plan {
            tier: PlanTier::Maker,
            display_name: "Maker",
            monthly_price_usd: 1.50,
            max_projects: -1, // unlimited
            llm_intents_per_month: 500,
            compiles_per_month: 5_000,
            deploys_per_month: 1_000,
            max_devices: 10,
            support: "Email (varshinicb@vidyuthlabs.co.in)",
            features: &[
                "Unlimited projects",
                "10 devices",
                "Everything in Free",
                "Priority email support",
                "OTA firmware updates",
                "Fleet dashboard",
                "Project sync across devices",
                "Community driver marketplace",
            ],
        },
    ]
}

/// Look up the plan for a given tier.
pub fn for_tier(tier: PlanTier) -> Plan {
    catalog().into_iter().find(|p| p.tier == tier).expect("tier in catalog")
}
