# Artifact 9 — Security Implementation Plan

## 1. Trust Hierarchy

```
Vidyuthlabs Root CA
    (Offline, HSM-stored, signs backend keys)
    │
    ├── Backend Signing Key (Ed25519)
    │   (Online, OS keychain, rotatable)
    │   Signs all bytecode payloads
    │   │
    │   └── Signed Bytecode Payloads
    │       (Per-deploy, per-device binding)
    │
    └── Device Keypairs (Ed25519)
        (Per-device, factory-provisioned)
        (Device private key in eFuse, never extractable)
        │
        └── Device-to-Backend Authentication
            (HMAC challenge-response)
```

---

## 2. Factory Provisioning Procedure

### Prerequisites
- ESP32-S3 module with unprogrammed eFuses
- Provisioning station (secured workstation with HSM access)
- Vidyuthlabs Provisioning Tool (CLI)
- Backend signing public key (from HSM)

### Step-by-Step Procedure

```
Step 1: Generate Device Identity
─────────────────────────────────
- Generate unique device UUID (v4)
- Generate Ed25519 keypair for the device
- Generate HMAC device secret (32 bytes, random)

Step 2: Program eFuses (IRREVERSIBLE)
─────────────────────────────────
- Write device UUID to USER_DATA eFuse block (block 4)
- Write device Ed25519 public key to USER_DATA eFuse block (block 5)
- Write backend verification public key to USER_DATA eFuse block (block 6)
- Write HMAC device secret to KEY_PURPOSE eFuse block (block 7)
- Set read-protect on device private key eFuse (KEY_PURPOSE block 8)
- Set write-protect on all programmed eFuse blocks

Step 3: Flash Firmware
─────────────────────────────────
- Flash factory firmware image (pre-signed with Vidyuthlabs root key)
- Flash partition table (partitions.csv)
- Flash bootloader (with secure boot V2 digest)

Step 4: Enable Secure Boot V2
─────────────────────────────────
- Burn Secure Boot V2 key digest to eFuse BLOCK_KEY0
- Set SECURE_BOOT_EN eFuse bit
- Verify: device only boots signed firmware images

Step 5: Enable Flash Encryption
─────────────────────────────────
- Generate flash encryption key (AES-256-XTS)
- Burn key to eFuse BLOCK_KEY1
- Set SPI_BOOT_CRYPT_CNT to enable encryption
- Encrypt all flash partitions
- Set DOWNLOAD_DIS_ENCRYPT and DOWNLOAD_DIS_DECRYPT bits
  (prevents key extraction via UART download mode)

Step 6: Register Device in Backend Database
─────────────────────────────────
- Insert row into `devices` table:
  - device_uuid
  - board_sku (e.g., "VDYT-S3-R1")
  - device_pubkey (Ed25519 public key, base64)
  - hmac_secret_hash (SHA-256 of HMAC secret, for backend verification)
  - firmware_version
  - provisioned_at timestamp
- Store device private key ONLY in eFuse (never in database)

Step 7: Print QR Code Label
─────────────────────────────────
- Encode: device_uuid + board_sku + provisioning_batch
- Print and affix to device enclosure
- QR scanned by Android app during pairing
```

### Provisioning Tool — Key Commands

```bash
# Generate device identity
parakram-provision generate \
    --board-sku VDYT-S3-R1 \
    --output device_identity.json

# Program eFuses and flash firmware
parakram-provision flash \
    --identity device_identity.json \
    --firmware firmware_v1.0.0.bin \
    --bootloader bootloader.bin \
    --partition-table partitions.csv \
    --backend-pubkey backend_verify.pub \
    --port /dev/ttyUSB0

# Verify provisioned device
parakram-provision verify \
    --port /dev/ttyUSB0 \
    --expected-uuid <uuid>

# Register in backend
parakram-provision register \
    --identity device_identity.json \
    --backend-url http://localhost:8400
```

---

## 3. Backend Key Management Procedure

