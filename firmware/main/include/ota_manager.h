/**
 * @file ota_manager.h
 * @brief OTA (Over-The-Air) firmware update manager for Parakram ESP32-S3.
 *
 * Spawns a background FreeRTOS task that polls the Parakram backend every
 * 30 minutes for a new firmware image.  If an update is available the
 * manager downloads, SHA-256 verifies, and applies it before rebooting.
 *
 * All public functions are thread-safe.
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── OTA state machine ───────────────────────────────────────────────────── */

typedef enum {
    OTA_STATE_IDLE = 0,       /**< No update activity */
    OTA_STATE_CHECKING,       /**< Contacting backend to check for updates */
    OTA_STATE_DOWNLOADING,    /**< Streaming firmware image from backend */
    OTA_STATE_VERIFYING,      /**< SHA-256 hash verification in progress */
    OTA_STATE_APPLYING,       /**< Setting new boot partition */
    OTA_STATE_REBOOT_PENDING, /**< Update applied — waiting to reboot */
    OTA_STATE_ERROR,          /**< An error occurred; see error_msg */
} ota_state_t;

/* ── Status snapshot (returned by value — no pointers) ───────────────────── */

typedef struct {
    ota_state_t state;
    uint32_t    bytes_downloaded;
    uint32_t    total_bytes;
    char        version[32];    /**< Firmware version string from manifest */
    char        error_msg[128]; /**< Human-readable error description */
} ota_status_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the OTA manager and start its background task.
 *
 * Must be called once after WiFi is up.  The task begins its first check
 * immediately and then repeats every 30 minutes.
 *
 * @param backend_base_url  Base URL of the Parakram backend,
 *                          e.g. "http://192.168.1.10:8400".  Not freed.
 * @param device_id         Null-terminated device identifier string.
 * @param auth_token        Bearer token for Authorization header.
 */
void ota_manager_init(const char *backend_base_url,
                      const char *device_id,
                      const char *auth_token);

/**
 * @brief Return a snapshot of the current OTA status.
 *
 * Thread-safe: acquires the internal mutex before copying state.
 *
 * @return ota_status_t  Value copy — safe to read without holding any lock.
 */
ota_status_t ota_manager_get_status(void);

/**
 * @brief Request an immediate OTA check outside the regular schedule.
 *
 * Thread-safe.  Has no effect if a check or download is already running.
 */
void ota_manager_trigger_check(void);

#ifdef __cplusplus
}
#endif
