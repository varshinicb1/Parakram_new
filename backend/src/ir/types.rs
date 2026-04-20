//! Parakram IR type definitions.
//!
//! These structs represent the complete IR JSON document
//! as defined in the IR JSON Schema (Artifact 3).

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Complete IR document — the contract between backend and device.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRDocument {
    pub version: String,
    pub program_id: String,
    pub board_id: String,
    pub created_at: String,
    #[serde(default)]
    pub signature: String,
    pub devices: Vec<IRDevice>,
    #[serde(default)]
    pub state: HashMap<String, IRStateVariable>,
    #[serde(default)]
    pub triggers: Vec<IRTrigger>,
    #[serde(default)]
    pub pipelines: Vec<IRPipeline>,
    #[serde(default)]
    pub constraints: IRConstraints,
}

/// A physical hardware device declaration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRDevice {
    pub id: String,
    pub driver: String,
    #[serde(default)]
    pub bus: String,
    #[serde(default, deserialize_with = "deserialize_optional_string")]
    pub address: Option<String>,
    #[serde(default, deserialize_with = "deserialize_optional_string")]
    pub pin_slot: Option<String>,
    pub capabilities: Vec<String>,
    #[serde(default)]
    pub config: Option<serde_json::Value>,
}

/// A typed state variable.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRStateVariable {
    #[serde(rename = "type")]
    pub var_type: String,
    pub initial: serde_json::Value,
    #[serde(default)]
    pub min: Option<f64>,
    #[serde(default)]
    pub max: Option<f64>,
    #[serde(default)]
    pub persistent: Option<bool>,
}

/// A trigger that activates pipelines.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRTrigger {
    pub id: String,
    #[serde(alias = "type", alias = "trigger_type")]
    pub trigger_type: String,
    #[serde(default)]
    pub interval_ms: Option<u64>,
    #[serde(default)]
    pub cron: Option<String>,
    #[serde(default)]
    pub device: Option<String>,
    #[serde(default)]
    pub field: Option<String>,
    #[serde(default)]
    pub threshold: Option<f64>,
    #[serde(default)]
    pub comparison: Option<String>,
    #[serde(default)]
    pub hysteresis: Option<f64>,
    #[serde(default)]
    pub edge: Option<String>,
    #[serde(default)]
    pub topic: Option<String>,
    #[serde(default)]
    pub payload_match: Option<String>,
    #[serde(default)]
    pub ble_event_type: Option<String>,
    #[serde(default)]
    pub window_start: Option<String>,
    #[serde(default)]
    pub window_end: Option<String>,
    #[serde(default)]
    pub debounce_ms: Option<u32>,
}

/// A pipeline — ordered sequence of nodes forming a DAG.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRPipeline {
    pub id: String,
    pub trigger: String,
    #[serde(default = "default_true")]
    pub enabled: bool,
    #[serde(default = "default_priority")]
    pub priority: u8,
    #[serde(default)]
    pub mutex_group: Option<String>,
    pub nodes: Vec<IRNode>,
    pub max_execution_ms: u32,
}

fn default_true() -> bool {
    true
}
fn default_priority() -> u8 {
    5
}

fn default_node_id() -> String {
    uuid::Uuid::new_v4().to_string()
}

/// A single execution node in a pipeline.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRNode {
    #[serde(default = "default_node_id")]
    pub id: String,
    #[serde(alias = "type", alias = "node_type")]
    pub node_type: String,
    #[serde(default)]
    pub next: Option<serde_json::Value>,
    #[serde(default)]
    pub device: Option<String>,
    #[serde(default)]
    pub field: Option<String>,
    #[serde(default)]
    pub variable: Option<String>,
    #[serde(default)]
    pub store_to: Option<String>,
    #[serde(default)]
    pub load_from: Option<String>,
    #[serde(default)]
    pub value: Option<serde_json::Value>,
    #[serde(default)]
    pub left: Option<serde_json::Value>,
    #[serde(default)]
    pub right: Option<serde_json::Value>,
    #[serde(default)]
    pub op: Option<String>,
    #[serde(default)]
    pub if_true: Option<String>,
    #[serde(default)]
    pub if_false: Option<String>,
    #[serde(default)]
    pub operands: Option<Vec<serde_json::Value>>,
    #[serde(default)]
    pub min_value: Option<f64>,
    #[serde(default)]
    pub max_value: Option<f64>,
    #[serde(default)]
    pub in_min: Option<f64>,
    #[serde(default)]
    pub in_max: Option<f64>,
    #[serde(default)]
    pub out_min: Option<f64>,
    #[serde(default)]
    pub out_max: Option<f64>,
    #[serde(default)]
    pub topic: Option<String>,
    #[serde(default)]
    pub payload: Option<String>,
    #[serde(default)]
    pub characteristic: Option<String>,
    #[serde(default)]
    pub fields: Option<Vec<String>>,
    #[serde(default)]
    pub destination: Option<String>,
    #[serde(default)]
    pub text: Option<String>,
    #[serde(default)]
    pub line: Option<u8>,
    #[serde(default)]
    pub duration_ms: Option<u32>,
    #[serde(default)]
    pub duty_percent: Option<f64>,
    #[serde(default)]
    pub color: Option<RGBColor>,
    #[serde(default)]
    pub led_index: Option<i32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RGBColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

/// Global constraints for the program.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct IRConstraints {
    #[serde(default = "default_max_nodes")]
    pub max_total_nodes: u32,
    #[serde(default = "default_max_state")]
    pub max_state_variables: u32,
    #[serde(default = "default_max_pipelines")]
    pub max_pipelines: u32,
    #[serde(default)]
    pub max_stack_depth: Option<u32>,
    #[serde(default)]
    pub total_state_bytes: Option<u32>,
    #[serde(default)]
    pub max_execution_ms: Option<u32>,
    #[serde(default)]
    pub max_devices: Option<u32>,
    #[serde(default)]
    pub global_mutex_groups: Option<Vec<IRMutexGroup>>,
    #[serde(default)]
    pub resource_budgets: Option<serde_json::Value>,
}

fn default_max_nodes() -> u32 { 256 }
fn default_max_state() -> u32 { 64 }
fn default_max_pipelines() -> u32 { 16 }

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRMutexGroup {
    pub name: String,
    pub pipelines: Vec<String>,
}

/// Robust deserializer to handle LLMs outputting integers for string fields.
fn deserialize_optional_string<'de, D>(deserializer: D) -> Result<Option<String>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    match Option::<serde_json::Value>::deserialize(deserializer)? {
        Some(serde_json::Value::String(s)) => Ok(Some(s)),
        Some(serde_json::Value::Number(n)) => Ok(Some(n.to_string())),
        Some(serde_json::Value::Null) | None => Ok(None),
        _ => Err(serde::de::Error::custom("expected string or number")),
    }
}