### Key Generation

```bash
# Generate backend Ed25519 signing keypair
# Run on secured provisioning workstation, NEVER on a shared server

# Step 1: Generate keypair
openssl genpkey -algorithm ed25519 -out backend_signing.pem
openssl pkey -in backend_signing.pem -pubout -out backend_verify.pub

# Step 2: Store private key in OS keychain
# macOS:
security add-generic-password \
    -s "parakram-backend-signing-key" \
    -a "parakram" \
    -w "$(base64 < backend_signing.pem)" \
    -T /usr/local/bin/parakram-backend

# Linux (using libsecret / gnome-keyring):
secret-tool store --label="Parakram Backend Signing Key" \
    application parakram \
    key backend-signing-key <<< "$(base64 < backend_signing.pem)"

# Step 3: Delete the PEM file from disk
shred -u backend_signing.pem
```

### Key Rotation Procedure

```
1. Generate new Ed25519 keypair
2. Update the backend's keychain with the new private key
3. Deploy new backend version that loads the new key
4. The new public key must be pushed to all devices via OTA firmware update
   (since the verify key is in eFuse, this requires a firmware update that
    supports dual-key verification during the transition period)
5. During transition: backend signs with BOTH old and new keys
   (device firmware checks new key first, falls back to old)
6. After all devices updated: revoke old key, backend signs with new key only
```

### Key Storage Architecture

```
┌─────────────────────────────────────────────┐
│              Backend Process                 │
│                                              │
│  startup:                                    │
│    key = read_from_keychain("parakram-...");  │
│    SIGNING_KEY = Ed25519PrivateKey::from(key);│
│    // Key lives ONLY in process memory       │
│    // Never written to disk, DB, or logs     │
│                                              │
│  On each deploy:                             │
│    signature = SIGNING_KEY.sign(bytecode);    │
│    // Only the signature leaves the process  │
│                                              │
└─────────────────────────────────────────────┘
```

---

## 4. Payload Signing Code (Rust — Backend)

