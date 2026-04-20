//! Bytecode compiler module.
//!
//! Compiles validated IR documents into signed bytecode payloads.

pub mod bytecode;
pub mod constant_pool;
pub mod emitter;
pub mod indexer;
pub mod signer;

use crate::ir::types::IRDocument;
use crate::ir::validator::{ValidationResult, validate_ir};
use crate::drivers::registry::DriverRegistry;
use crate::compiler::indexer::CompilationIndex;
use crate::compiler::emitter::emit_pipeline;
use crate::compiler::constant_pool::ConstantPool;
use crate::compiler::signer::PayloadSigner;
use crate::compiler::bytecode::*;
use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum CompileError {
    #[error("Validation failed: {0:?}")]
    ValidationFailed(ValidationResult),
    #[error("Compilation error: {0}")]
    CompilationError(String),
    #[error("Signing error: {0}")]
    SigningError(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompileResult {
    pub bytecode_b64: String,
    pub bytecode_size: usize,
    pub bytecode_hash: String,
    pub num_instructions: usize,
    pub num_constants: usize,
    pub num_pipelines: usize,
    pub ir_summary: IRSummary,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IRSummary {
    pub devices: usize,
    pub state_variables: usize,
    pub triggers: usize,
    pub total_nodes: usize,
}

/// Compile an IR document into a signed bytecode payload.
pub fn compile_ir(
    ir: &IRDocument,
    device_id_hash: u32,
    registry: &DriverRegistry,
    signer: &PayloadSigner,
) -> Result<CompileResult, CompileError> {
    // Step 1: Validate
    let validation = validate_ir(ir, registry);
    if !validation.valid {
        return Err(CompileError::ValidationFailed(validation));
    }

    // Step 2: Build compilation index
    let index = CompilationIndex::build(ir);

    // Step 3: Build constant pool
    let mut const_pool = ConstantPool::new();

    // Step 4: Emit instructions for each pipeline
    let mut all_instructions: Vec<Instruction> = Vec::new();
    let mut pipeline_offsets: Vec<(String, usize, usize)> = Vec::new(); // (id, start, count)

    for pipeline in &ir.pipelines {
        let start = all_instructions.len();
        let instructions = emit_pipeline(pipeline, &index, &mut const_pool, ir)
            .map_err(|e| CompileError::CompilationError(e))?;
        let count = instructions.len();
        all_instructions.extend(instructions);
        pipeline_offsets.push((pipeline.id.clone(), start, count));
    }

    // Step 5: Serialize instructions to bytes
    let instruction_bytes: Vec<u8> = all_instructions
        .iter()
        .flat_map(|inst| inst.to_bytes())
        .collect();

    // Step 6: Serialize constant pool to bytes
    let const_bytes = const_pool.serialize();

    // Step 7: Sign the payload
    let program_id_bytes: [u8; 16] = {
        let mut buf = [0u8; 16];
        let src = ir.program_id.as_bytes();
        let len = src.len().min(16);
        buf[..len].copy_from_slice(&src[..len]);
        buf
    };
    let payload = signer
        .sign_payload(&program_id_bytes, device_id_hash, &instruction_bytes, &const_bytes)
        .map_err(|e| CompileError::SigningError(format!("{}", e)))?;

    // Step 8: Compute hash
    use sha2::{Sha256, Digest};
    let hash = {
        let mut hasher = Sha256::new();
        hasher.update(&payload);
        format!("{:x}", hasher.finalize())
    };

    Ok(CompileResult {
        bytecode_b64: base64::Engine::encode(&base64::engine::general_purpose::STANDARD, &payload),
        bytecode_size: payload.len(),
        bytecode_hash: hash,
        num_instructions: all_instructions.len(),
        num_constants: const_pool.count(),
        num_pipelines: ir.pipelines.len(),
        ir_summary: IRSummary {
            devices: ir.devices.len(),
            state_variables: ir.state.len(),
            triggers: ir.triggers.len(),
            total_nodes: ir.pipelines.iter().map(|p| p.nodes.len()).sum(),
        },
    })
}
