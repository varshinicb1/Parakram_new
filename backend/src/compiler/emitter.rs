//! Bytecode emitter — translates IR pipeline nodes to bytecode instructions.
//!
//! This is where the IR→bytecode compilation actually happens.
//! Each IR node type produces a specific sequence of instructions.

use std::collections::HashMap;
use crate::compiler::bytecode::*;
use crate::compiler::constant_pool::ConstantPool;
use crate::compiler::indexer::CompilationIndex;
use crate::ir::types::{IRDocument, IRNode, IRPipeline};

/// Emit bytecode instructions for a single pipeline.
pub fn emit_pipeline(
    pipeline: &IRPipeline,
    index: &CompilationIndex,
    const_pool: &mut ConstantPool,
    ir: &IRDocument,
) -> Result<Vec<Instruction>, String> {
    let mut instructions: Vec<Instruction> = Vec::new();

    // Pipeline start pseudo-instruction
    let pipeline_idx = index.pipeline_indices.get(&pipeline.id)
        .copied()
        .unwrap_or(0);
    instructions.push(
        Instruction::new(OP_PIPELINE_START)
            .with_a(pipeline_idx)
            .with_b(pipeline.nodes.len() as u8)
            .with_imm_u16(pipeline.max_execution_ms as u16)
    );

    // Build node ID → instruction offset map for jump targets
    // First pass: estimate instruction counts per node
    let mut node_offsets: HashMap<String, usize> = HashMap::new();
    let mut offset = 1; // Start after PIPELINE_START
    for node in &pipeline.nodes {
        node_offsets.insert(node.id.clone(), offset);
        offset += estimate_node_instruction_count(node);
    }

    // Second pass: emit instructions
    for node in &pipeline.nodes {
        emit_node(node, &mut instructions, index, const_pool, ir, &node_offsets)?;
    }

    // Pipeline end
    instructions.push(Instruction::new(OP_HALT));
    instructions.push(Instruction::new(OP_PIPELINE_END).with_a(pipeline_idx));

    Ok(instructions)
}

/// Estimate how many instructions a node will produce (for jump target calculation).
fn estimate_node_instruction_count(node: &IRNode) -> usize {
    match node.node_type.as_str() {
        "sensor.read" => 2,      // DRV_READ + STORE_VAR
        "actuator.write" => 2,   // load value + DRV_WRITE
        "actuator.write_pwm" => 2,
        "condition.compare" => 4, // load left + load right + CMP + JMP_IF
        "condition.range" => 5,   // load + load min + load max + IN_RANGE + JMP_IF
        "condition.and" | "condition.or" => 3,
        "condition.not" => 2,
        "math.add" | "math.sub" | "math.mul" | "math.div" => 4, // load + load + op + store
        "math.abs" => 3,
        "math.clamp" => 6,
        "math.map" => 8,
        "state.load" => 1,
        "state.store" => 2,      // load value + STORE_VAR
        "state.increment" => 1,
        "mqtt.publish" => 2,
        "ble.notify" => 2,
        "storage.log" => 2,
        "display.text" => 1,
        "display.value" => 2,
        "delay.ms" => 1,
        "noop" => 1,
        _ => 1,
    }
}

