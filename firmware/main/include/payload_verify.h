/**
 * @file payload_verify.h
 * @brief Ed25519 payload verification using mbedTLS.
 */
#ifndef PAYLOAD_VERIFY_H
#define PAYLOAD_VERIFY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "system_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t     magic[4];
    uint16_t    version;
    uint16_t    flags;
    uint8_t     program_id[16];
    uint32_t    device_hash;
    uint16_t    num_instructions;
    uint16_t    num_constants;
} __attribute__((packed)) bytecode_header_t;

typedef struct {
    uint8_t     magic[4];
    uint32_t    signed_length;
    uint8_t     signature[SYS_SIGNATURE_SIZE];
} __attribute__((packed)) signature_block_t;

typedef struct {
    bool            valid;
    bytecode_header_t header;
    const uint8_t  *instructions;
    uint16_t        instructions_size;
    const uint8_t  *constants;
    uint16_t        constants_size;
} verified_payload_t;

esp_err_t payload_verify_init(const uint8_t pubkey[SYS_PUBKEY_SIZE]);
esp_err_t payload_verify(const uint8_t *payload, uint32_t payload_len,
                         uint32_t expected_device_hash, verified_payload_t *out);

#ifdef __cplusplus
}
#endif
#endif /* PAYLOAD_VERIFY_H */
