//! Ed25519 payload signer.
//!
//! Signs compiled bytecode with Ed25519 for device verification.

use ring::signature::{Ed25519KeyPair, KeyPair};
use ring::rand::SystemRandom;
use thiserror::Error;

use crate::compiler::bytecode::{BYTECODE_MAGIC, BYTECODE_VERSION, SIG_MAGIC};

#[derive(Debug, Error)]
pub enum SigningError {
    #[error("Key generation failed: {0}")]
    KeyGenFailed(String),
    #[error("Invalid key format: {0}")]
    InvalidKey(String),
    #[error("Payload too large: {size} bytes (max {max})")]
    PayloadTooLarge { size: usize, max: usize },
}

/// Bytecode payload signer holding an Ed25519 private key.
pub struct PayloadSigner {
    key_pair: Ed25519KeyPair,
    _rng: SystemRandom,
}

impl PayloadSigner {
    /// Create a signer from PKCS#8 DER bytes.
    pub fn from_pkcs8(pkcs8_der: &[u8]) -> Result<Self, SigningError> {
        let key_pair = Ed25519KeyPair::from_pkcs8(pkcs8_der)
            .map_err(|e| SigningError::InvalidKey(format!("{:?}", e)))?;
        Ok(Self {
            key_pair,
            _rng: SystemRandom::new(),
        })
    }

    /// Create a signer with an ephemeral key (for development).
    pub fn new_ephemeral() -> Result<Self, SigningError> {
        let rng = SystemRandom::new();
        let pkcs8_doc = Ed25519KeyPair::generate_pkcs8(&rng)
            .map_err(|e| SigningError::KeyGenFailed(format!("{:?}", e)))?;
        let key_pair = Ed25519KeyPair::from_pkcs8(pkcs8_doc.as_ref())
            .map_err(|e| SigningError::InvalidKey(format!("{:?}", e)))?;
        Ok(Self {
            key_pair,
            _rng: rng,
        })
    }

    /// Get the public key bytes (32 bytes).
    pub fn public_key_bytes(&self) -> &[u8] {
        self.key_pair.public_key().as_ref()
    }

    /// Sign a compiled bytecode payload.
    ///
    /// Returns the complete signed payload:
    /// [header: 32B][instructions: N×8B][constants: M bytes][sig_block: 72B]
    pub fn sign_payload(
        &self,
        program_id: &[u8; 16],
        device_id_hash: u32,
        instructions: &[u8],
        constant_pool: &[u8],
    ) -> Result<Vec<u8>, SigningError> {
        let num_instructions = instructions.len() / 8;
        if num_instructions > 1024 {
            return Err(SigningError::PayloadTooLarge {
                size: num_instructions,
                max: 1024,
            });
        }

        // --- Build header (32 bytes) ---
        let mut header = Vec::with_capacity(32);
        header.extend_from_slice(&BYTECODE_MAGIC);                         // 0x00: magic (4B)
        header.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());         // 0x04: version (2B)
        header.extend_from_slice(&0u16.to_le_bytes());                     // 0x06: flags (2B)
        header.extend_from_slice(program_id);                              // 0x08: program_id (16B)
        header.extend_from_slice(&device_id_hash.to_le_bytes());           // 0x18: device_hash (4B)
        header.extend_from_slice(&(num_instructions as u16).to_le_bytes()); // 0x1C: num_instr (2B)

        // Estimate constant count from pool size
        let num_constants = if constant_pool.is_empty() { 0u16 } else {
            (constant_pool.len() / 8).max(1) as u16
        };
        header.extend_from_slice(&num_constants.to_le_bytes());            // 0x1E: num_const (2B)

        assert_eq!(header.len(), 32);

        // --- Assemble signed content ---
        let signed_len = header.len() + instructions.len() + constant_pool.len();
        let mut signed_content = Vec::with_capacity(signed_len);
        signed_content.extend_from_slice(&header);
        signed_content.extend_from_slice(instructions);
        signed_content.extend_from_slice(constant_pool);

        // --- Sign ---
        let signature = self.key_pair.sign(&signed_content);
        let sig_bytes = signature.as_ref();
        assert_eq!(sig_bytes.len(), 64);

        // --- Build signature block (72 bytes) ---
        let mut sig_block = Vec::with_capacity(72);
        sig_block.extend_from_slice(&SIG_MAGIC);                               // magic (4B)
        sig_block.extend_from_slice(&(signed_len as u32).to_le_bytes());       // signed_len (4B)
        sig_block.extend_from_slice(sig_bytes);                                 // signature (64B)
        assert_eq!(sig_block.len(), 72);

        // --- Final payload ---
        let mut payload = Vec::with_capacity(signed_len + 72);
        payload.extend_from_slice(&signed_content);
        payload.extend_from_slice(&sig_block);

        Ok(payload)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sign_and_verify() {
        let signer = PayloadSigner::new_ephemeral().unwrap();

        let program_id = [0u8; 16];
        let device_hash: u32 = 0xDEADBEEF;
        // Single HALT instruction
        let instructions = [0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let const_pool = [];

        let payload = signer.sign_payload(&program_id, device_hash, &instructions, &const_pool).unwrap();

        // Verify structure
        assert_eq!(&payload[0..4], &BYTECODE_MAGIC);
        assert_eq!(payload.len(), 32 + 8 + 72); // header + 1 instr + sig

        // Verify signature
        let signed_len = u32::from_le_bytes(payload[payload.len()-68..payload.len()-64].try_into().unwrap()) as usize;
        let signed_content = &payload[..signed_len];
        let sig = &payload[payload.len()-64..];

        let pubkey = ring::signature::UnparsedPublicKey::new(
            &ring::signature::ED25519,
            signer.public_key_bytes(),
        );
        assert!(pubkey.verify(signed_content, sig).is_ok());
    }
}
