//! ROS 2 node graph generation — 100% local, no LLM required.
//! POST /api/ros/generate — plain-English → Isaac ROS node graph in microseconds.

use axum::{extract::State, http::StatusCode, routing::post, Json, Router};
use serde::{Deserialize, Serialize};
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::ros_graph;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/generate", post(generate_node_graph))
        .route("/templates", axum::routing::get(list_templates))
}

#[derive(Debug, Deserialize)]
pub struct RosGenerateRequest {
    pub description: String,
    #[serde(default = "default_platform")]
    pub platform: String,
    #[serde(default)]
    pub available_packages: Vec<String>,
    #[serde(default)]
    pub device_ids: Vec<String>,
}

fn default_platform() -> String { "jetson_orin".into() }

#[derive(Debug, Serialize)]
pub struct GenerateResponse {
    pub description: String,
    pub matched_templates: Vec<String>,
    pub confidence: f32,
    pub nodes: Vec<ros_graph::NodeDef>,
    pub topics: Vec<ros_graph::TopicDef>,
    pub launch_snippet: String,
    pub parakram_topics: Vec<String>,
    pub generation_time_us: u64,
    pub engine: String,
}

#[derive(Debug, Serialize)]
pub struct TemplateInfo {
    pub name: String,
    pub keywords: Vec<String>,
}

async fn generate_node_graph(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<RosGenerateRequest>,
) -> Result<Json<GenerateResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let _claims = validate_token(&token, &state)?;

    if req.description.trim().is_empty() {
        return Err((StatusCode::BAD_REQUEST, Json(ErrorBody {
            error: ErrorDetail { code: "EMPTY_DESCRIPTION".into(), message: "description cannot be empty".into() },
        })));
    }

    let graph = ros_graph::generate(&req.description, &req.platform, &req.device_ids);

    Ok(Json(GenerateResponse {
        description: graph.description,
        matched_templates: graph.matched_templates,
        confidence: graph.confidence,
        nodes: graph.nodes,
        topics: graph.topics,
        launch_snippet: graph.launch_snippet,
        parakram_topics: graph.parakram_topics,
        generation_time_us: graph.generation_time_us,
        engine: "parakram-ros-graph-v1-local".into(),
    }))
}

async fn list_templates(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> Result<Json<Vec<TemplateInfo>>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let _claims = validate_token(&token, &state)?;

    let infos: Vec<TemplateInfo> = ros_graph::templates::all_templates()
        .into_iter()
        .map(|t| TemplateInfo {
            name: t.name.into(),
            keywords: t.keywords.iter().map(|(k, _)| k.to_string()).collect(),
        })
        .collect();

    Ok(Json(infos))
}
