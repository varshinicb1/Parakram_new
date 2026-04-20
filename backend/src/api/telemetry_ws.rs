//! WebSocket telemetry endpoint.
//!
//! GET /api/telemetry/ws/:deviceId?token=<jwt>
//!
//! On connect: sends {"type":"connected","deviceId":"..."}.
//! Every 2 s: pushes a randomised telemetry frame.
//! Client → server: {"type":"ping"} → {"type":"pong"}.
//! JWT is passed as `?token=` query param (WS handshake cannot set headers easily).

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Path, Query, State,
    },
    http::StatusCode,
    response::IntoResponse,
    routing::get,
    Json, Router,
};
use rand::Rng;
use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::api::auth::{validate_token, ErrorBody, ErrorDetail};
use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new().route("/ws/:device_id", get(ws_handler))
}

// ---------------------------------------------------------------------------
// Query params
// ---------------------------------------------------------------------------

#[derive(Debug, Deserialize)]
pub struct WsParams {
    pub token: Option<String>,
}

// ---------------------------------------------------------------------------
// Telemetry frame types
// ---------------------------------------------------------------------------

#[derive(Debug, Serialize)]
struct ConnectedFrame<'a> {
    #[serde(rename = "type")]
    kind: &'static str,
    #[serde(rename = "deviceId")]
    device_id: &'a str,
}

#[derive(Debug, Serialize)]
struct TelemetryData {
    temperature: f64,
    humidity: f64,
    uptime_s: u64,
    free_heap: u64,
    rssi: i32,
}

#[derive(Debug, Serialize)]
struct TelemetryFrame {
    #[serde(rename = "type")]
    kind: &'static str,
    ts: u64,
    data: TelemetryData,
}

#[derive(Debug, Serialize)]
struct PongFrame {
    #[serde(rename = "type")]
    kind: &'static str,
}

#[derive(Debug, Deserialize)]
struct ClientMessage {
    #[serde(rename = "type")]
    kind: String,
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

async fn ws_handler(
    State(state): State<AppState>,
    Path(device_id): Path<String>,
    Query(params): Query<WsParams>,
    ws: WebSocketUpgrade,
) -> impl IntoResponse {
    // Validate JWT from query param before upgrading
    let token = match params.token {
        Some(ref t) if !t.is_empty() => t.clone(),
        _ => {
            return (
                StatusCode::UNAUTHORIZED,
                Json(ErrorBody {
                    error: ErrorDetail {
                        code: "MISSING_TOKEN".into(),
                        message: "token query parameter required".into(),
                    },
                }),
            )
                .into_response();
        }
    };

    if let Err(e) = validate_token(&token, &state) {
        return e.into_response();
    }

    tracing::info!(device_id = %device_id, "WebSocket telemetry upgrade");

    ws.on_upgrade(move |socket| handle_socket(socket, device_id))
}

async fn handle_socket(mut socket: WebSocket, device_id: String) {
    // Send connected frame
    let connected = ConnectedFrame {
        kind: "connected",
        device_id: &device_id,
    };
    if let Ok(msg) = serde_json::to_string(&connected) {
        if socket.send(Message::Text(msg)).await.is_err() {
            tracing::error!(device_id = %device_id, "Failed to send connected frame");
            return;
        }
    }

    let mut interval = tokio::time::interval(tokio::time::Duration::from_secs(2));
    // The first tick fires immediately — skip it so we don't send two frames at once.
    interval.tick().await;

    let start_ts = unix_now();

    loop {
        tokio::select! {
            // Periodic telemetry push
            _ = interval.tick() => {
                let frame = build_telemetry_frame(start_ts);
                match serde_json::to_string(&frame) {
                    Ok(msg) => {
                        if socket.send(Message::Text(msg)).await.is_err() {
                            tracing::info!(device_id = %device_id, "WS client disconnected (send error)");
                            return;
                        }
                    }
                    Err(e) => {
                        tracing::error!(device_id = %device_id, "Telemetry serialize error: {}", e);
                    }
                }
            }

            // Incoming message from client
            msg = socket.recv() => {
                match msg {
                    None => {
                        tracing::info!(device_id = %device_id, "WS connection closed by client");
                        return;
                    }
                    Some(Err(e)) => {
                        tracing::error!(device_id = %device_id, "WS recv error: {}", e);
                        return;
                    }
                    Some(Ok(Message::Text(text))) => {
                        handle_client_text(&mut socket, &device_id, &text).await;
                    }
                    Some(Ok(Message::Close(_))) => {
                        tracing::info!(device_id = %device_id, "WS close frame received");
                        return;
                    }
                    Some(Ok(Message::Ping(payload))) => {
                        // axum handles pong automatically for protocol-level pings,
                        // but we also forward just in case.
                        if socket.send(Message::Pong(payload)).await.is_err() {
                            return;
                        }
                    }
                    Some(Ok(_)) => { /* binary / pong — ignore */ }
                }
            }
        }
    }
}

async fn handle_client_text(socket: &mut WebSocket, device_id: &str, text: &str) {
    match serde_json::from_str::<ClientMessage>(text) {
        Ok(msg) if msg.kind == "ping" => {
            let pong = PongFrame { kind: "pong" };
            if let Ok(reply) = serde_json::to_string(&pong) {
                if socket.send(Message::Text(reply)).await.is_err() {
                    tracing::error!(device_id = %device_id, "Failed to send pong");
                }
            }
        }
        Ok(msg) => {
            tracing::info!(device_id = %device_id, kind = %msg.kind, "Unknown WS message type");
        }
        Err(e) => {
            tracing::warn!(device_id = %device_id, "Unparseable WS message: {}", e);
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn unix_now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn build_telemetry_frame(start_ts: u64) -> TelemetryFrame {
    let mut rng = rand::thread_rng();
    let now = unix_now();

    TelemetryFrame {
        kind: "telemetry",
        ts: now,
        data: TelemetryData {
            temperature: rng.gen_range(20.0_f64..=35.0_f64),
            humidity: rng.gen_range(40.0_f64..=80.0_f64),
            uptime_s: now.saturating_sub(start_ts),
            free_heap: rng.gen_range(64_000_u64..=256_000_u64),
            rssi: rng.gen_range(-90_i32..=-50_i32),
        },
    }
}
