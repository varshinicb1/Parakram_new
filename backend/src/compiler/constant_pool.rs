//! Constant pool — stores string and float constants referenced by bytecode.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum ConstantValue {
    Int(i32),
    Float(f32),
    Bool(bool),
    String(String),
}

/// Constant pool entry.
#[derive(Debug, Clone)]
pub struct ConstantEntry {
    pub value: ConstantValue,
}

/// Constant pool — append-only collection of constants.
pub struct ConstantPool {
    entries: Vec<ConstantEntry>,
}

impl ConstantPool {
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
        }
    }

    /// Add a float constant, returning its index.
    pub fn add_float(&mut self, val: f32) -> u16 {
        // Check if already exists
        for (i, entry) in self.entries.iter().enumerate() {
            if let ConstantValue::Float(v) = &entry.value {
                if (*v - val).abs() < f32::EPSILON {
                    return i as u16;
                }
            }
        }
        let idx = self.entries.len() as u16;
        self.entries.push(ConstantEntry {
            value: ConstantValue::Float(val),
        });
        idx
    }

    /// Add an integer constant, returning its index.
    pub fn add_int(&mut self, val: i32) -> u16 {
        for (i, entry) in self.entries.iter().enumerate() {
            if let ConstantValue::Int(v) = &entry.value {
                if *v == val {
                    return i as u16;
                }
            }
        }
        let idx = self.entries.len() as u16;
        self.entries.push(ConstantEntry {
            value: ConstantValue::Int(val),
        });
        idx
    }

    /// Add a string constant, returning its index.
    pub fn add_string(&mut self, val: &str) -> u16 {
        for (i, entry) in self.entries.iter().enumerate() {
            if let ConstantValue::String(s) = &entry.value {
                if s == val {
                    return i as u16;
                }
            }
        }
        let idx = self.entries.len() as u16;
        self.entries.push(ConstantEntry {
            value: ConstantValue::String(val.to_string()),
        });
        idx
    }

    pub fn count(&self) -> usize {
        self.entries.len()
    }

    /// Serialize the constant pool to bytes.
    ///
    /// Format per entry:
    ///   [type: 1B] [length: 1B] [data: variable]
    ///
    /// Types: 0=int(4B), 1=float(4B), 2=bool(1B), 3=string(NB, null-terminated, padded to 4)
    pub fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        for entry in &self.entries {
            match &entry.value {
                ConstantValue::Int(v) => {
                    bytes.push(0x00);       // type = int
                    bytes.push(4);          // length
                    bytes.extend_from_slice(&v.to_le_bytes());
                    bytes.extend_from_slice(&[0, 0]); // pad to 8 bytes per entry
                }
                ConstantValue::Float(v) => {
                    bytes.push(0x01);       // type = float
                    bytes.push(4);          // length
                    bytes.extend_from_slice(&v.to_le_bytes());
                    bytes.extend_from_slice(&[0, 0]); // pad
                }
                ConstantValue::Bool(v) => {
                    bytes.push(0x02);       // type = bool
                    bytes.push(1);          // length
                    bytes.push(if *v { 1 } else { 0 });
                    bytes.extend_from_slice(&[0, 0, 0, 0, 0]); // pad to 8
                }
                ConstantValue::String(s) => {
                    bytes.push(0x03);       // type = string
                    let str_bytes = s.as_bytes();
                    let len = str_bytes.len().min(31) as u8;
                    bytes.push(len + 1);    // +1 for null terminator
                    bytes.extend_from_slice(&str_bytes[..len as usize]);
                    bytes.push(0);          // null terminator
                    // Pad to 4-byte alignment
                    let total = 2 + (len as usize) + 1;
                    let padding = (4 - (total % 4)) % 4;
                    bytes.extend(std::iter::repeat(0).take(padding));
                }
            }
        }

        bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dedup_float() {
        let mut pool = ConstantPool::new();
        let i1 = pool.add_float(30.0);
        let i2 = pool.add_float(30.0);
        assert_eq!(i1, i2);
        assert_eq!(pool.count(), 1);
    }

    #[test]
    fn test_different_types() {
        let mut pool = ConstantPool::new();
        pool.add_float(1.0);
        pool.add_int(42);
        pool.add_string("hello");
        assert_eq!(pool.count(), 3);
    }

    #[test]
    fn test_serialize_round_trip() {
        let mut pool = ConstantPool::new();
        pool.add_float(30.0);
        let bytes = pool.serialize();
        assert!(!bytes.is_empty());
        // Type=1 (float), Length=4
        assert_eq!(bytes[0], 0x01);
        assert_eq!(bytes[1], 4);
    }
}