```rust
//! Parakram Backend — Bytecode Payload Signer
//!
//! Uses the `ring` crate for Ed25519 signing.
//! The signing key is loaded from the OS keychain at startup
//! and held in memory for the lifetime of the process.

use ring::signature::{self, Ed25519KeyPair, KeyPair};
use ring::rand::SystemRandom;
use sha2::{Sha256, Digest};
use std::sync::Arc;
use thiserror::Error;

/// Magic bytes for the signature block
const SIG_MAGIC: [u8; 4] = [0x53, 0x49, 0x47, 0x30]; // "SIG0"

/// Magic bytes for the bytecode header
const BYTECODE_MAGIC: [u8; 4] = [0x50, 0x52, 0x4B, 0x4D]; // "PRKM"

/// Bytecode format version
const BYTECODE_VERSION: u16 = 1;

#[derive(Error, Debug)]
pub enum SigningError {
    #[error("Failed to load signing key from keychain: {0}")]
    KeychainError(String),
    #[error("Invalid key format: {0}")]
    InvalidKey(String),
    #[error("Signing failed: {0}")]
    SigningFailed(String),
    #[error("Payload too large: {size} bytes (max {max})")]
    PayloadTooLarge { size: usize, max: usize },
}

/// Bytecode payload signer.
/// Holds the Ed25519 private key in memory.
pub struct PayloadSigner {
    key_pair: Ed25519KeyPair,
    _rng: SystemRandom,
}

impl PayloadSigner {
    /// Create a new signer from PKCS#8 DER-encoded private key bytes.
    ///
    /// The key bytes should be loaded from the OS keychain at startup.
    /// After calling this constructor, the caller should zeroize the
    /// original key bytes.
    pub fn new(pkcs8_der: &[u8]) -> Result<Self, SigningError> {
        let key_pair = Ed25519KeyPair::from_pkcs8(pkcs8_der)
            .map_err(|e| SigningError::InvalidKey(format!("{:?}", e)))?;

        Ok(Self {
            key_pair,
            _rng: SystemRandom::new(),
        })
    }

    /// Load the signing key from the OS keychain.
    ///
    /// On Linux, uses libsecret (gnome-keyring / KDE Wallet).
    /// On macOS, uses Security.framework.
    pub fn from_keychain() -> Result<Self, SigningError> {
        let key_b64 = Self::read_keychain_entry("parakram-backend-signing-key")
            .map_err(|e| SigningError::KeychainError(e))?;

        use base64::Engine;
        let key_pem = base64::engine::general_purpose::STANDARD
            .decode(&key_b64)
            .map_err(|e| SigningError::InvalidKey(format!("Base64 decode failed: {}", e)))?;

        // Parse PEM to get DER-encoded PKCS#8
        let pkcs8_der = Self::pem_to_pkcs8_der(&key_pem)
            .map_err(|e| SigningError::InvalidKey(e))?;

        Self::new(&pkcs8_der)
    }

    /// Get the public key bytes (for device provisioning).
    pub fn public_key_bytes(&self) -> &[u8] {
        self.key_pair.public_key().as_ref()
    }

    /// Sign a compiled bytecode payload.
    ///
    /// # Arguments
    /// * `program_id` - 16-byte UUID of the program
    /// * `device_id_hash` - CRC32 of the target device UUID
    /// * `instructions` - Compiled instruction bytes (N × 8 bytes)
    /// * `constant_pool` - Serialized constant pool
    ///
    /// # Returns
    /// Complete signed payload: [header][instructions][constants][signature_block]
    pub fn sign_payload(
        &self,
        program_id: &[u8; 16],
        device_id_hash: u32,
        instructions: &[u8],
        constant_pool: &[u8],
    ) -> Result<Vec<u8>, SigningError> {
        let num_instructions = instructions.len() / 8;
        let num_constants = constant_pool.len() / 36; // Approximate

        // Validate sizes
        if num_instructions > 1024 {
            return Err(SigningError::PayloadTooLarge {
                size: num_instructions,
                max: 1024,
            });
        }

        // Build header (32 bytes)
        let mut header = Vec::with_capacity(32);
        header.extend_from_slice(&BYTECODE_MAGIC);           // 0x00: magic (4B)
        header.extend_from_slice(&BYTECODE_VERSION.to_le_bytes()); // 0x04: version (2B)
        header.extend_from_slice(&0u16.to_le_bytes());        // 0x06: flags (2B)
        header.extend_from_slice(program_id);                  // 0x08: program_id (16B)
        header.extend_from_slice(&device_id_hash.to_le_bytes()); // 0x18: device_id_hash (4B)
        header.extend_from_slice(&(num_instructions as u16).to_le_bytes()); // 0x1C: num_instr (2B)
        header.extend_from_slice(&(num_constants as u16).to_le_bytes());    // 0x1E: num_const (2B)

        assert_eq!(header.len(), 32);

        // Assemble signed content: header + instructions + constant_pool
        let signed_content_len = header.len() + instructions.len() + constant_pool.len();
        let mut signed_content = Vec::with_capacity(signed_content_len);
        signed_content.extend_from_slice(&header);
        signed_content.extend_from_slice(instructions);
        signed_content.extend_from_slice(constant_pool);

        // Sign
        let signature = self.key_pair.sign(&signed_content);
        let sig_bytes = signature.as_ref();

        assert_eq!(sig_bytes.len(), 64, "Ed25519 signature must be 64 bytes");

        // Build signature block (72 bytes)
        let mut sig_block = Vec::with_capacity(72);
        sig_block.extend_from_slice(&SIG_MAGIC);                          // 0x00: sig_magic (4B)
        sig_block.extend_from_slice(&(signed_content_len as u32).to_le_bytes()); // 0x04: signed_len (4B)
        sig_block.extend_from_slice(sig_bytes);                            // 0x08: signature (64B)

        assert_eq!(sig_block.len(), 72);

        // Assemble final payload
        let mut payload = Vec::with_capacity(signed_content_len + 72);
        payload.extend_from_slice(&signed_content);
        payload.extend_from_slice(&sig_block);

        Ok(payload)
    }

    /// Compute SHA-256 hash of a payload (for storage/comparison).
    pub fn hash_payload(payload: &[u8]) -> [u8; 32] {
        let mut hasher = Sha256::new();
        hasher.update(payload);
        hasher.finalize().into()
    }

    // Platform-specific keychain access
    #[cfg(target_os = "linux")]
    fn read_keychain_entry(key_name: &str) -> Result<String, String> {
        use std::process::Command;
        let output = Command::new("secret-tool")
            .args(["lookup", "application", "parakram", "key", key_name])
            .output()
            .map_err(|e| format!("Failed to run secret-tool: {}", e))?;

        if !output.status.success() {
            return Err(format!(
                "secret-tool failed: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }

    #[cfg(target_os = "macos")]
    fn read_keychain_entry(key_name: &str) -> Result<String, String> {
        use std::process::Command;
        let output = Command::new("security")
            .args([
                "find-generic-password",
                "-s", key_name,
                "-a", "parakram",
                "-w",
            ])
            .output()
            .map_err(|e| format!("Failed to run security: {}", e))?;

        if !output.status.success() {
            return Err(format!(
                "security command failed: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }

    fn pem_to_pkcs8_der(pem_bytes: &[u8]) -> Result<Vec<u8>, String> {
        // Simple PEM parser — extract base64 content between BEGIN/END markers
        let pem_str = std::str::from_utf8(pem_bytes)
            .map_err(|e| format!("Invalid UTF-8 in PEM: {}", e))?;

        let b64_content: String = pem_str
            .lines()
            .filter(|line| !line.starts_with("-----"))
            .collect();

        use base64::Engine;
        base64::engine::general_purpose::STANDARD
            .decode(&b64_content)
            .map_err(|e| format!("Base64 decode failed: {}", e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ring::signature::Ed25519KeyPair;
    use ring::rand::SystemRandom;

    #[test]
    fn test_sign_and_verify_payload() {
        // Generate test keypair
        let rng = SystemRandom::new();
        let pkcs8_doc = Ed25519KeyPair::generate_pkcs8(&rng).unwrap();
        let signer = PayloadSigner::new(pkcs8_doc.as_ref()).unwrap();

        // Create test payload
        let program_id = [0u8; 16];
        let device_id_hash: u32 = 0xDEADBEEF;
        let instructions = vec![0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]; // HALT
        let constant_pool = vec![];

        let payload = signer
            .sign_payload(&program_id, device_id_hash, &instructions, &constant_pool)
            .unwrap();

        // Verify structure
        assert_eq!(&payload[0..4], &BYTECODE_MAGIC);  // Magic
        assert_eq!(payload.len(), 32 + 8 + 72);       // header + 1 instr + sig block

        // Extract and verify signature
        let signed_len = u32::from_le_bytes(payload[payload.len()-68..payload.len()-64].try_into().unwrap());
        let signed_content = &payload[..signed_len as usize];
        let signature = &payload[payload.len()-64..];

        let public_key = ring::signature::UnparsedPublicKey::new(
            &ring::signature::ED25519,
            signer.public_key_bytes(),
        );
        assert!(public_key.verify(signed_content, signature).is_ok());
    }
}
```

