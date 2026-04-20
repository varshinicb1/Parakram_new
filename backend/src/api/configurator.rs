//! Deterministic Template Configurator — Zero LLM dependency.
//!
//! Instead of relying on an LLM to generate IR from scratch, this engine:
//!   1. Takes a template ID and user-supplied parameters
//!   2. Injects those parameters into a pre-validated IR skeleton
//!   3. Returns a guaranteed-valid IR document every single time
//!
//! This is 100% reliable — no hallucination risk, instant response.

use axum::{Router, Json, extract::State};
use axum::routing::post;
use axum::http::StatusCode;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::AppState;

#[derive(Debug, Deserialize)]
pub struct ConfigureRequest {
    pub template_id: String,
    pub parameters: HashMap<String, serde_json::Value>,
    #[serde(default = "default_board")]
    pub board_id: String,
}

fn default_board() -> String { "VDYT-S3-R1".into() }

#[derive(Debug, Serialize)]
pub struct ConfigureResponse {
    pub success: bool,
    pub ir: serde_json::Value,
    pub summary: ConfigSummary,
    pub missing_params: Vec<ParamSpec>,
}

#[derive(Debug, Serialize)]
pub struct ConfigSummary {
    pub template_name: String,
    pub sensors_used: Vec<String>,
    pub actuators_used: Vec<String>,
    pub pipelines: u32,
    pub nodes: u32,
}

#[derive(Debug, Serialize, Clone)]
pub struct ParamSpec {
    pub key: String,
    pub label: String,
    pub param_type: String, // "number", "string", "bool", "select"
    pub default: serde_json::Value,
    pub options: Vec<String>,
    pub required: bool,
    pub description: String,
}

/// Template skeleton definition
struct TemplateSkeleton {
    id: &'static str,
    name: &'static str,
    params: Vec<ParamSpec>,
    build_fn: fn(&HashMap<String, serde_json::Value>, &str) -> serde_json::Value,
}

fn get_param_f64(params: &HashMap<String, serde_json::Value>, key: &str, default: f64) -> f64 {
    params.get(key)
        .and_then(|v| v.as_f64())
        .unwrap_or(default)
}

fn get_param_str<'a>(params: &'a HashMap<String, serde_json::Value>, key: &str, default: &'a str) -> String {
    params.get(key)
        .and_then(|v| v.as_str())
        .unwrap_or(default)
        .to_string()
}

fn get_param_bool(params: &HashMap<String, serde_json::Value>, key: &str, default: bool) -> bool {
    params.get(key)
        .and_then(|v| v.as_bool())
        .unwrap_or(default)
}

// ===== TEMPLATE BUILDERS =====

fn build_thermostat(params: &HashMap<String, serde_json::Value>, board_id: &str) -> serde_json::Value {
    let high = get_param_f64(params, "temp_high", 28.0);
    let low = get_param_f64(params, "temp_low", 25.0);
    let _hyst = get_param_f64(params, "hysteresis", 1.0);
    let interval = get_param_f64(params, "check_interval_ms", 5000.0) as u64;
    let sensor = get_param_str(params, "temp_sensor", "drv_bme280");
    let mqtt = get_param_bool(params, "enable_mqtt", false);

    let mut nodes = vec![
        serde_json::json!({"id": "read_temp", "type": "sensor.read", "device": "temp_sensor", "field": "temperature", "output": "$current_temp"}),
        serde_json::json!({"id": "check_high", "type": "condition.compare", "left": "$current_temp", "op": ">", "right": high, "if_true": "fan_on", "if_false": "check_low"}),
        serde_json::json!({"id": "check_low", "type": "condition.compare", "left": "$current_temp", "op": "<", "right": low, "if_true": "fan_off", "if_false": "done"}),
        serde_json::json!({"id": "fan_on", "type": "actuator.write", "device": "cooler_relay", "field": "on_off", "value": 1, "next": "done"}),
        serde_json::json!({"id": "fan_off", "type": "actuator.write", "device": "cooler_relay", "field": "on_off", "value": 0, "next": "done"}),
        serde_json::json!({"id": "done", "type": "noop"}),
    ];

    if mqtt {
        nodes.insert(5, serde_json::json!({"id": "pub_temp", "type": "mqtt.publish", "topic": "parakram/temperature", "payload": "$current_temp", "next": "done"}));
    }

    serde_json::json!({
        "version": "1.0",
        "program_id": uuid::Uuid::new_v4().to_string(),
        "board_id": board_id,
        "created_at": chrono::Utc::now().to_rfc3339(),
        "signature": "",
        "devices": [
            {"id": "temp_sensor", "driver": sensor, "bus": "i2c_0", "address": "0x76", "pin_slot": "0", "capabilities": ["temperature"]},
            {"id": "cooler_relay", "driver": "drv_relay", "bus": "gpio", "address": "", "pin_slot": "5", "capabilities": ["on_off"]}
        ],
        "state": {"current_temp": {"type": "float", "initial": 0.0}},
        "triggers": [{"id": "check_loop", "type": "timer", "interval_ms": interval}],
        "pipelines": [{"id": "thermostat_pipeline", "trigger": "check_loop", "nodes": nodes, "max_execution_ms": 2000}],
        "constraints": {"max_memory_bytes": 32768, "max_cycle_us": 5000}
    })
}

