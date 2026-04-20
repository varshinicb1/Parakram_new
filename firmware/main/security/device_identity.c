/**
 * @file device_identity.c
 * @brief Device identity — UUID and CRC32 hash for payload binding.
 */

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "DEVID";

static uint8_t s_mac[6];
static uint32_t s_device_hash = 0;
static bool s_initialized = false;

/* CRC32 (matching backend crc32fast) */
static uint32_t crc32_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
        else crc >>= 1;
    }
    return crc;
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_byte(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFF;
}

esp_err_t device_identity_init(void) {
    esp_err_t err = esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC: %d", err);
        return err;
    }

    /* Build device UUID string from MAC: "VDYT-XXXXXXXXXXXX" */
    char uuid_str[32];
    snprintf(uuid_str, sizeof(uuid_str), "VDYT-%02X%02X%02X%02X%02X%02X",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);

    /* CRC32 of the UUID string — must match what the backend computes */
    s_device_hash = crc32((const uint8_t *)uuid_str, strlen(uuid_str));
    s_initialized = true;

    ESP_LOGI(TAG, "Device ID: %s, hash: 0x%08lX", uuid_str, (unsigned long)s_device_hash);
    return ESP_OK;
}

uint32_t device_identity_get_hash(void) {
    return s_device_hash;
}

const uint8_t *device_identity_get_mac(void) {
    return s_mac;
}