/// Emit bytecode instructions for a single IR node.
fn emit_node(
    node: &IRNode,
    out: &mut Vec<Instruction>,
    index: &CompilationIndex,
    const_pool: &mut ConstantPool,
    _ir: &IRDocument,
    node_offsets: &HashMap<String, usize>,
) -> Result<(), String> {
    match node.node_type.as_str() {
        "sensor.read" => {
            let dev_id = node.device.as_ref().ok_or("sensor.read requires 'device'")?;
            let field = node.field.as_ref().ok_or("sensor.read requires 'field'")?;
            let store_to = node.store_to.as_ref()
                .or(node.variable.as_ref())
                .ok_or("sensor.read requires 'store_to' or 'variable'")?;
            let drv_idx = index.device_index(dev_id).ok_or(format!("Device '{}' not indexed", dev_id))?;
            let field_idx = index.field_index(dev_id, field).ok_or(format!("Field '{}' not indexed for '{}'", field, dev_id))?;
            let var_idx = index.variable_index(store_to).ok_or(format!("Variable '{}' not indexed", store_to))?;

            out.push(Instruction::new(OP_DRV_READ).with_a(drv_idx).with_b(field_idx));
            out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
        }

        "actuator.write" => {
            let dev_id = node.device.as_ref().ok_or("actuator.write requires 'device'")?;
            let drv_idx = index.device_index(dev_id).ok_or(format!("Device '{}' not indexed", dev_id))?;

            // Load the value to write
            emit_value_load(&node.value, out, index, const_pool)?;

            out.push(Instruction::new(OP_DRV_WRITE).with_a(drv_idx));
        }

        "actuator.write_pwm" => {
            let dev_id = node.device.as_ref().ok_or("actuator.write_pwm requires 'device'")?;
            let drv_idx = index.device_index(dev_id).ok_or(format!("Device '{}' not indexed", dev_id))?;

            if let Some(duty) = node.duty_percent {
                out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(duty as f32));
            } else {
                emit_value_load(&node.value, out, index, const_pool)?;
            }

            out.push(Instruction::new(OP_DRV_PWM).with_a(drv_idx));
        }

        "condition.compare" => {
            // Load left operand
            emit_operand_load(&node.left, out, index, const_pool)?;
            // Load right operand
            emit_operand_load(&node.right, out, index, const_pool)?;

            // Emit comparison
            let op = node.op.as_deref().unwrap_or("eq");
            let cmp_opcode = match op {
                "eq" => OP_CMP_EQ,
                "neq" => OP_CMP_NEQ,
                "gt" => OP_CMP_GT,
                "lt" => OP_CMP_LT,
                "gte" => OP_CMP_GTE,
                "lte" => OP_CMP_LTE,
                _ => return Err(format!("Unknown comparison operator: '{}'", op)),
            };
            out.push(Instruction::new(cmp_opcode));

            // Conditional jump
            if let Some(ref true_target) = node.if_true {
                let target_offset = node_offsets.get(true_target)
                    .copied()
                    .ok_or(format!("Jump target '{}' not found", true_target))?;
                out.push(Instruction::new(OP_JMP_IF).with_imm_u16(target_offset as u16));
            } else {
                // No true target — just pop the comparison result
                out.push(Instruction::new(OP_POP));
            }
        }

        "condition.range" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            let min_val = node.min_value.ok_or("condition.range requires 'min_value'")?;
            let max_val = node.max_value.ok_or("condition.range requires 'max_value'")?;
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(min_val as f32));
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(max_val as f32));
            out.push(Instruction::new(OP_IN_RANGE));

            if let Some(ref true_target) = node.if_true {
                let target_offset = node_offsets.get(true_target).copied()
                    .ok_or(format!("Jump target '{}' not found", true_target))?;
                out.push(Instruction::new(OP_JMP_IF).with_imm_u16(target_offset as u16));
            }
        }

        "condition.and" => {
            if let Some(ref operands) = node.operands {
                if operands.len() >= 2 {
                    emit_json_value_load(&operands[0], out, index, const_pool)?;
                    for operand in &operands[1..] {
                        emit_json_value_load(operand, out, index, const_pool)?;
                        out.push(Instruction::new(OP_AND));
                    }
                }
            }
        }

        "condition.or" => {
            if let Some(ref operands) = node.operands {
                if operands.len() >= 2 {
                    emit_json_value_load(&operands[0], out, index, const_pool)?;
                    for operand in &operands[1..] {
                        emit_json_value_load(operand, out, index, const_pool)?;
                        out.push(Instruction::new(OP_OR));
                    }
                }
            }
        }

        "condition.not" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            out.push(Instruction::new(OP_NOT));
        }

        "math.add" | "math.sub" | "math.mul" | "math.div" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            emit_operand_load(&node.right, out, index, const_pool)?;

            let opcode = match node.node_type.as_str() {
                "math.add" => OP_ADD_F,
                "math.sub" => OP_SUB_F,
                "math.mul" => OP_MUL_F,
                "math.div" => OP_DIV_F,
                _ => unreachable!(),
            };
            out.push(Instruction::new(opcode));

            if let Some(ref store_to) = node.store_to {
                let var_idx = index.variable_index(store_to)
                    .ok_or(format!("Variable '{}' not indexed", store_to))?;
                out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
            }
        }

        "math.abs" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            out.push(Instruction::new(OP_ABS_F));
            if let Some(ref store_to) = node.store_to {
                let var_idx = index.variable_index(store_to)
                    .ok_or(format!("Variable '{}' not indexed", store_to))?;
                out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
            }
        }

        "math.clamp" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            let min_v = node.min_value.ok_or("math.clamp requires 'min_value'")?;
            let max_v = node.max_value.ok_or("math.clamp requires 'max_value'")?;
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(min_v as f32));
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(max_v as f32));
            out.push(Instruction::new(OP_CLAMP_F));
            if let Some(ref store_to) = node.store_to {
                let var_idx = index.variable_index(store_to)
                    .ok_or(format!("Variable '{}' not indexed", store_to))?;
                out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
            }
        }

        "math.map" => {
            emit_operand_load(&node.left, out, index, const_pool)?;
            let in_min = node.in_min.ok_or("math.map requires 'in_min'")? as f32;
            let in_max = node.in_max.ok_or("math.map requires 'in_max'")? as f32;
            let out_min = node.out_min.ok_or("math.map requires 'out_min'")? as f32;
            let out_max = node.out_max.ok_or("math.map requires 'out_max'")? as f32;
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(in_min));
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(in_max));
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(out_min));
            out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(out_max));
            out.push(Instruction::new(OP_MAP_F));
            if let Some(ref store_to) = node.store_to {
                let var_idx = index.variable_index(store_to)
                    .ok_or(format!("Variable '{}' not indexed", store_to))?;
                out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
            }
        }

        "state.load" => {
            let var_name = node.load_from.as_ref()
                .or(node.variable.as_ref())
                .ok_or("state.load requires 'load_from' or 'variable'")?;
            let var_idx = index.variable_index(var_name)
                .ok_or(format!("Variable '{}' not indexed", var_name))?;
            out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
        }

        "state.store" => {
            let var_name = node.store_to.as_ref()
                .or(node.variable.as_ref())
                .ok_or("state.store requires 'store_to' or 'variable'")?;
            let var_idx = index.variable_index(var_name)
                .ok_or(format!("Variable '{}' not indexed", var_name))?;
            // If there's a value to load, do it; otherwise the stack already has the value from previous node
            if node.value.is_some() {
                emit_value_load(&node.value, out, index, const_pool)?;
            }
            out.push(Instruction::new(OP_STORE_VAR).with_a(var_idx));
        }

        "state.increment" => {
            let var_name = node.store_to.as_ref()
                .or(node.variable.as_ref())
                .ok_or("state.increment requires 'store_to' or 'variable'")?;
            let var_idx = index.variable_index(var_name)
                .ok_or(format!("Variable '{}' not indexed", var_name))?;
            out.push(Instruction::new(OP_INC_VAR).with_a(var_idx));
        }

        "mqtt.publish" => {
            let topic = node.topic.as_ref().ok_or("mqtt.publish requires 'topic'")?;
            let topic_idx = const_pool.add_string(topic);

            // Load payload
            if let Some(ref payload) = node.payload {
                if let Some(var_name) = payload.strip_prefix('$') {
                    let var_idx = index.variable_index(var_name)
                        .ok_or(format!("Variable '{}' not indexed", var_name))?;
                    out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
                } else {
                    let str_idx = const_pool.add_string(payload);
                    out.push(Instruction::new(OP_LOAD_CONST).with_imm_u16(str_idx));
                }
            } else {
                out.push(Instruction::new(OP_NOP)); // No payload
            }

            out.push(Instruction::new(OP_MQTT_PUB).with_imm_u16(topic_idx));
        }

        "ble.notify" => {
            let char_name = node.characteristic.as_ref().ok_or("ble.notify requires 'characteristic'")?;
            let char_idx = const_pool.add_string(char_name);
            let var_name = node.variable.as_deref()
                .or(node.load_from.as_deref())
                .ok_or("ble.notify requires 'variable' or 'load_from'")?;
            let var_idx = index.variable_index(var_name)
                .ok_or(format!("ble.notify: variable '{}' not found in index", var_name))?;
            out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
            out.push(Instruction::new(OP_BLE_NOTIFY).with_imm_u16(char_idx));
        }

        "storage.log" => {
            let fields = node.fields.as_ref().ok_or("storage.log requires 'fields'")?;
            let dest = node.destination.as_deref().unwrap_or("flash");
            let dest_code: u8 = match dest {
                "sd_card" => 0,
                "flash" => 1,
                "mqtt" => 2,
                _ => 1,
            };
            // Load each field's variable
            for field in fields {
                if let Some(var_idx) = index.variable_index(field) {
                    out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
                }
            }
            out.push(Instruction::new(OP_LOG).with_a(dest_code).with_b(fields.len() as u8));
        }

        "display.text" => {
            let dev_id = node.device.as_ref().ok_or("display.text requires 'device'")?;
            let drv_idx = index.device_index(dev_id).ok_or(format!("Device '{}' not indexed", dev_id))?;
            let text = node.text.as_ref().ok_or("display.text requires 'text'")?;
            let text_idx = const_pool.add_string(text);
            let line = node.line.unwrap_or(0);
            out.push(Instruction::new(OP_DISP_TEXT).with_a(drv_idx).with_b(line).with_imm_u16(text_idx));
        }

        "display.value" => {
            let dev_id = node.device.as_ref().ok_or("display.value requires 'device'")?;
            let drv_idx = index.device_index(dev_id).ok_or(format!("Device '{}' not indexed", dev_id))?;
            let var_name = node.load_from.as_ref().ok_or("display.value requires 'load_from'")?;
            let var_idx = index.variable_index(var_name)
                .ok_or(format!("Variable '{}' not indexed", var_name))?;
            let line = node.line.unwrap_or(0);
            out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
            out.push(Instruction::new(OP_DISP_VAL).with_a(drv_idx).with_b(line));
        }

        "delay.ms" => {
            let duration = node.duration_ms.ok_or("delay.ms requires 'duration_ms'")?;
            out.push(Instruction::new(OP_DELAY_MS).with_imm_u16(duration.min(5000) as u16));
        }

        "noop" => {
            out.push(Instruction::new(OP_NOP));
        }

        other => {
            return Err(format!("Unknown node type: '{}'", other));
        }
    }

    Ok(())
}