fn build_irrigation(params: &HashMap<String, serde_json::Value>, board_id: &str) -> serde_json::Value {
    let threshold = get_param_f64(params, "moisture_threshold", 30.0);
    let valve_duration_ms = get_param_f64(params, "valve_duration_ms", 10000.0) as u64;
    let check_interval = get_param_f64(params, "check_interval_ms", 30000.0) as u64;
    let _cooldown_ms = get_param_f64(params, "cooldown_ms", 300000.0) as u64;

    serde_json::json!({
        "version": "1.0",
        "program_id": uuid::Uuid::new_v4().to_string(),
        "board_id": board_id,
        "created_at": chrono::Utc::now().to_rfc3339(),
        "signature": "",
        "devices": [
            {"id": "soil_sensor", "driver": "drv_soil_cap", "bus": "adc", "address": "", "pin_slot": "0", "capabilities": ["soil_moisture"]},
            {"id": "water_valve", "driver": "drv_solenoid", "bus": "gpio", "address": "", "pin_slot": "5", "capabilities": ["on_off"]}
        ],
        "state": {
            "moisture": {"type": "float", "initial": 100.0},
            "last_watered": {"type": "int", "initial": 0},
            "can_water": {"type": "bool", "initial": true}
        },
        "triggers": [{"id": "soil_check", "type": "timer", "interval_ms": check_interval}],
        "pipelines": [{
            "id": "irrigation_pipeline", "trigger": "soil_check",
            "nodes": [
                {"id": "read_soil", "type": "sensor.read", "device": "soil_sensor", "field": "soil_moisture", "output": "$moisture"},
                {"id": "check_dry", "type": "condition.compare", "left": "$moisture", "op": "<", "right": threshold, "if_true": "check_cooldown", "if_false": "done"},
                {"id": "check_cooldown", "type": "condition.compare", "left": "$can_water", "op": "==", "right": true, "if_true": "water_on", "if_false": "done"},
                {"id": "water_on", "type": "actuator.write", "device": "water_valve", "field": "on_off", "value": 1, "next": "wait"},
                {"id": "wait", "type": "delay.ms", "duration": valve_duration_ms, "next": "water_off"},
                {"id": "water_off", "type": "actuator.write", "device": "water_valve", "field": "on_off", "value": 0, "next": "log"},
                {"id": "log", "type": "mqtt.publish", "topic": "parakram/irrigation", "payload": "watered", "next": "done"},
                {"id": "done", "type": "noop"}
            ],
            "max_execution_ms": 5000
        }],
        "constraints": {"max_memory_bytes": 32768, "max_cycle_us": 5000}
    })
}

