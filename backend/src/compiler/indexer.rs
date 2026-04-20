//! Index builder — assigns numeric indices to devices, variables, and other IR entities.
//!
//! The VM operates on indices, not string IDs. This module maps IR string IDs
//! to compact u8 indices for bytecode emission.

use std::collections::HashMap;
use crate::ir::types::IRDocument;

/// Compilation index — maps string IDs to compact numeric indices.
#[derive(Debug)]
pub struct CompilationIndex {
    /// device_id → driver index (u8)
    pub device_indices: HashMap<String, u8>,
    /// variable_name → variable index (u8)
    pub variable_indices: HashMap<String, u8>,
    /// trigger_id → trigger index (u8)
    pub trigger_indices: HashMap<String, u8>,
    /// pipeline_id → pipeline index (u8)
    pub pipeline_indices: HashMap<String, u8>,
    /// (device_id, capability_name) → field index within that driver (u8)
    pub field_indices: HashMap<(String, String), u8>,
}

impl CompilationIndex {
    /// Build the compilation index from a validated IR document.
    pub fn build(ir: &IRDocument) -> Self {
        let mut device_indices = HashMap::new();
        let mut field_indices = HashMap::new();

        for (i, dev) in ir.devices.iter().enumerate() {
            device_indices.insert(dev.id.clone(), i as u8);
            for (fi, cap) in dev.capabilities.iter().enumerate() {
                field_indices.insert((dev.id.clone(), cap.clone()), fi as u8);
            }
        }

        let variable_indices: HashMap<String, u8> = ir.state.keys()
            .enumerate()
            .map(|(i, name)| (name.clone(), i as u8))
            .collect();

        let trigger_indices: HashMap<String, u8> = ir.triggers.iter()
            .enumerate()
            .map(|(i, t)| (t.id.clone(), i as u8))
            .collect();

        let pipeline_indices: HashMap<String, u8> = ir.pipelines.iter()
            .enumerate()
            .map(|(i, p)| (p.id.clone(), i as u8))
            .collect();

        Self {
            device_indices,
            variable_indices,
            trigger_indices,
            pipeline_indices,
            field_indices,
        }
    }

    pub fn device_index(&self, id: &str) -> Option<u8> {
        self.device_indices.get(id).copied()
    }

    pub fn variable_index(&self, name: &str) -> Option<u8> {
        self.variable_indices.get(name).copied()
    }

    pub fn field_index(&self, device_id: &str, field: &str) -> Option<u8> {
        self.field_indices.get(&(device_id.to_string(), field.to_string())).copied()
    }
}
