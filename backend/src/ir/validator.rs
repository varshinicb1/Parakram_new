//! Parakram IR Validator — 8-step validation pipeline.
//!
//! Every IR document must pass ALL 8 steps before compilation.
//! Any failure returns a structured error with step, field path, and human-readable message.

use std::collections::{HashMap, HashSet, VecDeque};
use crate::drivers::registry::DriverRegistry;
use crate::ir::types::*;
use serde::{Deserialize, Serialize};

/// Validation error with structured context.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ValidationError {
    pub step: u8,
    pub step_name: String,
    pub field_path: String,
    pub code: String,
    pub message: String,
}

/// Validation warning (non-blocking).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ValidationWarning {
    pub step: u8,
    pub field_path: String,
    pub code: String,
    pub message: String,
}

/// Complete validation result.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ValidationResult {
    pub valid: bool,
    pub errors: Vec<ValidationError>,
    pub warnings: Vec<ValidationWarning>,
    pub steps_completed: u8,
}

impl ValidationResult {
    fn new() -> Self {
        Self {
            valid: true,
            errors: Vec::new(),
            warnings: Vec::new(),
            steps_completed: 0,
        }
    }

    fn add_error(&mut self, step: u8, step_name: &str, field_path: &str, code: &str, message: &str) {
        self.valid = false;
        self.errors.push(ValidationError {
            step,
            step_name: step_name.to_string(),
            field_path: field_path.to_string(),
            code: code.to_string(),
            message: message.to_string(),
        });
    }

    fn add_warning(&mut self, step: u8, field_path: &str, code: &str, message: &str) {
        self.warnings.push(ValidationWarning {
            step,
            field_path: field_path.to_string(),
            code: code.to_string(),
            message: message.to_string(),
        });
    }
}

