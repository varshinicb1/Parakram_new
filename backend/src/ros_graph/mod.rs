//! Local ROS 2 node graph intelligence engine.
//!
//! No LLM required. Works entirely offline.
//!
//! Pipeline:
//!   1. `classifier` scores every template against the user's prompt
//!   2. `composer`   merges the top-scoring templates into one unified graph
//!   3. `launcher`   renders a Python ROS 2 launch file from the graph
//!
//! To add new domain knowledge: add a `GraphTemplate` to `templates.rs`.
//! No retraining, no API calls — just structured Rust data.

pub mod classifier;
pub mod composer;
pub mod launcher;
pub mod templates;

use serde::{Deserialize, Serialize};

/// A single ROS 2 node definition.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NodeDef {
    pub package: String,
    pub executable: String,
    /// Unique name within the launch file; deduplication key.
    pub name: String,
    pub parameters: serde_json::Value,
    pub remappings: Vec<[String; 2]>,
    /// Topic names this node publishes (for graph wiring).
    #[serde(default)]
    pub publishes: Vec<String>,
    /// Topic names this node subscribes to.
    #[serde(default)]
    pub subscribes: Vec<String>,
}

/// A ROS 2 topic inferred from the node graph.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TopicDef {
    pub name: String,
    pub msg_type: String,
    pub publisher: String,
    pub subscribers: Vec<String>,
    pub hz: f32,
}

/// The final composed output sent back to the caller.
#[derive(Debug, Serialize)]
pub struct GeneratedGraph {
    pub description: String,
    pub matched_templates: Vec<String>,
    pub confidence: f32,
    pub nodes: Vec<NodeDef>,
    pub topics: Vec<TopicDef>,
    pub launch_snippet: String,
    pub parakram_topics: Vec<String>,
    pub generation_time_us: u64,
}

/// Entry point — classify the prompt, compose the graph, render the launch file.
pub fn generate(description: &str, platform: &str, device_ids: &[String]) -> GeneratedGraph {
    let t0 = std::time::Instant::now();

    let scored = classifier::score_all(description);
    let selected = classifier::select_templates(&scored);
    let (nodes, topics) = composer::compose(&selected, description, device_ids);
    let launch = launcher::render(&nodes, platform, device_ids);

    let parakram_topics: Vec<String> = device_ids.iter()
        .flat_map(|id| {
            let safe = id.replace('-', "_").to_lowercase();
            vec![
                format!("/parakram/{safe}/telemetry"),
                format!("/parakram/{safe}/status"),
            ]
        })
        .collect();

    let matched_names: Vec<String> = selected.iter().map(|t| t.name.to_string()).collect();
    let confidence = scored.first().map(|(_, s)| *s).unwrap_or(0.0);

    GeneratedGraph {
        description: description.to_string(),
        matched_templates: matched_names,
        confidence,
        nodes,
        topics,
        launch_snippet: launch,
        parakram_topics,
        generation_time_us: t0.elapsed().as_micros() as u64,
    }
}