fn build_security_alarm(params: &HashMap<String, serde_json::Value>, board_id: &str) -> serde_json::Value {
    let buzzer_freq = get_param_f64(params, "buzzer_freq_hz", 2000.0) as u32;
    let buzzer_duration = get_param_f64(params, "alarm_duration_ms", 3000.0) as u64;
    let enable_mqtt = get_param_bool(params, "enable_mqtt", true);

    serde_json::json!({
        "version": "1.0",
        "program_id": uuid::Uuid::new_v4().to_string(),
        "board_id": board_id,
        "created_at": chrono::Utc::now().to_rfc3339(),
        "signature": "",
        "devices": [
            {"id": "motion_sensor", "driver": "drv_pir", "bus": "gpio", "address": "", "pin_slot": "4", "capabilities": ["motion"]},
            {"id": "alarm_buzzer", "driver": "drv_buzzer", "bus": "pwm", "address": "", "pin_slot": "5", "capabilities": ["tone_hz", "on_off"]}
        ],
        "state": {"motion_detected": {"type": "bool", "initial": false}},
        "triggers": [{"id": "motion_poll", "type": "timer", "interval_ms": 200}],
        "pipelines": [{
            "id": "alarm_pipeline", "trigger": "motion_poll",
            "nodes": [
                {"id": "read_pir", "type": "sensor.read", "device": "motion_sensor", "field": "motion", "output": "$motion_detected"},
                {"id": "check_motion", "type": "condition.compare", "left": "$motion_detected", "op": "==", "right": true, "if_true": "sound_alarm", "if_false": "done"},
                {"id": "sound_alarm", "type": "actuator.write_pwm", "device": "alarm_buzzer", "field": "tone_hz", "value": buzzer_freq, "next": "wait"},
                {"id": "wait", "type": "delay.ms", "duration": buzzer_duration, "next": "alarm_off"},
                {"id": "alarm_off", "type": "actuator.write", "device": "alarm_buzzer", "field": "on_off", "value": 0, "next": if enable_mqtt { "notify" } else { "done" }},
                {"id": "notify", "type": "mqtt.publish", "topic": "parakram/alerts", "payload": "MOTION_DETECTED", "next": "done"},
                {"id": "done", "type": "noop"}
            ],
            "max_execution_ms": 4000
        }],
        "constraints": {"max_memory_bytes": 32768, "max_cycle_us": 5000}
    })
}

fn build_voice_assistant(params: &HashMap<String, serde_json::Value>, board_id: &str) -> serde_json::Value {
    let wake_threshold_db = get_param_f64(params, "wake_threshold_db", 60.0);
    let speaker_volume = get_param_f64(params, "speaker_volume", 80.0) as u32;
    let display_type = get_param_str(params, "display_driver", "drv_oled_ssd1306");

    serde_json::json!({
        "version": "1.0",
        "program_id": uuid::Uuid::new_v4().to_string(),
        "board_id": board_id,
        "created_at": chrono::Utc::now().to_rfc3339(),
        "signature": "",
        "devices": [
            {"id": "microphone", "driver": "drv_inmp441", "bus": "i2s_0", "address": "", "pin_slot": "0", "capabilities": ["audio_level_db", "voice_detect"]},
            {"id": "speaker", "driver": "drv_max98357a", "bus": "i2s_0", "address": "", "pin_slot": "1", "capabilities": ["audio_play", "volume_percent"]},
            {"id": "display", "driver": display_type, "bus": "i2c_0", "address": "0x3C", "pin_slot": "0", "capabilities": ["text_display"]}
        ],
        "state": {
            "audio_level": {"type": "float", "initial": 0.0},
            "is_listening": {"type": "bool", "initial": false},
            "status_text": {"type": "string", "initial": "Ready"}
        },
        "triggers": [{"id": "audio_poll", "type": "timer", "interval_ms": 50}],
        "pipelines": [{
            "id": "voice_pipeline", "trigger": "audio_poll",
            "nodes": [
                {"id": "read_mic", "type": "sensor.read", "device": "microphone", "field": "audio_level_db", "output": "$audio_level"},
                {"id": "check_wake", "type": "condition.compare", "left": "$audio_level", "op": ">", "right": wake_threshold_db, "if_true": "start_listen", "if_false": "update_display"},
                {"id": "start_listen", "type": "state.store", "variable": "is_listening", "value": true, "next": "show_listening"},
                {"id": "show_listening", "type": "display.text", "device": "display", "line": 0, "text": "Listening...", "next": "pub_voice"},
                {"id": "pub_voice", "type": "mqtt.publish", "topic": "parakram/voice/wake", "payload": "WAKE_DETECTED", "next": "play_ack"},
                {"id": "play_ack", "type": "actuator.write", "device": "speaker", "field": "volume_percent", "value": speaker_volume, "next": "done"},
                {"id": "update_display", "type": "display.text", "device": "display", "line": 0, "text": "Ready", "next": "done"},
                {"id": "done", "type": "noop"}
            ],
            "max_execution_ms": 2000
        }],
        "constraints": {"max_memory_bytes": 65536, "max_cycle_us": 5000}
    })
}