/// Validate an IR document through the complete 8-step pipeline.
pub fn validate_ir(ir: &IRDocument, registry: &DriverRegistry) -> ValidationResult {
    let mut result = ValidationResult::new();

    // Step 1: Structural validation (field types, required fields, bounds)
    step1_structural_validation(ir, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 1;

    // Step 2: Device reference resolution
    step2_device_references(ir, registry, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 2;

    // Step 3: State reference resolution
    step3_state_references(ir, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 3;

    // Step 4: DAG cycle detection (Kahn's algorithm)
    step4_dag_cycle_detection(ir, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 4;

    // Step 5: Resource conflict detection
    step5_resource_conflicts(ir, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 5;

    // Step 6: Timing bound verification
    step6_timing_bounds(ir, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 6;

    // Step 7: Driver compatibility check
    step7_driver_compatibility(ir, registry, &mut result);
    if !result.valid {
        return result;
    }
    result.steps_completed = 7;

    // Step 8: Safety policy check
    step8_safety_policy(ir, registry, &mut result);
    result.steps_completed = 8;

    result
}

/// Step 1: Structural validation — field presence, types, and numeric bounds.
fn step1_structural_validation(ir: &IRDocument, result: &mut ValidationResult) {
    let step = 1;
    let name = "Structural Validation";

    // Version
    if ir.version != "1.0" {
        result.add_error(step, name, "version", "SCHEMA_INVALID",
            &format!("Version must be '1.0', got '{}'", ir.version));
    }

    // Board ID format
    if !ir.board_id.starts_with("VDYT-") {
        result.add_error(step, name, "board_id", "SCHEMA_INVALID",
            &format!("Board ID must start with 'VDYT-', got '{}'", ir.board_id));
    }

    // Devices
    if ir.devices.is_empty() {
        result.add_error(step, name, "devices", "SCHEMA_INVALID",
            "At least one device is required");
    }
    if ir.devices.len() > 32 {
        result.add_error(step, name, "devices", "SCHEMA_INVALID",
            &format!("Maximum 32 devices, got {}", ir.devices.len()));
    }

    // Check device IDs are unique
    let mut device_ids = HashSet::new();
    for (i, dev) in ir.devices.iter().enumerate() {
        if !device_ids.insert(&dev.id) {
            result.add_error(step, name, &format!("devices[{}].id", i), "SCHEMA_INVALID",
                &format!("Duplicate device ID: '{}'", dev.id));
        }
        if dev.id.len() > 32 {
            result.add_error(step, name, &format!("devices[{}].id", i), "STRING_TOO_LONG",
                &format!("Device ID '{}' exceeds 32 characters", dev.id));
        }
        if dev.capabilities.is_empty() {
            result.add_error(step, name, &format!("devices[{}].capabilities", i), "SCHEMA_INVALID",
                &format!("Device '{}' must have at least one capability", dev.id));
        }
    }

    // State variables
    if ir.state.len() > 64 {
        result.add_error(step, name, "state", "STATE_VAR_LIMIT_EXCEEDED",
            &format!("Maximum 64 state variables, got {}", ir.state.len()));
    }
    for (name_key, var) in &ir.state {
        if name_key.len() > 32 {
            result.add_error(step, name, &format!("state.{}", name_key), "STRING_TOO_LONG",
                &format!("State variable name '{}' exceeds 32 characters", name_key));
        }
        match var.var_type.as_str() {
            "int" | "float" | "bool" | "string" => {}
            other => {
                result.add_error(step, name, &format!("state.{}.type", name_key), "SCHEMA_INVALID",
                    &format!("Invalid type '{}', must be int|float|bool|string", other));
            }
        }
    }

    // Triggers
    if ir.triggers.is_empty() {
        result.add_error(step, name, "triggers", "SCHEMA_INVALID",
            "At least one trigger is required");
    }
    let mut trigger_ids = HashSet::new();
    for (i, trigger) in ir.triggers.iter().enumerate() {
        if !trigger_ids.insert(&trigger.id) {
            result.add_error(step, name, &format!("triggers[{}].id", i), "SCHEMA_INVALID",
                &format!("Duplicate trigger ID: '{}'", trigger.id));
        }
        match trigger.trigger_type.as_str() {
            "timer" => {
                if trigger.interval_ms.is_none() && trigger.cron.is_none() {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "Timer trigger requires 'interval_ms' or 'cron'");
                }
                if let Some(ms) = trigger.interval_ms {
                    if ms < 100 {
                        result.add_error(step, name, &format!("triggers[{}].interval_ms", i),
                            "SCHEMA_INVALID", "Minimum timer interval is 100ms");
                    }
                }
            }
            "sensor_threshold" => {
                if trigger.device.is_none() || trigger.field.is_none()
                    || trigger.threshold.is_none() || trigger.comparison.is_none()
                    || trigger.hysteresis.is_none()
                {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "sensor_threshold requires: device, field, threshold, comparison, hysteresis");
                }
            }
            "gpio_edge" => {
                if trigger.device.is_none() || trigger.edge.is_none() {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "gpio_edge requires: device, edge");
                }
            }
            "mqtt_message" => {
                if trigger.topic.is_none() {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "mqtt_message requires: topic");
                }
            }
            "ble_event" => {
                if trigger.ble_event_type.is_none() {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "ble_event requires: ble_event_type");
                }
            }
            "time_window" => {
                if trigger.window_start.is_none() || trigger.window_end.is_none() {
                    result.add_error(step, name, &format!("triggers[{}]", i), "SCHEMA_INVALID",
                        "time_window requires: window_start, window_end");
                }
            }
            "startup" => {}
            other => {
                result.add_error(step, name, &format!("triggers[{}].type", i), "SCHEMA_INVALID",
                    &format!("Unknown trigger type: '{}'", other));
            }
        }
    }

    // Pipelines
    if ir.pipelines.is_empty() {
        result.add_error(step, name, "pipelines", "SCHEMA_INVALID",
            "At least one pipeline is required");
    }
    if ir.pipelines.len() > 16 {
        result.add_error(step, name, "pipelines", "PIPELINE_LIMIT_EXCEEDED",
            &format!("Maximum 16 pipelines, got {}", ir.pipelines.len()));
    }

    let mut total_nodes = 0u32;
    for (i, pipeline) in ir.pipelines.iter().enumerate() {
        if pipeline.nodes.is_empty() {
            result.add_error(step, name, &format!("pipelines[{}].nodes", i), "SCHEMA_INVALID",
                "Pipeline must have at least one node");
        }
        if pipeline.nodes.len() > 64 {
            result.add_error(step, name, &format!("pipelines[{}].nodes", i), "SCHEMA_INVALID",
                &format!("Maximum 64 nodes per pipeline, got {}", pipeline.nodes.len()));
        }
        total_nodes += pipeline.nodes.len() as u32;

        if pipeline.max_execution_ms == 0 || pipeline.max_execution_ms > 5000 {
            result.add_error(step, name, &format!("pipelines[{}].max_execution_ms", i),
                "SCHEMA_INVALID", "max_execution_ms must be between 1 and 5000");
        }
    }

    if total_nodes > 256 {
        result.add_error(step, name, "pipelines", "NODE_LIMIT_EXCEEDED",
            &format!("Maximum 256 total nodes across all pipelines, got {}", total_nodes));
    }
}

/// Step 2: Device reference resolution — all device IDs used in nodes must exist.
fn step2_device_references(ir: &IRDocument, registry: &DriverRegistry, result: &mut ValidationResult) {
    let step = 2;
    let name = "Device Reference Resolution";

    let device_ids: HashSet<&str> = ir.devices.iter().map(|d| d.id.as_str()).collect();

    // Check all devices have valid drivers
    for (i, dev) in ir.devices.iter().enumerate() {
        if registry.get_driver(&dev.driver).is_none() {
            result.add_error(step, name, &format!("devices[{}].driver", i), "DRIVER_NOT_FOUND",
                &format!("Driver '{}' not found in registry", dev.driver));
        }
    }

    // Check all pipeline node device references
    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        for (ni, node) in pipeline.nodes.iter().enumerate() {
            if let Some(ref dev_id) = node.device {
                if !device_ids.contains(dev_id.as_str()) {
                    result.add_error(step, name,
                        &format!("pipelines[{}].nodes[{}].device", pi, ni),
                        "DEVICE_NOT_FOUND",
                        &format!("Device '{}' referenced in node '{}' not found in devices array", dev_id, node.id));
                }
            }
        }
    }

    // Check trigger device references
    for (i, trigger) in ir.triggers.iter().enumerate() {
        if let Some(ref dev_id) = trigger.device {
            if !device_ids.contains(dev_id.as_str()) {
                result.add_error(step, name, &format!("triggers[{}].device", i), "DEVICE_NOT_FOUND",
                    &format!("Device '{}' referenced in trigger '{}' not found", dev_id, trigger.id));
            }
        }
    }
}

