/**
 * @file payload_verify.c
 * @brief Ed25519 payload signature verification via PSA Crypto API (mbedTLS 3.x / ESP-IDF 5.1).
 *
 * No external library needed — uses the PSA Crypto API included with ESP-IDF 5.1.
 * Requires CONFIG_MBEDTLS_PSA_CRYPTO_C=y and CONFIG_MBEDTLS_EDDSA_C=y in sdkconfig.
 */

#include "payload_verify.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include <string.h>

static const char *TAG = "PAYVRFY";

static uint8_t s_pubkey[SYS_PUBKEY_SIZE]; /* 32-byte Ed25519 public key */
static bool    s_initialized = false;
static bool    s_psa_ready   = false;

esp_err_t payload_verify_init(const uint8_t pubkey[SYS_PUBKEY_SIZE]) {
    /* Initialize PSA Crypto subsystem (idempotent) */
    psa_status_t psa_st = psa_crypto_init();
    if (psa_st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)psa_st);
        return ESP_FAIL;
    }
    s_psa_ready = true;

    memcpy(s_pubkey, pubkey, SYS_PUBKEY_SIZE);
    s_initialized = true;
    ESP_LOGI(TAG, "Payload verifier initialized (Ed25519/PSA)");
    return ESP_OK;
}

/**
 * Verify a 64-byte Ed25519 signature over msg[0..msg_len) using the stored public key.
 * Returns 0 on success, -1 on failure.
 */
static int ed25519_verify_psa(const uint8_t sig[64], const uint8_t *msg, size_t msg_len) {
    psa_key_id_t         key_id;
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attrs,
        PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_algorithm(&attrs, PSA_ALG_PURE_EDDSA);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_bits(&attrs, 255);

    psa_status_t st = psa_import_key(&attrs, s_pubkey, SYS_PUBKEY_SIZE, &key_id);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %d", (int)st);
        return -1;
    }

    st = psa_verify_message(key_id, PSA_ALG_PURE_EDDSA, msg, msg_len, sig, 64);
    psa_destroy_key(key_id);

    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_verify_message failed: %d", (int)st);
        return -1;
    }
    return 0;
}

esp_err_t payload_verify(const uint8_t *payload, uint32_t payload_len,
                         uint32_t expected_device_hash, verified_payload_t *out) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "Verifier not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_psa_ready) {
        ESP_LOGE(TAG, "PSA crypto not ready");
        return ESP_FAIL;
    }
    if (out == NULL || payload == NULL) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(verified_payload_t));

    /* Minimum: header(32) + 1 instruction(8) + sig_block(72) */
    if (payload_len < SYS_HEADER_SIZE + SYS_INSTRUCTION_SIZE + SYS_SIG_BLOCK_SIZE) {
        ESP_LOGE(TAG, "Payload too small: %lu bytes", (unsigned long)payload_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const bytecode_header_t *hdr = (const bytecode_header_t *)payload;

    /* Magic check */
    if (hdr->magic[0] != SYS_BYTECODE_MAGIC_0 || hdr->magic[1] != SYS_BYTECODE_MAGIC_1 ||
        hdr->magic[2] != SYS_BYTECODE_MAGIC_2 || hdr->magic[3] != SYS_BYTECODE_MAGIC_3) {
        ESP_LOGE(TAG, "Invalid magic: %02X %02X %02X %02X",
                 hdr->magic[0], hdr->magic[1], hdr->magic[2], hdr->magic[3]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Version check */
    if (hdr->version != SYS_BYTECODE_VERSION) {
        ESP_LOGE(TAG, "Unsupported bytecode version: %d", hdr->version);
        return ESP_ERR_INVALID_VERSION;
    }

    /* Device binding check — prevents cross-device payload transfer */
    if (hdr->device_hash != expected_device_hash) {
        ESP_LOGE(TAG, "Device hash mismatch: expected 0x%08lX, got 0x%08lX",
                 (unsigned long)expected_device_hash,
                 (unsigned long)hdr->device_hash);
        return ESP_ERR_INVALID_ARG;
    }

    /* Instruction count sanity */
    if (hdr->num_instructions == 0 || hdr->num_instructions > SYS_MAX_INSTRUCTIONS) {
        ESP_LOGE(TAG, "Invalid instruction count: %d", hdr->num_instructions);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t instr_size         = (uint32_t)hdr->num_instructions * SYS_INSTRUCTION_SIZE;
    uint32_t signed_content_len = payload_len - SYS_SIG_BLOCK_SIZE;

    const signature_block_t *sig_block =
        (const signature_block_t *)(payload + signed_content_len);

    /* Signature block magic */
    if (sig_block->magic[0] != 'S' || sig_block->magic[1] != 'I' ||
        sig_block->magic[2] != 'G' || sig_block->magic[3] != '0') {
        ESP_LOGE(TAG, "Invalid signature block magic");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (sig_block->signed_length != signed_content_len) {
        ESP_LOGE(TAG, "Signed length mismatch: block=%lu actual=%lu",
                 (unsigned long)sig_block->signed_length,
                 (unsigned long)signed_content_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Real Ed25519 signature verification via PSA Crypto */
    int sig_result = ed25519_verify_psa(sig_block->signature, payload, signed_content_len);
    if (sig_result != 0) {
        ESP_LOGE(TAG, "Signature verification FAILED — payload rejected");
        return ESP_ERR_INVALID_CRC;
    }

    uint32_t const_offset = SYS_HEADER_SIZE + instr_size;
    uint32_t const_size   = signed_content_len - const_offset;

    out->valid            = true;
    out->header           = *hdr;
    out->instructions     = payload + SYS_HEADER_SIZE;
    out->instructions_size = instr_size;
    out->constants        = (const_size > 0) ? (payload + const_offset) : NULL;
    out->constants_size   = const_size;

    ESP_LOGI(TAG, "Payload verified OK: %d instructions, %d constants",
             hdr->num_instructions, hdr->num_constants);
    return ESP_OK;
}
