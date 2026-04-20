//! Driver Marketplace — community-contributed driver types and helpers.
//!
//! This module is the shared data layer for the marketplace feature.
//! Database helpers live in `db`, the submission validator in `validator`.

pub mod db;
pub mod validator;

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// A community-contributed driver row as returned by list/search endpoints.
///
/// `source_code` is intentionally absent here — it must never be leaked in
/// listing responses.  Use [`CommunityDriverFull`] for detail/admin views.
#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct CommunityDriver {
    pub id: Uuid,
    pub author_id: String,
    pub name: String,
    pub display_name: String,
    pub description: String,
    pub version: String,
    pub driver_type: String,
    pub bus_types: Vec<String>,
    pub capabilities: Vec<String>,
    pub status: String,
    pub rejection_reason: Option<String>,
    pub downloads: i32,
    pub stars_total: i32,
    pub stars_count: i32,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

/// Full driver row including `source_code` and `validation_json`.
///
/// Only returned on detail / admin endpoints that require authentication.
#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct CommunityDriverFull {
    pub id: Uuid,
    pub author_id: String,
    pub name: String,
    pub display_name: String,
    pub description: String,
    pub version: String,
    pub driver_type: String,
    pub bus_types: Vec<String>,
    pub capabilities: Vec<String>,
    pub source_code: String,
    pub status: String,
    pub rejection_reason: Option<String>,
    pub validation_json: Option<serde_json::Value>,
    pub downloads: i32,
    pub stars_total: i32,
    pub stars_count: i32,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

/// Compute an average star rating from cumulative totals.
///
/// Returns `0.0` when no ratings have been submitted yet.
pub fn avg_stars(total: i32, count: i32) -> f32 {
    if count == 0 {
        0.0
    } else {
        total as f32 / count as f32
    }
}
