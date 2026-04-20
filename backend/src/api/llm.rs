//! LLM endpoints — natural language intent processing.

use axum::{extract::State, http::StatusCode, routing::post, Json, Router};
use serde::{Deserialize, Serialize};
use std::time::Instant;
use crate::AppState;
use crate::api::auth::{extract_bearer_token, validate_token, ErrorBody, ErrorDetail};
use crate::ir::validator;
use crate::llm::client::LLMClient;
use crate::llm::prompt::build_system_prompt;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/intent", post(process_intent))
}

#[derive(Debug, Deserialize)]
pub struct IntentRequest {
    pub description: String,
    #[serde(alias = "boardId")]
    pub board_id: String,
    #[serde(default, alias = "deviceId")]
    pub device_id: Option<String>,
    #[serde(default)]
    pub context: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct IntentResponse {
    pub feasible: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ir: Option<serde_json::Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ir_preview: Option<IRPreview>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub validation: Option<validator::ValidationResult>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub clarifications: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub suggestions: Option<Vec<String>>,
    pub llm_model: String,
    pub generation_time_ms: u64,
}

#[derive(Debug, Serialize)]
pub struct IRPreview {
    pub summary: String,
    pub triggers: Vec<TriggerPreview>,
    pub actions: Vec<ActionPreview>,
    pub sensors_used: Vec<String>,
    pub actuators_used: Vec<String>,
}

#[derive(Debug, Serialize)]
pub struct TriggerPreview {
    pub description: String,
    pub interval: String,
}

#[derive(Debug, Serialize)]
pub struct ActionPreview {
    pub condition: String,
    pub action: String,
}

async fn process_intent(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(req): Json<IntentRequest>,
) -> Result<Json<IntentResponse>, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    // Enforce monthly LLM-intent quota before spending the API call.
    if let Err(e) = crate::billing::check_quota(
        &state.db, &claims.sub, crate::billing::QuotaKind::LlmIntent,
    ).await {
        let (code, msg) = crate::billing::quota::quota_error_response(e);
        return Err((code, Json(ErrorBody {
            error: ErrorDetail { code: "QUOTA_EXCEEDED".into(), message: msg },
        })));
    }

    // Resolve LLM key: user's own key takes priority over server-wide key.
    let user_key: Option<String> = sqlx::query_scalar(
        "SELECT llm_api_key FROM users WHERE user_id::text = $1"
    )
    .bind(&claims.sub)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?
    .flatten();

    let client = match user_key.filter(|k| !k.is_empty()) {
        Some(key) => LLMClient::for_user_key(key),
        None if state.llm_api_key != "not-configured" => {
            LLMClient::new(state.llm_api_key.clone(), state.llm_model.clone())
        }
        None => return Err((StatusCode::PAYMENT_REQUIRED, Json(ErrorBody {
            error: ErrorDetail {
                code: "NO_LLM_KEY".into(),
                message: "Add your OpenRouter API key in Settings to use AI features.".into(),
            },
        }))),
    };