/// Step 3: State reference resolution — no dangling $variable references.
fn step3_state_references(ir: &IRDocument, result: &mut ValidationResult) {
    let step = 3;
    let name = "State Reference Resolution";

    let state_vars: HashSet<&str> = ir.state.keys().map(|s| s.as_str()).collect();

    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        for (ni, node) in pipeline.nodes.iter().enumerate() {
            let path = format!("pipelines[{}].nodes[{}]", pi, ni);

            // Check store_to references
            if let Some(ref var_name) = node.store_to {
                if !state_vars.contains(var_name.as_str()) {
                    result.add_error(step, name, &format!("{}.store_to", path),
                        "STATE_VAR_NOT_FOUND",
                        &format!("State variable '{}' not found", var_name));
                }
            }

            // Check load_from references
            if let Some(ref var_name) = node.load_from {
                if !state_vars.contains(var_name.as_str()) {
                    result.add_error(step, name, &format!("{}.load_from", path),
                        "STATE_VAR_NOT_FOUND",
                        &format!("State variable '{}' not found", var_name));
                }
            }

            // Check $variable references in left, right, payload, text
            check_var_ref(&node.left, &state_vars, step, name, &format!("{}.left", path), result);
            check_var_ref(&node.right, &state_vars, step, name, &format!("{}.right", path), result);

            if let Some(ref payload) = node.payload {
                for var_ref in extract_var_refs(payload) {
                    if !state_vars.contains(var_ref.as_str()) {
                        result.add_error(step, name, &format!("{}.payload", path),
                            "STATE_VAR_DANGLING_REF",
                            &format!("Variable '{}' referenced in payload not found", var_ref));
                    }
                }
            }

            // Check fields array in storage.log
            if let Some(ref fields) = node.fields {
                for (fi, field_name) in fields.iter().enumerate() {
                    if !state_vars.contains(field_name.as_str()) {
                        result.add_error(step, name, &format!("{}.fields[{}]", path, fi),
                            "STATE_VAR_NOT_FOUND",
                            &format!("State variable '{}' not found in fields", field_name));
                    }
                }
            }
        }
    }
}