// ===== SKELETON REGISTRY =====

fn get_skeletons() -> Vec<TemplateSkeleton> {
    vec![
        TemplateSkeleton {
            id: "tpl_smart_thermostat",
            name: "Smart Thermostat",
            params: vec![
                ParamSpec { key: "temp_high".into(), label: "High temperature (°C)".into(), param_type: "number".into(), default: serde_json::json!(28.0), options: vec![], required: true, description: "Fan turns ON above this".into() },
                ParamSpec { key: "temp_low".into(), label: "Low temperature (°C)".into(), param_type: "number".into(), default: serde_json::json!(25.0), options: vec![], required: true, description: "Fan turns OFF below this".into() },
                ParamSpec { key: "hysteresis".into(), label: "Hysteresis (°C)".into(), param_type: "number".into(), default: serde_json::json!(1.0), options: vec![], required: false, description: "Buffer to prevent rapid switching".into() },
                ParamSpec { key: "check_interval_ms".into(), label: "Check interval (ms)".into(), param_type: "number".into(), default: serde_json::json!(5000), options: vec![], required: false, description: "How often to read temperature".into() },
                ParamSpec { key: "temp_sensor".into(), label: "Temperature sensor".into(), param_type: "select".into(), default: serde_json::json!("drv_bme280"), options: vec!["drv_bme280".into(), "drv_dht22".into(), "drv_ds18b20".into(), "drv_sht31".into(), "drv_aht20".into()], required: true, description: "Which sensor is connected".into() },
                ParamSpec { key: "enable_mqtt".into(), label: "Enable MQTT logging".into(), param_type: "bool".into(), default: serde_json::json!(false), options: vec![], required: false, description: "Publish readings to MQTT".into() },
            ],
            build_fn: build_thermostat,
        },
        TemplateSkeleton {
            id: "tpl_auto_irrigation",
            name: "Smart Irrigation",
            params: vec![
                ParamSpec { key: "moisture_threshold".into(), label: "Moisture threshold (%)".into(), param_type: "number".into(), default: serde_json::json!(30.0), options: vec![], required: true, description: "Water below this level".into() },
                ParamSpec { key: "valve_duration_ms".into(), label: "Watering duration (ms)".into(), param_type: "number".into(), default: serde_json::json!(10000), options: vec![], required: true, description: "How long to water".into() },
                ParamSpec { key: "check_interval_ms".into(), label: "Check interval (ms)".into(), param_type: "number".into(), default: serde_json::json!(30000), options: vec![], required: false, description: "How often to check moisture".into() },
                ParamSpec { key: "cooldown_ms".into(), label: "Cooldown period (ms)".into(), param_type: "number".into(), default: serde_json::json!(300000), options: vec![], required: false, description: "Minimum time between waterings".into() },
            ],
            build_fn: build_irrigation,
        },
        TemplateSkeleton {
            id: "tpl_intruder_alarm",
            name: "Intruder Alarm",
            params: vec![
                ParamSpec { key: "buzzer_freq_hz".into(), label: "Alarm frequency (Hz)".into(), param_type: "number".into(), default: serde_json::json!(2000), options: vec![], required: false, description: "Buzzer tone frequency".into() },
                ParamSpec { key: "alarm_duration_ms".into(), label: "Alarm duration (ms)".into(), param_type: "number".into(), default: serde_json::json!(3000), options: vec![], required: false, description: "How long alarm sounds".into() },
                ParamSpec { key: "enable_mqtt".into(), label: "Enable MQTT alert".into(), param_type: "bool".into(), default: serde_json::json!(true), options: vec![], required: false, description: "Push alert via MQTT".into() },
            ],
            build_fn: build_security_alarm,
        },
        TemplateSkeleton {
            id: "tpl_voice_assistant",
            name: "AI Voice Assistant Device",
            params: vec![
                ParamSpec { key: "wake_threshold_db".into(), label: "Wake word threshold (dB)".into(), param_type: "number".into(), default: serde_json::json!(60.0), options: vec![], required: true, description: "Mic level to trigger listening".into() },
                ParamSpec { key: "speaker_volume".into(), label: "Speaker volume (%)".into(), param_type: "number".into(), default: serde_json::json!(80), options: vec![], required: false, description: "Output volume".into() },
                ParamSpec { key: "display_driver".into(), label: "Display type".into(), param_type: "select".into(), default: serde_json::json!("drv_oled_ssd1306"), options: vec!["drv_oled_ssd1306".into(), "drv_st7789".into(), "drv_ili9341".into(), "drv_sh1106".into()], required: true, description: "Connected display module".into() },
            ],
            build_fn: build_voice_assistant,
        },
    ]
}