    let board_row: Option<(serde_json::Value, serde_json::Value)> = sqlx::query_as(
        "SELECT pin_map, default_devices FROM board_skus WHERE sku = $1"
    )
    .bind(&req.board_id)
    .fetch_optional(&state.db).await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
        error: ErrorDetail { code: "DB_ERROR".into(), message: e.to_string() },
    })))?;

    let board_json = if let Some((pin_map, devices)) = board_row {
        format!(r#"{{"sku": "{}", "pin_map": {}, "default_devices": {}}}"#,
            req.board_id, pin_map, devices)
    } else {
        format!(r#"{{"sku": "{}", "connected_sensors": [], "connected_actuators": []}}"#, req.board_id)
    };

    let system_prompt = build_system_prompt(&board_json);

    let start = Instant::now();

    match client.process_intent(&req.description, &board_json, &system_prompt).await {
        Ok(result) => {
            let generation_time = start.elapsed().as_millis() as u64;

            if !result.feasible {
                let _ = sqlx::query(
                    "INSERT INTO llm_logs (user_id, request_type, model, input_prompt, output_response, valid, processing_ms)
                     VALUES ($1::uuid, 'feasibility', $2, $3, $4, false, $5)"
                )
                .bind(&claims.sub).bind(&state.llm_model)
                .bind(&req.description).bind(result.reason.as_deref().unwrap_or(""))
                .bind(generation_time as i32)
                .execute(&state.db).await;

                return Ok(Json(IntentResponse {
                    feasible: false,
                    ir: None, ir_preview: None, validation: None,
                    reason: result.reason,
                    clarifications: result.clarifications,
                    suggestions: result.suggestions,
                    llm_model: result.model,
                    generation_time_ms: generation_time,
                }));
            }

            let ir = match result.ir {
                Some(ir) => ir,
                None => return Err((StatusCode::INTERNAL_SERVER_ERROR, Json(ErrorBody {
                    error: ErrorDetail { code: "LLM_ERROR".into(), message: "LLM returned feasible=true but no IR".into() },
                }))),
            };
            let validation = validator::validate_ir(&ir, &state.driver_registry);
            let preview = build_ir_preview(&ir);

            let ir_str = serde_json::to_string(&ir).unwrap_or_default();
            let ir_value = serde_json::to_value(&ir).unwrap_or_default();
            let _ = sqlx::query(
                "INSERT INTO llm_logs (user_id, request_type, model, input_prompt, output_response, valid, processing_ms)
                 VALUES ($1::uuid, 'generation', $2, $3, $4, $5, $6)"
            )
            .bind(&claims.sub).bind(&state.llm_model)
            .bind(&req.description).bind(&ir_str)
            .bind(validation.valid)
            .bind(generation_time as i32)
            .execute(&state.db).await;

            // Charge the quota only on successful generation.
            let _ = crate::billing::increment_usage(
                &state.db, &claims.sub, crate::billing::QuotaKind::LlmIntent,
            ).await;
            crate::metrics::LLM_INTENTS_TOTAL.inc();

            Ok(Json(IntentResponse {
                feasible: true,
                ir: Some(ir_value.clone()),
                ir_preview: Some(preview),
                validation: Some(validation),
                reason: None, clarifications: None, suggestions: None,
                llm_model: result.model,
                generation_time_ms: generation_time,
            }))
        }
        Err(e) => {
            Err((StatusCode::UNPROCESSABLE_ENTITY, Json(ErrorBody {
                error: ErrorDetail { code: "LLM_ERROR".into(), message: e },
            })))
        }
    }
}

fn build_ir_preview(ir: &crate::ir::types::IRDocument) -> IRPreview {
    let triggers: Vec<TriggerPreview> = ir.triggers.iter().map(|t| {
        let desc = match t.trigger_type.as_str() {
            "timer" => format!("Every {} seconds", t.interval_ms.unwrap_or(0) / 1000),
            "sensor_threshold" => format!("{} {} {} {}",
                t.device.as_deref().unwrap_or("?"),
                t.field.as_deref().unwrap_or("?"),
                t.comparison.as_deref().unwrap_or("?"),
                t.threshold.unwrap_or(0.0)),
            "startup" => "On device startup".into(),
            _ => t.trigger_type.clone(),
        };
        TriggerPreview {
            description: desc,
            interval: t.interval_ms.map(|ms| format!("{}ms", ms)).unwrap_or_default(),
        }
    }).collect();

    let actuator_drivers = ["drv_relay","drv_servo","drv_ws2812","drv_buzzer","drv_motor_dc",
        "drv_motor_stepper","drv_lcd_i2c","drv_oled_ssd1306","drv_solenoid","drv_fan_pwm"];

    let sensors: Vec<String> = ir.devices.iter()
        .filter(|d| !actuator_drivers.contains(&d.driver.as_str()))
        .map(|d| format!("{} ({})", d.id, d.driver))
        .collect();

    let actuators: Vec<String> = ir.devices.iter()
        .filter(|d| actuator_drivers.contains(&d.driver.as_str()))
        .map(|d| format!("{} ({})", d.id, d.driver))
        .collect();

    let summary = format!("{} pipeline(s) with {} trigger(s), {} sensor(s), {} actuator(s)",
        ir.pipelines.len(), ir.triggers.len(), sensors.len(), actuators.len());

    IRPreview {
        summary, triggers, actions: Vec::new(),
        sensors_used: sensors, actuators_used: actuators,
    }
}