---

## 5. Payload Verification Code (C — Device Firmware)

```c
/**
 * @file payload_verify.c
 * @brief Ed25519 signature verification for incoming bytecode payloads.
 *
 * Verifies that a bytecode payload was signed by the Vidyuthlabs backend
 * signing key. The backend's public verification key is stored in eFuse.
 *
 * This module:
 * - Reads the backend public key from eFuse at init
 * - Verifies Ed25519 signatures using mbedtls
 * - Validates payload header (magic, version, device binding)
 * - Rejects any payload not signed by the trusted key
 */

#include "payload_verify.h"
#include "device_identity.h"
#include "system_config.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_crc.h"
#include "mbedtls/ed25519.h"
#include "mbedtls/sha256.h"
#include <string.h>

static const char *TAG = "payload_verify";

/* ============================================================
 * Constants
 * ============================================================ */

/** Bytecode header magic bytes "PRKM" */
static const uint8_t BYTECODE_MAGIC[4] = {0x50, 0x52, 0x4B, 0x4D};

/** Signature block magic bytes "SIG0" */
static const uint8_t SIG_MAGIC[4] = {0x53, 0x49, 0x47, 0x30};

/** Expected bytecode format version */
#define EXPECTED_VERSION    1

/** Header size in bytes */
#define HEADER_SIZE         32

/** Signature block size in bytes */
#define SIG_BLOCK_SIZE      72

/** Ed25519 public key size */
#define PUBKEY_SIZE         32

/** Ed25519 signature size */
#define SIGNATURE_SIZE      64

/** Maximum payload size (header + 1024 instructions + const pool + sig) */
#define MAX_PAYLOAD_SIZE    16384

/* ============================================================
 * Static State
 * ============================================================ */

typedef struct {
    bool        initialized;
    uint8_t     backend_pubkey[PUBKEY_SIZE];     /* From eFuse */
    uint32_t    device_id_hash;                  /* CRC32 of our device UUID */
} verify_state_t;

static verify_state_t s_state = {
    .initialized = false,
};

/* ============================================================
 * Parsed Payload Structures
 * ============================================================ */

/** Parsed bytecode header */
typedef struct {
    uint8_t     magic[4];
    uint16_t    version;
    uint16_t    flags;
    uint8_t     program_id[16];
    uint32_t    device_id_hash;
    uint16_t    num_instructions;
    uint16_t    num_constants;
} __attribute__((packed)) bytecode_header_t;

/** Parsed signature block */
typedef struct {
    uint8_t     magic[4];
    uint32_t    signed_length;
    uint8_t     signature[SIGNATURE_SIZE];
} __attribute__((packed)) sig_block_t;

_Static_assert(sizeof(bytecode_header_t) == HEADER_SIZE, "Header must be 32 bytes");
_Static_assert(sizeof(sig_block_t) == SIG_BLOCK_SIZE, "Sig block must be 72 bytes");

/* ============================================================
 * Initialization
 * ============================================================ */

esp_err_t payload_verify_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    /* Read backend verification public key from eFuse block 6 */
    esp_err_t ret = esp_efuse_read_block(
        EFUSE_BLK6,
        s_state.backend_pubkey,
        0,
        PUBKEY_SIZE * 8     /* bit count */
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read backend pubkey from eFuse: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verify pubkey is not all zeros (unprogrammed) */
    bool all_zero = true;
    for (int i = 0; i < PUBKEY_SIZE; i++) {
        if (s_state.backend_pubkey[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        ESP_LOGE(TAG, "Backend pubkey in eFuse is all zeros (not provisioned)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get our device ID hash for binding check */
    uint8_t device_uuid[16];
    ret = device_identity_get_uuid(device_uuid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device UUID: %s", esp_err_to_name(ret));
        return ret;
    }
    s_state.device_id_hash = esp_crc32_le(0, device_uuid, 16);

    s_state.initialized = true;
    ESP_LOGI(TAG, "Payload verifier initialized (device_id_hash=0x%08lX)",
             (unsigned long)s_state.device_id_hash);

    return ESP_OK;
}

/* ============================================================
 * Verification
 * ============================================================ */

/**
 * @brief Verify a signed bytecode payload.
 *
 * Performs the following checks in order:
 * 1. Size bounds check
 * 2. Header magic and version
 * 3. Device ID binding
 * 4. Instruction count bounds
 * 5. Signature block magic
 * 6. Signed length consistency
 * 7. Ed25519 signature verification
 *
 * @param payload       Pointer to the complete payload (header + code + consts + sig)
 * @param payload_len   Total length of the payload in bytes
 * @param result        Output: parsed header fields (if verification passes)
 *
 * @return  VERIFY_OK on success, specific error code on failure
 */
payload_verify_result_t payload_verify(
    const uint8_t *payload,
    size_t payload_len,
    payload_info_t *result)
{
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "Verifier not initialized");
        return VERIFY_ERR_NOT_INIT;
    }

    if (payload == NULL || result == NULL) {
        return VERIFY_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(payload_info_t));

    /* ---- Check 1: Size bounds ---- */
    if (payload_len < HEADER_SIZE + SIG_BLOCK_SIZE) {
        ESP_LOGE(TAG, "Payload too small: %u bytes (min %u)",
                 (unsigned)payload_len, HEADER_SIZE + SIG_BLOCK_SIZE);
        return VERIFY_ERR_TOO_SMALL;
    }
    if (payload_len > MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Payload too large: %u bytes (max %u)",
                 (unsigned)payload_len, MAX_PAYLOAD_SIZE);
        return VERIFY_ERR_TOO_LARGE;
    }

    /* ---- Check 2: Header magic and version ---- */
    const bytecode_header_t *header = (const bytecode_header_t *)payload;

    if (memcmp(header->magic, BYTECODE_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Invalid magic: %02X%02X%02X%02X",
                 header->magic[0], header->magic[1], header->magic[2], header->magic[3]);
        return VERIFY_ERR_BAD_MAGIC;
    }

    if (header->version != EXPECTED_VERSION) {
        ESP_LOGE(TAG, "Unsupported version: %u (expected %u)", header->version, EXPECTED_VERSION);
        return VERIFY_ERR_BAD_VERSION;
    }

    /* ---- Check 3: Device binding ---- */
    if (header->device_id_hash != s_state.device_id_hash) {
        ESP_LOGE(TAG, "Device ID mismatch: payload=0x%08lX, device=0x%08lX",
                 (unsigned long)header->device_id_hash,
                 (unsigned long)s_state.device_id_hash);
        return VERIFY_ERR_WRONG_DEVICE;
    }

    /* ---- Check 4: Instruction count bounds ---- */
    if (header->num_instructions == 0 || header->num_instructions > SYS_MAX_INSTRUCTIONS) {
        ESP_LOGE(TAG, "Invalid instruction count: %u (max %u)",
                 header->num_instructions, SYS_MAX_INSTRUCTIONS);
        return VERIFY_ERR_BAD_INSTR_COUNT;
    }

    /* ---- Check 5: Signature block ---- */
    const sig_block_t *sig_block = (const sig_block_t *)(payload + payload_len - SIG_BLOCK_SIZE);

    if (memcmp(sig_block->magic, SIG_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Invalid signature block magic");
        return VERIFY_ERR_BAD_SIG_MAGIC;
    }

    /* ---- Check 6: Signed length consistency ---- */
    uint32_t signed_length = sig_block->signed_length;
    if (signed_length + SIG_BLOCK_SIZE != payload_len) {
        ESP_LOGE(TAG, "Signed length mismatch: signed=%lu + sig=%u != total=%u",
                 (unsigned long)signed_length, SIG_BLOCK_SIZE, (unsigned)payload_len);
        return VERIFY_ERR_LENGTH_MISMATCH;
    }

    /* Verify signed_length covers exactly header + instructions + constants */
    size_t expected_signed = HEADER_SIZE
                           + (header->num_instructions * SYS_INSTRUCTION_SIZE)
                           + (payload_len - HEADER_SIZE
                              - (header->num_instructions * SYS_INSTRUCTION_SIZE)
                              - SIG_BLOCK_SIZE);

    if (signed_length != expected_signed) {
        ESP_LOGE(TAG, "Signed content size inconsistency");
        return VERIFY_ERR_LENGTH_MISMATCH;
    }

    /* ---- Check 7: Ed25519 signature verification ---- */
    const uint8_t *signed_content = payload;
    const uint8_t *signature = sig_block->signature;

    int ret = mbedtls_ed25519_verify(
        signature,
        SIGNATURE_SIZE,
        signed_content,
        signed_length,
        s_state.backend_pubkey,
        PUBKEY_SIZE
    );

    if (ret != 0) {
        ESP_LOGE(TAG, "SIGNATURE VERIFICATION FAILED (mbedtls error: -0x%04X)", (unsigned)-ret);
        return VERIFY_ERR_BAD_SIGNATURE;
    }

    /* ---- All checks passed ---- */
    ESP_LOGI(TAG, "Payload verified successfully (instructions=%u, constants=%u)",
             header->num_instructions, header->num_constants);

    /* Fill result */
    memcpy(result->program_id, header->program_id, 16);
    result->num_instructions = header->num_instructions;
    result->num_constants = header->num_constants;
    result->instructions_offset = HEADER_SIZE;
    result->constants_offset = HEADER_SIZE + (header->num_instructions * SYS_INSTRUCTION_SIZE);
    result->constants_length = signed_length - result->constants_offset;

    return VERIFY_OK;
}

/* ============================================================
 * Header File (payload_verify.h)
 * ============================================================ */

/*
#ifndef PAYLOAD_VERIFY_H
#define PAYLOAD_VERIFY_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VERIFY_OK                   = 0,
    VERIFY_ERR_NOT_INIT         = -1,
    VERIFY_ERR_INVALID_ARG      = -2,
    VERIFY_ERR_TOO_SMALL        = -3,
    VERIFY_ERR_TOO_LARGE        = -4,
    VERIFY_ERR_BAD_MAGIC        = -5,
    VERIFY_ERR_BAD_VERSION      = -6,
    VERIFY_ERR_WRONG_DEVICE     = -7,
    VERIFY_ERR_BAD_INSTR_COUNT  = -8,
    VERIFY_ERR_BAD_SIG_MAGIC    = -9,
    VERIFY_ERR_LENGTH_MISMATCH  = -10,
    VERIFY_ERR_BAD_SIGNATURE    = -11,
} payload_verify_result_t;

typedef struct {
    uint8_t     program_id[16];
    uint16_t    num_instructions;
    uint16_t    num_constants;
    size_t      instructions_offset;
    size_t      constants_offset;
    size_t      constants_length;
} payload_info_t;

esp_err_t payload_verify_init(void);

payload_verify_result_t payload_verify(
    const uint8_t *payload,
    size_t payload_len,
    payload_info_t *result);

#ifdef __cplusplus
}
#endif

#endif // PAYLOAD_VERIFY_H
*/
```