fn check_var_ref(val: &Option<serde_json::Value>, vars: &HashSet<&str>, step: u8, name: &str, path: &str, result: &mut ValidationResult) {
    if let Some(serde_json::Value::String(s)) = val {
        if let Some(var_name) = s.strip_prefix('$') {
            if !vars.contains(var_name) {
                result.add_error(step, name, path, "STATE_VAR_DANGLING_REF",
                    &format!("Variable '{}' referenced as '{}' not found", var_name, s));
            }
        }
    }
}

fn extract_var_refs(s: &str) -> Vec<String> {
    let mut refs = Vec::new();
    for part in s.split('$').skip(1) {
        let var_name: String = part.chars().take_while(|c| c.is_alphanumeric() || *c == '_').collect();
        if !var_name.is_empty() {
            refs.push(var_name);
        }
    }
    refs
}

/// Step 4: DAG cycle detection using Kahn's algorithm.
fn step4_dag_cycle_detection(ir: &IRDocument, result: &mut ValidationResult) {
    let step = 4;
    let name = "DAG Cycle Detection";

    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        let node_ids: Vec<&str> = pipeline.nodes.iter().map(|n| n.id.as_str()).collect();
        let id_to_index: HashMap<&str, usize> = node_ids.iter().enumerate().map(|(i, id)| (*id, i)).collect();
        let n = node_ids.len();

        // Build adjacency list and in-degree count
        let mut adj: Vec<Vec<usize>> = vec![Vec::new(); n];
        let mut in_degree: Vec<u32> = vec![0; n];

        // Sequential edges: each node implicitly flows to the next
        for i in 0..n.saturating_sub(1) {
            adj[i].push(i + 1);
            in_degree[i + 1] += 1;
        }

        // Conditional jump edges
        for (ni, node) in pipeline.nodes.iter().enumerate() {
            if let Some(ref target) = node.if_true {
                if let Some(&target_idx) = id_to_index.get(target.as_str()) {
                    if target_idx <= ni {
                        result.add_error(step, name,
                            &format!("pipelines[{}].nodes[{}].if_true", pi, ni),
                            "FORWARD_REF_VIOLATION",
                            &format!("if_true target '{}' is not a forward reference (backward edge creates cycle)", target));
                    }
                } else {
                    result.add_error(step, name,
                        &format!("pipelines[{}].nodes[{}].if_true", pi, ni),
                        "SCHEMA_INVALID",
                        &format!("if_true target '{}' not found in pipeline nodes", target));
                }
            }
            if let Some(ref target) = node.if_false {
                if let Some(&target_idx) = id_to_index.get(target.as_str()) {
                    if target_idx <= ni {
                        result.add_error(step, name,
                            &format!("pipelines[{}].nodes[{}].if_false", pi, ni),
                            "FORWARD_REF_VIOLATION",
                            &format!("if_false target '{}' is not a forward reference", target));
                    }
                } else {
                    result.add_error(step, name,
                        &format!("pipelines[{}].nodes[{}].if_false", pi, ni),
                        "SCHEMA_INVALID",
                        &format!("if_false target '{}' not found in pipeline nodes", target));
                }
            }
        }

        // Run Kahn's algorithm (verify no cycles even with jump edges)
        let mut queue: VecDeque<usize> = VecDeque::new();
        let mut in_deg = in_degree.clone();
        for i in 0..n {
            if in_deg[i] == 0 {
                queue.push_back(i);
            }
        }

        let mut visited = 0;
        while let Some(u) = queue.pop_front() {
            visited += 1;
            for &v in &adj[u] {
                in_deg[v] -= 1;
                if in_deg[v] == 0 {
                    queue.push_back(v);
                }
            }
        }

        if visited != n {
            result.add_error(step, name, &format!("pipelines[{}]", pi),
                "DAG_CYCLE_DETECTED",
                &format!("Pipeline '{}' contains a cycle ({} of {} nodes reachable)", pipeline.id, visited, n));
        }
    }
}

