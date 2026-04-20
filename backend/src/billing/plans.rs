//! Plan definitions — single source of truth for quotas and pricing.
//!
//! Stripe price IDs are read from environment variables so the same binary
//! works across dev/staging/prod without recompiling. Missing price IDs are
//! OK at startup (we only need them when a checkout is requested).

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PlanTier {
    Free,
    Hobby,
    Pro,
    Enterprise,
}

impl PlanTier {
    pub fn as_str(&self) -> &'static str {
        match self {
            PlanTier::Free       => "free",
            PlanTier::Hobby      => "hobby",
            PlanTier::Pro        => "pro",
            PlanTier::Enterprise => "enterprise",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s {
            "hobby"      => PlanTier::Hobby,
            "pro"        => PlanTier::Pro,
            "enterprise" => PlanTier::Enterprise,
            _            => PlanTier::Free,
        }
    }

    /// Stripe price ID, sourced from env vars.
    /// Return `None` for Free (no Stripe plan) and Enterprise (sales-led).
    pub fn stripe_price_id(&self) -> Option<String> {
        match self {
            PlanTier::Hobby => std::env::var("STRIPE_PRICE_HOBBY").ok(),
            PlanTier::Pro   => std::env::var("STRIPE_PRICE_PRO").ok(),
            _               => None,
        }
    }
}

/// Plan is only ever serialized out (plan catalog → JSON); we never
/// deserialize it on the backend, so we keep the static refs without
/// implementing Deserialize.
#[derive(Debug, Clone, Serialize)]
pub struct Plan {
    pub tier: PlanTier,
    pub display_name: &'static str,
    pub monthly_price_usd: u32,
    pub llm_intents_per_month: i32,
    pub compiles_per_month: i32,
    pub deploys_per_month: i32,
    pub max_devices: i32,
    pub support: &'static str,
    pub features: &'static [&'static str],
}

/// Canonical plan catalog. Add new plans here.
pub fn catalog() -> Vec<Plan> {
    vec![
        Plan {
            tier: PlanTier::Free,
            display_name: "Free",
            monthly_price_usd: 0,
            llm_intents_per_month: 20,
            compiles_per_month: 50,
            deploys_per_month: 10,
            max_devices: 1,
            support: "Community",
            features: &[
                "Web playground",
                "1 device",
                "All 59 drivers",
                "Bytecode compilation",
            ],
        },
        Plan {
            tier: PlanTier::Hobby,
            display_name: "Hobby",
            monthly_price_usd: 9,
            llm_intents_per_month: 500,
            compiles_per_month: 2_000,
            deploys_per_month: 500,
            max_devices: 5,
            support: "Email",
            features: &[
                "5 devices",
                "Everything in Free",
                "Email support",
                "Project templates",
                "ROS 2 node graph generator",
            ],
        },
        Plan {
            tier: PlanTier::Pro,
            display_name: "Pro",
            monthly_price_usd: 29,
            llm_intents_per_month: 10_000,
            compiles_per_month: 50_000,
            deploys_per_month: 10_000,
            max_devices: 50,
            support: "Priority email + Slack",
            features: &[
                "50 devices",
                "Everything in Hobby",
                "Priority support",
                "Fleet coordination",
                "OTA updates",
                "Mobile app sync",
            ],
        },
        Plan {
            tier: PlanTier::Enterprise,
            display_name: "Enterprise",
            monthly_price_usd: 0, // Custom pricing
            llm_intents_per_month: -1, // unlimited
            compiles_per_month: -1,
            deploys_per_month: -1,
            max_devices: -1,
            support: "Dedicated CSM + SLA",
            features: &[
                "Unlimited devices",
                "Everything in Pro",
                "99.9% SLA",
                "On-prem deployment option",
                "Custom driver development",
                "24×7 phone support",
            ],
        },
    ]
}

/// Look up the plan for a given tier.
pub fn for_tier(tier: PlanTier) -> Plan {
    catalog().into_iter().find(|p| p.tier == tier).expect("tier in catalog")
}