---

## 6. SQLite DDL (Backend Device Registry)

```sql
-- Parakram Backend Database Schema
-- SQLite with WAL mode for concurrent readers

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;

-- Users
CREATE TABLE users (
    user_id         TEXT PRIMARY KEY,           -- UUID
    username        TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,              -- Argon2id hash
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    last_login_at   TEXT
);

-- Driver registry (populated at backend startup)
CREATE TABLE drivers (
    driver_id       INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,        -- e.g., "drv_bme280"
    display_name    TEXT NOT NULL,
    version         TEXT NOT NULL,
    type            TEXT NOT NULL CHECK(type IN ('sensor','actuator','display','combo')),
    bus_type        TEXT NOT NULL CHECK(bus_type IN ('i2c','spi','uart','onewire','adc','gpio','pwm','rmt')),
    capabilities    TEXT NOT NULL,               -- JSON array: ["temperature","humidity",...]
    max_latency_us  INTEGER NOT NULL,
    min_interval_ms INTEGER NOT NULL,
    i2c_addresses   TEXT,                        -- JSON array: ["0x76","0x77"] or NULL
    config_schema   TEXT,                        -- JSON Schema for driver-specific config
    failure_modes   TEXT NOT NULL                 -- JSON array of failure mode objects
);

-- Board SKU registry
CREATE TABLE board_skus (
    sku             TEXT PRIMARY KEY,            -- e.g., "VDYT-S3-R1"
    name            TEXT NOT NULL,
    soc             TEXT NOT NULL DEFAULT 'ESP32-S3',
    flash_mb        INTEGER NOT NULL DEFAULT 16,
    psram_mb        INTEGER NOT NULL DEFAULT 8,
    pin_map         TEXT NOT NULL,               -- JSON: full pin mapping
    default_devices TEXT NOT NULL,               -- JSON: default connected devices
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Paired devices
CREATE TABLE devices (
    device_id       TEXT PRIMARY KEY,            -- UUID (from eFuse)
    user_id         TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    board_sku       TEXT NOT NULL REFERENCES board_skus(sku),
    name            TEXT NOT NULL DEFAULT 'My Parakram Device',
    device_pubkey   TEXT NOT NULL,               -- Base64-encoded Ed25519 public key
    firmware_version TEXT,
    ip_address      TEXT,
    ble_address     TEXT,
    status          TEXT NOT NULL DEFAULT 'offline'
                    CHECK(status IN ('online','offline','deploying','error')),
    active_program_id TEXT,
    error_count     INTEGER NOT NULL DEFAULT 0,
    paired_at       TEXT NOT NULL DEFAULT (datetime('now')),
    last_seen_at    TEXT
);

CREATE INDEX idx_devices_user ON devices(user_id);
CREATE INDEX idx_devices_status ON devices(status);

-- Projects
CREATE TABLE projects (
    project_id      TEXT PRIMARY KEY,            -- UUID
    user_id         TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    device_id       TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    name            TEXT NOT NULL,
    description     TEXT,
    ir_json         TEXT,                         -- Full IR JSON document
    bytecode_hash   TEXT,                         -- SHA-256 of last compiled bytecode
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    deployed_at     TEXT
);

CREATE INDEX idx_projects_user ON projects(user_id);
CREATE INDEX idx_projects_device ON projects(device_id);

-- Telemetry data
CREATE TABLE telemetry (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id       TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    pipeline_id     TEXT NOT NULL,
    timestamp       TEXT NOT NULL DEFAULT (datetime('now')),
    tick            INTEGER,
    values_json     TEXT NOT NULL,                -- JSON object: {"temperature": 28.4, ...}
    UNIQUE(device_id, pipeline_id, timestamp)
);

CREATE INDEX idx_telemetry_device_time ON telemetry(device_id, timestamp DESC);
CREATE INDEX idx_telemetry_pipeline ON telemetry(device_id, pipeline_id, timestamp DESC);

-- LLM interaction log
CREATE TABLE llm_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         TEXT NOT NULL REFERENCES users(user_id),
    request_type    TEXT NOT NULL CHECK(request_type IN ('feasibility','generation','retry')),
    model           TEXT NOT NULL,
    input_prompt    TEXT NOT NULL,
    output_response TEXT NOT NULL,
    valid           INTEGER NOT NULL DEFAULT 0,   -- 0=invalid, 1=valid
    processing_ms   INTEGER NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_llm_logs_user ON llm_logs(user_id, created_at DESC);

-- Deployment history
CREATE TABLE deployments (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id      TEXT NOT NULL REFERENCES projects(project_id) ON DELETE CASCADE,
    device_id       TEXT NOT NULL REFERENCES devices(device_id),
    bytecode_hash   TEXT NOT NULL,
    transfer_method TEXT NOT NULL CHECK(transfer_method IN ('wifi','ble')),
    status          TEXT NOT NULL CHECK(status IN ('success','failed','timeout')),
    error_message   TEXT,
    deployed_at     TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_deployments_project ON deployments(project_id, deployed_at DESC);
```