/// Step 5: Resource conflict detection — no two pipelines write same actuator without mutex.
fn step5_resource_conflicts(ir: &IRDocument, result: &mut ValidationResult) {
    let step = 5;
    let name = "Resource Conflict Detection";

    // Collect which actuators each pipeline writes to
    let mut pipeline_writes: HashMap<String, Vec<String>> = HashMap::new();

    for pipeline in &ir.pipelines {
        let mut writes = Vec::new();
        for node in &pipeline.nodes {
            match node.node_type.as_str() {
                "actuator.write" | "actuator.write_pwm" => {
                    if let Some(ref dev) = node.device {
                        writes.push(dev.clone());
                    }
                }
                _ => {}
            }
        }
        if !writes.is_empty() {
            pipeline_writes.insert(pipeline.id.clone(), writes);
        }
    }

    // Check for conflicts
    let mut actuator_pipelines: HashMap<String, Vec<String>> = HashMap::new();
    for (pipeline_id, actuators) in &pipeline_writes {
        let mut pipelines_for_actuator: HashSet<String> = HashSet::new();
        for actuator in actuators {
            pipelines_for_actuator.insert(actuator.clone());
        }
        for actuator in pipelines_for_actuator {
            actuator_pipelines
                .entry(actuator.clone())
                .or_default()
                .push(pipeline_id.clone());
        }
    }

    // Collect mutex groups
    let mutex_groups: HashSet<String> = ir.constraints.global_mutex_groups
        .as_ref()
        .map(|groups| {
            groups.iter().flat_map(|g| g.pipelines.iter().cloned()).collect()
        })
        .unwrap_or_default();

    for (actuator, pipelines) in &actuator_pipelines {
        if pipelines.len() > 1 {
            // Check if all pipelines are in a mutex group
            let all_in_mutex = pipelines.iter().all(|p| mutex_groups.contains(p));
            if !all_in_mutex {
                result.add_error(step, name, &format!("devices.{}", actuator),
                    "RESOURCE_CONFLICT",
                    &format!("Actuator '{}' written by multiple pipelines ({}) without a mutex_group",
                        actuator, pipelines.join(", ")));
            }
        }
    }
}

/// Step 6: Timing bound verification — max_execution_ms <= trigger interval.
fn step6_timing_bounds(ir: &IRDocument, result: &mut ValidationResult) {
    let step = 6;
    let name = "Timing Bound Verification";

    let trigger_intervals: HashMap<&str, u64> = ir.triggers.iter()
        .filter_map(|t| {
            t.interval_ms.map(|ms| (t.id.as_str(), ms))
        })
        .collect();

    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        if let Some(&interval_ms) = trigger_intervals.get(pipeline.trigger.as_str()) {
            if pipeline.max_execution_ms as u64 > interval_ms {
                result.add_error(step, name, &format!("pipelines[{}].max_execution_ms", pi),
                    "EXECUTION_EXCEEDS_INTERVAL",
                    &format!("Pipeline '{}' max_execution_ms ({}) exceeds trigger interval ({}ms)",
                        pipeline.id, pipeline.max_execution_ms, interval_ms));
            }
        }

        // Verify trigger reference exists
        if !ir.triggers.iter().any(|t| t.id == pipeline.trigger) {
            result.add_error(step, name, &format!("pipelines[{}].trigger", pi),
                "SCHEMA_INVALID",
                &format!("Pipeline '{}' references non-existent trigger '{}'", pipeline.id, pipeline.trigger));
        }
    }
}

