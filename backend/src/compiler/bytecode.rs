//! Bytecode instruction definitions matching the ISA from Artifact 4.

/// Fixed 8-byte instruction.
#[derive(Debug, Clone)]
pub struct Instruction {
    pub opcode: u8,
    pub operand_a: u8,
    pub operand_b: u8,
    pub operand_c: u8,
    pub immediate: [u8; 4],
}

impl Instruction {
    pub fn new(opcode: u8) -> Self {
        Self {
            opcode,
            operand_a: 0,
            operand_b: 0,
            operand_c: 0,
            immediate: [0u8; 4],
        }
    }

    pub fn with_a(mut self, a: u8) -> Self { self.operand_a = a; self }
    pub fn with_b(mut self, b: u8) -> Self { self.operand_b = b; self }
    pub fn with_c(mut self, c: u8) -> Self { self.operand_c = c; self }
    pub fn with_imm_u16(mut self, val: u16) -> Self {
        self.immediate[0..2].copy_from_slice(&val.to_le_bytes());
        self
    }
    pub fn with_imm_f32(mut self, val: f32) -> Self {
        self.immediate.copy_from_slice(&val.to_le_bytes());
        self
    }
    pub fn with_imm_i32(mut self, val: i32) -> Self {
        self.immediate.copy_from_slice(&val.to_le_bytes());
        self
    }
    pub fn with_imm_u32(mut self, val: u32) -> Self {
        self.immediate.copy_from_slice(&val.to_le_bytes());
        self
    }

    pub fn to_bytes(&self) -> [u8; 8] {
        let mut bytes = [0u8; 8];
        bytes[0] = self.opcode;
        bytes[1] = self.operand_a;
        bytes[2] = self.operand_b;
        bytes[3] = self.operand_c;
        bytes[4..8].copy_from_slice(&self.immediate);
        bytes
    }
}

// ============================================================
// Opcodes — Matching ISA from Artifact 4
// ============================================================

// Stack operations
pub const OP_NOP: u8        = 0x00;
pub const OP_LOAD_IMM_I: u8 = 0x01;  // Push immediate i32
pub const OP_LOAD_IMM_F: u8 = 0x02;  // Push immediate f32
pub const OP_LOAD_IMM_B: u8 = 0x03;  // Push immediate bool
pub const OP_LOAD_VAR: u8   = 0x04;  // Push state variable
pub const OP_STORE_VAR: u8  = 0x05;  // Pop → state variable
pub const OP_LOAD_CONST: u8 = 0x06;  // Push constant pool entry
pub const OP_DUP: u8        = 0x07;
pub const OP_POP: u8        = 0x08;
pub const OP_SWAP: u8       = 0x09;
pub const OP_INC_VAR: u8    = 0x0A;  // Increment state variable

// Arithmetic
pub const OP_ADD_I: u8      = 0x10;
pub const OP_SUB_I: u8      = 0x11;
pub const OP_MUL_I: u8      = 0x12;
pub const OP_DIV_I: u8      = 0x13;
pub const OP_ADD_F: u8      = 0x14;
pub const OP_SUB_F: u8      = 0x15;
pub const OP_MUL_F: u8      = 0x16;
pub const OP_DIV_F: u8      = 0x17;
pub const OP_ABS_F: u8      = 0x18;
pub const OP_CLAMP_F: u8    = 0x19;
pub const OP_MAP_F: u8      = 0x1A;
pub const OP_MOD_I: u8      = 0x1B;
pub const OP_NEG_F: u8      = 0x1C;

// Comparison
pub const OP_CMP_EQ: u8     = 0x20;
pub const OP_CMP_NEQ: u8    = 0x21;
pub const OP_CMP_GT: u8     = 0x22;
pub const OP_CMP_LT: u8     = 0x23;
pub const OP_CMP_GTE: u8    = 0x24;
pub const OP_CMP_LTE: u8    = 0x25;
pub const OP_AND: u8        = 0x26;
pub const OP_OR: u8         = 0x27;
pub const OP_NOT: u8        = 0x28;
pub const OP_IN_RANGE: u8   = 0x29;

// Control flow
pub const OP_JMP: u8        = 0x40;
pub const OP_JMP_IF: u8     = 0x41;
pub const OP_JMP_IFNOT: u8  = 0x42;
pub const OP_HALT: u8       = 0x43;
pub const OP_YIELD: u8      = 0x44;

// Driver I/O
pub const OP_DRV_READ: u8   = 0x60;  // a=driver_idx, b=field_idx → push
pub const OP_DRV_WRITE: u8  = 0x61;  // a=driver_idx → pop value, write
pub const OP_DRV_PWM: u8    = 0x62;  // a=driver_idx → pop duty%

// Communication
pub const OP_MQTT_PUB: u8   = 0x70;  // a=topic_const_idx, pop payload
pub const OP_BLE_NOTIFY: u8 = 0x71;  // a=char_idx, pop payload
pub const OP_LOG: u8        = 0x72;  // a=destination, b=num_fields

// Display / UI Hierarchy
pub const OP_DISP_TEXT: u8  = 0x80;  // a=driver_idx, b=line, c=const_str_idx
pub const OP_DISP_VAL: u8   = 0x81;  // a=driver_idx, b=line, pop value
pub const OP_DISP_IMG_BLOB: u8 = 0x82; // a=img_asset_idx, immediate=blob_size

pub const OP_UI_CREATE_OBJ: u8 = 0x83; // a=obj_type enum (btn, cont, label), returns internal ptr handle
pub const OP_UI_SET_PARENT: u8 = 0x84; // a=child_handle, b=parent_handle
pub const OP_UI_SET_FLEX: u8   = 0x85; // a=handle, b=flex_flow_enum

// Timing
pub const OP_DELAY_MS: u8   = 0x90;  // immediate = duration_ms (u16)

// Pipeline metadata (pseudo-instructions — emitted at pipeline start)
pub const OP_PIPELINE_START: u8 = 0xF0;  // a=pipeline_id, imm=max_exec_ms, b=num_nodes
pub const OP_PIPELINE_END: u8   = 0xF1;

/// Bytecode header magic — "PRKM" (Parakram, Vidyuthlabs).
pub const BYTECODE_MAGIC: [u8; 4] = [0x50, 0x52, 0x4B, 0x4D];
/// Extended Proprietary Signature Watermark — "PARAKRAM_NC\0"
/// This signature is embedded in every compiled firmware binary.
/// It provides legal traceability and proof of origin.
/// DO NOT REMOVE — required for IP enforcement.
pub const PROPRIETARY_AUTH_SIG: [u8; 12] = [
    0x50, 0x41, 0x52, 0x41, 0x4B, 0x52, 0x41, 0x4D, 0x5F, 0x4E, 0x43, 0x00
];
/// Bytecode version.
pub const BYTECODE_VERSION: u16 = 1;
/// Signature block magic.
pub const SIG_MAGIC: [u8; 4] = [0x53, 0x49, 0x47, 0x30]; // "SIG0"
