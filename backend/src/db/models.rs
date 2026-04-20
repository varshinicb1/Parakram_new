//! Database model types for query results.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct UserRow {
    pub user_id: String,
    pub username: String,
    pub password_hash: String,
    pub created_at: String,
    pub last_login_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct DeviceRow {
    pub device_id: String,
    pub user_id: String,
    pub board_sku: String,
    pub name: String,
    pub device_pubkey: String,
    pub firmware_version: Option<String>,
    pub ip_address: Option<String>,
    pub ble_address: Option<String>,
    pub status: String,
    pub active_program_id: Option<String>,
    pub error_count: i32,
    pub paired_at: String,
    pub last_seen_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct ProjectRow {
    pub project_id: String,
    pub user_id: String,
    pub device_id: String,
    pub name: String,
    pub description: Option<String>,
    pub ir_json: Option<String>,
    pub bytecode_hash: Option<String>,
    pub created_at: String,
    pub updated_at: String,
    pub deployed_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct TelemetryRow {
    pub id: i64,
    pub device_id: String,
    pub pipeline_id: String,
    pub timestamp: String,
    pub tick: Option<i64>,
    pub values_json: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, sqlx::FromRow)]
pub struct DeploymentRow {
    pub id: i64,
    pub project_id: String,
    pub device_id: String,
    pub bytecode_hash: String,
    pub transfer_method: String,
    pub status: String,
    pub error_message: Option<String>,
    pub deployed_at: String,
}