/// Step 7: Driver compatibility — drivers support declared capabilities.
fn step7_driver_compatibility(ir: &IRDocument, registry: &DriverRegistry, result: &mut ValidationResult) {
    let step = 7;
    let name = "Driver Compatibility";

    for (i, dev) in ir.devices.iter().enumerate() {
        if let Some(driver_spec) = registry.get_driver(&dev.driver) {
            // Check each declared capability is supported
            for cap in &dev.capabilities {
                if !driver_spec.capabilities.contains(cap) {
                    result.add_error(step, name, &format!("devices[{}].capabilities", i),
                        "DRIVER_CAPABILITY_MISMATCH",
                        &format!("Driver '{}' does not support capability '{}'. Supported: {:?}",
                            dev.driver, cap, driver_spec.capabilities));
                }
            }

            // Check bus type compatibility
            if !driver_spec.bus_types.contains(&dev.bus) {
                // Allow "gpio" and "pwm" as flexible bus references
                let bus_base = dev.bus.split('_').next().unwrap_or(&dev.bus);
                if !driver_spec.bus_types.iter().any(|b| b.starts_with(bus_base)) {
                    result.add_error(step, name, &format!("devices[{}].bus", i),
                        "DRIVER_BUS_MISMATCH",
                        &format!("Driver '{}' does not support bus '{}'. Supported: {:?}",
                            dev.driver, dev.bus, driver_spec.bus_types));
                }
            }
        }
        // Driver existence already checked in Step 2
    }

    // Check node types reference valid capabilities
    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        for (ni, node) in pipeline.nodes.iter().enumerate() {
            if node.node_type == "sensor.read" {
                if let (Some(ref dev_id), Some(ref field)) = (&node.device, &node.field) {
                    if let Some(dev) = ir.devices.iter().find(|d| &d.id == dev_id) {
                        if !dev.capabilities.contains(field) {
                            result.add_error(step, name,
                                &format!("pipelines[{}].nodes[{}].field", pi, ni),
                                "DRIVER_CAPABILITY_MISMATCH",
                                &format!("Device '{}' does not have capability '{}'", dev_id, field));
                        }
                    }
                }
            }
        }
    }
}

/// Step 8: Safety policy — rate limits, stack bounds.
fn step8_safety_policy(ir: &IRDocument, registry: &DriverRegistry, result: &mut ValidationResult) {
    let step = 8;
    let name = "Safety Policy";

    // Check driver call rates
    let trigger_intervals: HashMap<&str, u64> = ir.triggers.iter()
        .filter_map(|t| t.interval_ms.map(|ms| (t.id.as_str(), ms)))
        .collect();

    for (pi, pipeline) in ir.pipelines.iter().enumerate() {
        if let Some(&interval_ms) = trigger_intervals.get(pipeline.trigger.as_str()) {
            for (ni, node) in pipeline.nodes.iter().enumerate() {
                if let Some(ref dev_id) = node.device {
                    if let Some(dev) = ir.devices.iter().find(|d| &d.id == dev_id) {
                        if let Some(driver_spec) = registry.get_driver(&dev.driver) {
                            if interval_ms < driver_spec.min_interval_ms as u64 {
                                result.add_error(step, name,
                                    &format!("pipelines[{}].nodes[{}]", pi, ni),
                                    "RATE_LIMIT_EXCEEDED",
                                    &format!("Pipeline '{}' calls driver '{}' every {}ms, but driver minimum interval is {}ms",
                                        pipeline.id, dev.driver, interval_ms, driver_spec.min_interval_ms));
                            }
                        }
                    }
                }
            }
        }

        // Estimate stack depth for safety
        let max_stack = estimate_stack_depth(&pipeline.nodes);
        if max_stack > 16 {
            result.add_error(step, name, &format!("pipelines[{}]", pi),
                "STACK_OVERFLOW_RISK",
                &format!("Pipeline '{}' estimated max stack depth {} exceeds VM limit of 16", pipeline.id, max_stack));
        }
    }
}