/// Load a JSON value that could be a literal or a $variable reference.
fn emit_operand_load(
    val: &Option<serde_json::Value>,
    out: &mut Vec<Instruction>,
    index: &CompilationIndex,
    const_pool: &mut ConstantPool,
) -> Result<(), String> {
    let val = val.as_ref().ok_or("Expected operand value")?;
    emit_json_value_load(val, out, index, const_pool)
}

fn emit_json_value_load(
    val: &serde_json::Value,
    out: &mut Vec<Instruction>,
    index: &CompilationIndex,
    const_pool: &mut ConstantPool,
) -> Result<(), String> {
    match val {
        serde_json::Value::String(s) => {
            if let Some(var_name) = s.strip_prefix('$') {
                let var_idx = index.variable_index(var_name)
                    .ok_or(format!("Variable '{}' not indexed", var_name))?;
                out.push(Instruction::new(OP_LOAD_VAR).with_a(var_idx));
            } else {
                let str_idx = const_pool.add_string(s);
                out.push(Instruction::new(OP_LOAD_CONST).with_imm_u16(str_idx));
            }
        }
        serde_json::Value::Number(n) => {
            if let Some(f) = n.as_f64() {
                out.push(Instruction::new(OP_LOAD_IMM_F).with_imm_f32(f as f32));
            } else if let Some(i) = n.as_i64() {
                out.push(Instruction::new(OP_LOAD_IMM_I).with_imm_i32(i as i32));
            }
        }
        serde_json::Value::Bool(b) => {
            out.push(Instruction::new(OP_LOAD_IMM_B).with_a(if *b { 1 } else { 0 }));
        }
        _ => {
            return Err(format!("Unsupported value type for operand load: {:?}", val));
        }
    }
    Ok(())
}

fn emit_value_load(
    val: &Option<serde_json::Value>,
    out: &mut Vec<Instruction>,
    index: &CompilationIndex,
    const_pool: &mut ConstantPool,
) -> Result<(), String> {
    emit_operand_load(val, out, index, const_pool)
}