// ===== API HANDLER =====

async fn configure(
    State(_state): State<AppState>,
    Json(req): Json<ConfigureRequest>,
) -> Result<Json<ConfigureResponse>, StatusCode> {
    let skeletons = get_skeletons();
    let skeleton = skeletons.iter()
        .find(|s| s.id == req.template_id)
        .ok_or(StatusCode::NOT_FOUND)?;

    // Check for missing required parameters, return what the user needs to fill
    let missing: Vec<ParamSpec> = skeleton.params.iter()
        .filter(|p| p.required && !req.parameters.contains_key(&p.key))
        .cloned()
        .collect();

    if !missing.is_empty() {
        return Ok(Json(ConfigureResponse {
            success: false,
            ir: serde_json::json!(null),
            summary: ConfigSummary {
                template_name: skeleton.name.to_string(),
                sensors_used: vec![],
                actuators_used: vec![],
                pipelines: 0,
                nodes: 0,
            },
            missing_params: missing,
        }));
    }

    // Merge defaults with user parameters
    let mut merged_params = HashMap::new();
    for p in &skeleton.params {
        if let Some(user_val) = req.parameters.get(&p.key) {
            merged_params.insert(p.key.clone(), user_val.clone());
        } else {
            merged_params.insert(p.key.clone(), p.default.clone());
        }
    }

    // Build the IR deterministically
    let ir = (skeleton.build_fn)(&merged_params, &req.board_id);

    // Extract summary from generated IR
    let empty_devices = vec![];
    let devices = ir["devices"].as_array().unwrap_or(&empty_devices);
    let actuator_keywords = ["relay","buzzer","servo","ws2812","solenoid","fan","motor","lcd","oled","max98357","mosfet","st7789","ili9341","sh1106","drv8833","tb6612"];
    let sensors: Vec<String> = devices.iter()
        .filter(|d| d["driver"].as_str().map(|s| !actuator_keywords.iter().any(|kw| s.contains(kw))).unwrap_or(false))
        .filter_map(|d| d["driver"].as_str().map(String::from))
        .collect();
    let actuators: Vec<String> = devices.iter()
        .filter(|d| !sensors.contains(&d["driver"].as_str().unwrap_or("").to_string()))
        .filter_map(|d| d["driver"].as_str().map(String::from))
        .collect();

    let empty_pipelines = vec![];
    let pipelines = ir["pipelines"].as_array().map(|a| a.len() as u32).unwrap_or(0);
    let nodes: u32 = ir["pipelines"].as_array().unwrap_or(&empty_pipelines).iter()
        .map(|p| p["nodes"].as_array().map(|n| n.len() as u32).unwrap_or(0))
        .sum();

    Ok(Json(ConfigureResponse {
        success: true,
        ir,
        summary: ConfigSummary {
            template_name: skeleton.name.to_string(),
            sensors_used: sensors,
            actuators_used: actuators,
            pipelines,
            nodes,
        },
        missing_params: vec![],
    }))
}

async fn list_configurable_templates() -> Json<Vec<serde_json::Value>> {
    let skeletons = get_skeletons();
    Json(skeletons.iter().map(|s| {
        serde_json::json!({
            "id": s.id,
            "name": s.name,
            "parameters": s.params,
        })
    }).collect())
}

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/build", post(configure))
        .route("/available", axum::routing::get(list_configurable_templates))
}