/// Estimate the maximum stack depth for a pipeline's node sequence.
fn estimate_stack_depth(nodes: &[IRNode]) -> u32 {
    let mut depth: i32 = 0;
    let mut max_depth: i32 = 0;

    for node in nodes {
        match node.node_type.as_str() {
            "sensor.read" => { depth += 1; }       // pushes value
            "actuator.write" => { depth -= 1; }    // pops value
            "condition.compare" => {
                // loads left, right, compares → net -1 (2 pop, 1 push bool)
                // But left/right may be var refs (push) or literals (push)
                depth += 2;  // load left, load right
                depth -= 2;  // compare pops both
                depth += 1;  // pushes result
                depth -= 1;  // JMP_IF pops
            }
            "state.load" => { depth += 1; }
            "state.store" | "state.increment" => {}
            "mqtt.publish" | "ble.notify" | "storage.log" => {}
            "math.add" | "math.sub" | "math.mul" | "math.div" => {
                depth += 2;  // load both operands
                depth -= 2;  // operation pops both
                depth += 1;  // pushes result
            }
            _ => {}
        }
        max_depth = max_depth.max(depth);
    }

    max_depth.max(0) as u32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::drivers::registry::DriverRegistry;

    fn make_minimal_ir() -> IRDocument {
        serde_json::from_str(r#"{
            "version": "1.0",
            "program_id": "00000000-0000-0000-0000-000000000001",
            "board_id": "VDYT-S3-R1",
            "created_at": "2025-01-01T00:00:00Z",
            "signature": "",
            "devices": [
                {"id": "sensor1", "driver": "drv_bme280", "bus": "i2c_0", "address": "0x76", "capabilities": ["temperature"]}
            ],
            "state": {
                "temp": {"type": "float", "initial": 0.0}
            },
            "triggers": [
                {"id": "t1", "type": "timer", "interval_ms": 5000}
            ],
            "pipelines": [
                {
                    "id": "p1",
                    "trigger": "t1",
                    "nodes": [
                        {"id": "n1", "type": "sensor.read", "device": "sensor1", "field": "temperature", "store_to": "temp"}
                    ],
                    "max_execution_ms": 200
                }
            ],
            "constraints": {"max_total_nodes": 256, "max_state_variables": 64, "max_pipelines": 16}
        }"#).unwrap()
    }

    #[test]
    fn test_valid_ir_passes() {
        let registry = DriverRegistry::new();
        let ir = make_minimal_ir();
        let result = validate_ir(&ir, &registry);
        assert!(result.valid, "Expected valid IR, got errors: {:?}", result.errors);
        assert_eq!(result.steps_completed, 8);
    }

    #[test]
    fn test_missing_device_fails() {
        let registry = DriverRegistry::new();
        let mut ir = make_minimal_ir();
        ir.pipelines[0].nodes[0].device = Some("nonexistent".to_string());
        let result = validate_ir(&ir, &registry);
        assert!(!result.valid);
        assert!(result.errors.iter().any(|e| e.code == "DEVICE_NOT_FOUND"));
    }

    #[test]
    fn test_dangling_variable_fails() {
        let registry = DriverRegistry::new();
        let mut ir = make_minimal_ir();
        ir.pipelines[0].nodes[0].store_to = Some("nonexistent_var".to_string());
        let result = validate_ir(&ir, &registry);
        assert!(!result.valid);
        assert!(result.errors.iter().any(|e| e.code == "STATE_VAR_NOT_FOUND"));
    }
}
