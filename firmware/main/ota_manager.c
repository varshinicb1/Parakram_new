/**
 * @file ota_manager.c
 * @brief OTA firmware update manager — ESP32-S3 / ESP-IDF 5.1 implementation.
 *
 * Flow
 * ────
 *  ota_check_task  (FreeRTOS, period = OTA_CHECK_INTERVAL_MS)
 *    └─ GET /api/ota/check/{device_id}          → { has_update, hash }
 *         └─ if has_update → ota_download_and_apply()
 *              ├─ GET /api/ota/manifest/{device_id} → { hash, size }
 *              ├─ esp_ota_begin()
 *              ├─ GET /api/ota/chunk/{device_id}  → binary stream
 *              │    └─ esp_ota_write() per HTTP event chunk
 *              ├─ esp_ota_end()
 *              ├─ SHA-256 verify (mbedtls)
 *              ├─ esp_ota_set_boot_partition()
 *              └─ esp_restart()
 *
 * All status mutations are protected by status_mutex (FreeRTOS mutex).
 * JSON is parsed with strstr() — no cJSON dependency.
 */

#include "ota_manager.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ── Module-level constants ──────────────────────────────────────────────── */

static const char *TAG = "OTA_MGR";

/** Period between automatic update checks (30 minutes). */
#define OTA_CHECK_INTERVAL_MS       (30UL * 60UL * 1000UL)

/** Stack depth for the background FreeRTOS task (words). */
#define OTA_TASK_STACK_WORDS        8192

/** Priority of the background task — low so it doesn't starve the VM. */
#define OTA_TASK_PRIORITY           2

/** HTTP receive buffer (bytes) — shared across all requests. */
#define OTA_HTTP_BUF_SIZE           4096

/** Maximum length for assembled HTTP response body (check + manifest). */
#define OTA_RESPONSE_BUF_SIZE       1024

/** Maximum URL string length. */
#define OTA_URL_MAX_LEN             512

/** Custom header sent with the chunk download request. */
#define OTA_DEVICE_KEY_HEADER       "X-Device-Key"
#define OTA_DEVICE_KEY_VALUE        "parakram"

/* ── Module state ────────────────────────────────────────────────────────── */

/** Guarded by status_mutex. */
static ota_status_t     s_status;
static SemaphoreHandle_t s_status_mutex = NULL;

/** Configuration strings — set once in ota_manager_init(). */
static char s_base_url[OTA_URL_MAX_LEN];
static char s_device_id[64];
static char s_auth_token[256];

/** Set by ota_manager_trigger_check() to request an immediate check. */
static volatile bool s_trigger_check = false;

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * Update the shared status struct under the mutex.
 * Only the fields provided are updated; call site fills what it needs.
 */
static void status_set(ota_state_t state,
                        uint32_t bytes_dl,
                        uint32_t total_bytes,
                        const char *version,
                        const char *error_msg)
{
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_status.state            = state;
        s_status.bytes_downloaded = bytes_dl;
        s_status.total_bytes      = total_bytes;

        if (version) {
            strncpy(s_status.version, version, sizeof(s_status.version) - 1);
            s_status.version[sizeof(s_status.version) - 1] = '\0';
        }
        if (error_msg) {
            strncpy(s_status.error_msg, error_msg, sizeof(s_status.error_msg) - 1);
            s_status.error_msg[sizeof(s_status.error_msg) - 1] = '\0';
        }

        xSemaphoreGive(s_status_mutex);
    }
}

/**
 * Minimal strstr()-based JSON value extractor.
 *
 * Locates the first occurrence of `key` (as `"key":`) in `json` and
 * copies its value into `out_buf` (up to `buf_len-1` chars + NUL).
 *
 * Handles:
 *   "key": "string value"
 *   "key": 12345
 *   "key": true / false
 *
 * @return true on success, false if the key was not found.
 */
static bool json_extract_value(const char *json,
                                const char *key,
                                char *out_buf,
                                size_t buf_len)
{
    /* Build search pattern: "key": */
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *pos = strstr(json, pattern);
    if (!pos) {
        return false;
    }

    /* Skip past the pattern and any whitespace. */
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
        pos++;
    }

    size_t i = 0;
    if (*pos == '"') {
        /* String value — read until closing quote. */
        pos++; /* skip opening quote */
        while (*pos != '\0' && *pos != '"' && i < buf_len - 1) {
            out_buf[i++] = *pos++;
        }
    } else {
        /* Number / boolean — read until delimiter. */
        while (*pos != '\0' && *pos != ',' && *pos != '}' &&
               *pos != ' '  && *pos != '\n' && i < buf_len - 1) {
            out_buf[i++] = *pos++;
        }
    }
    out_buf[i] = '\0';
    return (i > 0);
}

/* ── HTTP helper structures ──────────────────────────────────────────────── */

/**
 * Context passed to esp_http_client event handler when accumulating a
 * small response body (check / manifest endpoints).
 */
typedef struct {
    char    *buf;       /**< Pre-allocated buffer */
    size_t   buf_len;   /**< Buffer capacity including NUL */
    size_t   filled;    /**< Bytes written so far */
} http_body_ctx_t;

/**
 * Context passed to event handler during the binary chunk download.
 */
typedef struct {
    esp_ota_handle_t ota_handle;
    mbedtls_sha256_context sha_ctx;
    uint32_t bytes_written;
    bool     error;
} http_chunk_ctx_t;

/* ── HTTP event handler — accumulate small bodies ────────────────────────── */

static esp_err_t http_body_event_handler(esp_http_client_event_t *evt)
{
    http_body_ctx_t *ctx = (http_body_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        size_t space = ctx->buf_len - ctx->filled - 1; /* -1 for NUL */
        size_t to_copy = (size_t)evt->data_len < space
                         ? (size_t)evt->data_len : space;
        memcpy(ctx->buf + ctx->filled, evt->data, to_copy);
        ctx->filled += to_copy;
        ctx->buf[ctx->filled] = '\0';
    }
    return ESP_OK;
}

/* ── HTTP event handler — OTA chunk streaming ────────────────────────────── */

static esp_err_t http_chunk_event_handler(esp_http_client_event_t *evt)
{
    http_chunk_ctx_t *ctx = (http_chunk_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && !ctx->error) {
        /* Feed chunk into OTA write pipeline. */
        esp_err_t wr = esp_ota_write(ctx->ota_handle,
                                      evt->data,
                                      (size_t)evt->data_len);
        if (wr != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(wr));
            ctx->error = true;
            return ESP_FAIL;
        }

        /* Accumulate SHA-256 hash. */
        mbedtls_sha256_update(&ctx->sha_ctx, evt->data, (size_t)evt->data_len);

        ctx->bytes_written += (uint32_t)evt->data_len;

        /* Update progress in shared status (best-effort — skip on mutex timeout). */
        if (xSemaphoreTake(s_status_mutex, 0) == pdTRUE) {
            s_status.bytes_downloaded = ctx->bytes_written;
            xSemaphoreGive(s_status_mutex);
        }
    }
    return ESP_OK;
}

/* ── Core logic ──────────────────────────────────────────────────────────── */

/**
 * Build the common esp_http_client_config_t used for every request.
 * Caller supplies the full URL and event handler / user_data.
 */
static esp_http_client_handle_t make_http_client(
        const char *url,
        esp_err_t (*event_handler)(esp_http_client_event_t *),
        void *user_data)
{
    esp_http_client_config_t cfg = {
        .url                    = url,
        .event_handler          = event_handler,
        .user_data              = user_data,
        .timeout_ms             = 10000,
        .buffer_size            = OTA_HTTP_BUF_SIZE,
        .buffer_size_tx         = OTA_HTTP_BUF_SIZE,
        .skip_cert_common_name_check = true,
        .keep_alive_enable      = false,
    };
    return esp_http_client_init(&cfg);
}

/**
 * Perform a GET request and collect the body into *body_out.
 * *body_out must point to a zeroed http_body_ctx_t whose buf is allocated.
 * Returns the HTTP status code, or -1 on transport error.
 */
static int http_get_body(const char *url,
                          const char *auth_token,
                          const char *extra_header_key,
                          const char *extra_header_val,
                          http_body_ctx_t *body_ctx)
{
    esp_http_client_handle_t client =
        make_http_client(url, http_body_event_handler, body_ctx);
    if (!client) {
        ESP_LOGE(TAG, "http_client_init failed for %s", url);
        return -1;
    }

    /* Authorization header */
    char auth_hdr[320];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", auth_token);
    esp_http_client_set_header(client, "Authorization", auth_hdr);

    if (extra_header_key && extra_header_val) {
        esp_http_client_set_header(client, extra_header_key, extra_header_val);
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGE(TAG, "HTTP GET %s failed: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status;
}

/**
 * Download firmware binary, stream into OTA partition, and compute SHA-256.
 *
 * @param url           Full chunk endpoint URL.
 * @param ota_handle    Handle from esp_ota_begin().
 * @param sha_out       32-byte buffer to receive the computed digest.
 * @param bytes_out     Receives total bytes written.
 * @return ESP_OK on success.
 */
static esp_err_t download_firmware_chunk(const char *url,
                                          uint32_t total_size,
                                          esp_ota_handle_t ota_handle,
                                          uint8_t sha_out[32],
                                          uint32_t *bytes_out)
{
    http_chunk_ctx_t ctx = {
        .ota_handle    = ota_handle,
        .bytes_written = 0,
        .error         = false,
    };
    mbedtls_sha256_init(&ctx.sha_ctx);
    mbedtls_sha256_starts(&ctx.sha_ctx, 0 /* 0 = SHA-256 */);

    /* Update status with total_bytes before download begins. */
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_status.total_bytes      = total_size;
        s_status.bytes_downloaded = 0;
        xSemaphoreGive(s_status_mutex);
    }

    esp_http_client_handle_t client =
        make_http_client(url, http_chunk_event_handler, &ctx);
    if (!client) {
        mbedtls_sha256_free(&ctx.sha_ctx);
        return ESP_FAIL;
    }

    char auth_hdr[320];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_auth_token);
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_header(client, OTA_DEVICE_KEY_HEADER, OTA_DEVICE_KEY_VALUE);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = -1;
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "Chunk download failed: transport=%s, http=%d",
                 esp_err_to_name(err), status_code);
        mbedtls_sha256_free(&ctx.sha_ctx);
        return ESP_FAIL;
    }

    if (ctx.error) {
        mbedtls_sha256_free(&ctx.sha_ctx);
        return ESP_FAIL;
    }

    mbedtls_sha256_finish(&ctx.sha_ctx, sha_out);
    mbedtls_sha256_free(&ctx.sha_ctx);

    *bytes_out = ctx.bytes_written;
    return ESP_OK;
}

/**
 * Convert a 32-byte binary SHA-256 digest to a 64-character lower-case
 * hex string (+ NUL terminator — caller must supply >= 65 bytes).
 */
static void sha256_to_hex(const uint8_t digest[32], char hex_out[65])
{
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex_out[i * 2 + 0] = HEX[(digest[i] >> 4) & 0xF];
        hex_out[i * 2 + 1] = HEX[(digest[i])      & 0xF];
    }
    hex_out[64] = '\0';
}

/* ── ota_download_and_apply ──────────────────────────────────────────────── */

/**
 * Orchestrate the full download-verify-apply sequence.
 * Called from ota_check_task() when has_update is true.
 */
static void ota_download_and_apply(void)
{
    ESP_LOGI(TAG, "Starting OTA download and apply sequence");

    /* ── 1. Fetch manifest ── */
    status_set(OTA_STATE_DOWNLOADING, 0, 0, NULL, NULL);

    char manifest_url[OTA_URL_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(manifest_url, sizeof(manifest_url),
             "%s/api/ota/manifest/%s", s_base_url, s_device_id);
#pragma GCC diagnostic pop

    char manifest_body_buf[OTA_RESPONSE_BUF_SIZE];
    memset(manifest_body_buf, 0, sizeof(manifest_body_buf));
    http_body_ctx_t manifest_ctx = {
        .buf     = manifest_body_buf,
        .buf_len = sizeof(manifest_body_buf),
        .filled  = 0,
    };

    int manifest_status = http_get_body(manifest_url, s_auth_token,
                                         NULL, NULL, &manifest_ctx);
    if (manifest_status != 200) {
        ESP_LOGE(TAG, "Manifest fetch failed (HTTP %d)", manifest_status);
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "Manifest fetch failed");
        return;
    }

    ESP_LOGI(TAG, "Manifest: %s", manifest_body_buf);

    /* Parse manifest: { "hash": "...", "size": 12345 } */
    char manifest_hash[65] = {0};
    char size_str[16]       = {0};

    if (!json_extract_value(manifest_body_buf, "hash", manifest_hash, sizeof(manifest_hash)) ||
        !json_extract_value(manifest_body_buf, "size", size_str, sizeof(size_str))) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "Manifest parse error");
        return;
    }

    uint32_t firmware_size = (uint32_t)atoi(size_str);
    if (firmware_size == 0) {
        ESP_LOGE(TAG, "Invalid firmware size in manifest: %s", size_str);
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "Invalid firmware size");
        return;
    }

    ESP_LOGI(TAG, "Manifest OK — size=%lu, hash=%.16s...",
             (unsigned long)firmware_size, manifest_hash);

    /* ── 2. Find OTA partition ── */
    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found in partition table");
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "No OTA partition");
        return;
    }

    ESP_LOGI(TAG, "Writing to OTA partition: %s (offset=0x%08lx, size=0x%08lx)",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    if (firmware_size > update_partition->size) {
        ESP_LOGE(TAG, "Firmware (%lu bytes) exceeds partition (%lu bytes)",
                 (unsigned long)firmware_size,
                 (unsigned long)update_partition->size);
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "Firmware too large for partition");
        return;
    }

    /* ── 3. Begin OTA write ── */
    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition,
                                   OTA_WITH_SEQUENTIAL_WRITES,
                                   &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "OTA begin failed");
        return;
    }

    /* ── 4. Download + stream firmware ── */
    char chunk_url[OTA_URL_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(chunk_url, sizeof(chunk_url),
             "%s/api/ota/chunk/%s", s_base_url, s_device_id);
#pragma GCC diagnostic pop

    uint8_t  computed_sha[32] = {0};
    uint32_t bytes_written    = 0;

    err = download_firmware_chunk(chunk_url, firmware_size,
                                   update_handle, computed_sha, &bytes_written);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware download failed");
        esp_ota_abort(update_handle);
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "Firmware download failed");
        return;
    }

    ESP_LOGI(TAG, "Downloaded %lu bytes", (unsigned long)bytes_written);

    /* ── 5. Finalise OTA write ── */
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        status_set(OTA_STATE_ERROR, 0, 0, NULL, "OTA end failed (image invalid?)");
        return;
    }

    /* ── 6. SHA-256 verification ── */
    status_set(OTA_STATE_VERIFYING, bytes_written, firmware_size, NULL, NULL);

    char computed_hex[65];
    sha256_to_hex(computed_sha, computed_hex);

    /* Case-insensitive comparison — manifest hash may be upper or lower case. */
    char manifest_hash_lower[65];
    strncpy(manifest_hash_lower, manifest_hash, sizeof(manifest_hash_lower));
    for (int i = 0; manifest_hash_lower[i]; i++) {
        if (manifest_hash_lower[i] >= 'A' && manifest_hash_lower[i] <= 'F') {
            manifest_hash_lower[i] = (char)(manifest_hash_lower[i] + ('a' - 'A'));
        }
    }

    ESP_LOGI(TAG, "Expected SHA-256: %s", manifest_hash_lower);
    ESP_LOGI(TAG, "Computed SHA-256: %s", computed_hex);

    if (strcmp(computed_hex, manifest_hash_lower) != 0) {
        ESP_LOGE(TAG, "SHA-256 MISMATCH — aborting OTA");
        status_set(OTA_STATE_ERROR, bytes_written, firmware_size, NULL,
                   "SHA-256 verification failed");
        return;
    }

    ESP_LOGI(TAG, "SHA-256 verified OK");

    /* ── 7. Set new boot partition ── */
    status_set(OTA_STATE_APPLYING, bytes_written, firmware_size, NULL, NULL);

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        status_set(OTA_STATE_ERROR, bytes_written, firmware_size, NULL,
                   "Set boot partition failed");
        return;
    }

    ESP_LOGI(TAG, "OTA complete — new boot partition set to '%s'",
             update_partition->label);
    ESP_LOGI(TAG, "Rebooting into new firmware in 3 seconds...");

    status_set(OTA_STATE_REBOOT_PENDING, bytes_written, firmware_size, NULL, NULL);

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    /* Does not return */
}

/* ── FreeRTOS task ───────────────────────────────────────────────────────── */

static void ota_check_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "OTA check task started (interval=%lu min)",
             (unsigned long)(OTA_CHECK_INTERVAL_MS / 60000UL));

    /* Small initial delay to let the rest of the system settle after boot. */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        /* ── Poll backend for update ── */
        status_set(OTA_STATE_CHECKING, 0, 0, NULL, NULL);
        ESP_LOGI(TAG, "Checking for OTA update...");

        char check_url[OTA_URL_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(check_url, sizeof(check_url),
                 "%s/api/ota/check/%s", s_base_url, s_device_id);
#pragma GCC diagnostic pop

        char check_body_buf[OTA_RESPONSE_BUF_SIZE];
        memset(check_body_buf, 0, sizeof(check_body_buf));
        http_body_ctx_t check_ctx = {
            .buf     = check_body_buf,
            .buf_len = sizeof(check_body_buf),
            .filled  = 0,
        };

        int http_status = http_get_body(check_url, s_auth_token,
                                         NULL, NULL, &check_ctx);

        if (http_status != 200) {
            ESP_LOGW(TAG, "OTA check HTTP %d — will retry at next interval",
                     http_status);
            status_set(OTA_STATE_IDLE, 0, 0, NULL, NULL);
            goto sleep_and_check_trigger;
        }

        ESP_LOGI(TAG, "Check response: %s", check_body_buf);

        /* Parse: { "has_update": true/false, "hash": "..." } */
        char has_update_str[8] = {0};
        if (!json_extract_value(check_body_buf, "has_update",
                                 has_update_str, sizeof(has_update_str))) {
            ESP_LOGE(TAG, "Failed to parse has_update from check response");
            status_set(OTA_STATE_IDLE, 0, 0, NULL, NULL);
            goto sleep_and_check_trigger;
        }

        bool has_update = (strcmp(has_update_str, "true") == 0);

        if (!has_update) {
            ESP_LOGI(TAG, "No update available");
            status_set(OTA_STATE_IDLE, 0, 0, NULL, NULL);
            goto sleep_and_check_trigger;
        }

        ESP_LOGI(TAG, "Update available — starting download");
        ota_download_and_apply();
        /* If we get here, ota_download_and_apply() did not call esp_restart().
         * That means an error occurred — status is already set to ERROR.
         * Fall through to sleep so the task keeps running. */

sleep_and_check_trigger:
        /* Sleep in 1-second slices so we can react to ota_manager_trigger_check()
         * without blocking for a full 30 minutes. */
        {
            const uint32_t slices = OTA_CHECK_INTERVAL_MS / 1000;
            for (uint32_t i = 0; i < slices; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (s_trigger_check) {
                    s_trigger_check = false;
                    ESP_LOGI(TAG, "Immediate OTA check triggered");
                    break;
                }
            }
        }
    }
    /* Unreachable */
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ota_manager_init(const char *backend_base_url,
                      const char *device_id,
                      const char *auth_token)
{
    if (!backend_base_url || !device_id || !auth_token) {
        ESP_LOGE(TAG, "ota_manager_init: NULL argument(s)");
        return;
    }

    /* Copy configuration into module-level storage. */
    strncpy(s_base_url,    backend_base_url, sizeof(s_base_url)    - 1);
    strncpy(s_device_id,   device_id,        sizeof(s_device_id)   - 1);
    strncpy(s_auth_token,  auth_token,        sizeof(s_auth_token)  - 1);
    s_base_url[sizeof(s_base_url)     - 1] = '\0';
    s_device_id[sizeof(s_device_id)   - 1] = '\0';
    s_auth_token[sizeof(s_auth_token) - 1] = '\0';

    /* Initialise status. */
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_STATE_IDLE;

    /* Create mutex. */
    s_status_mutex = xSemaphoreCreateMutex();
    if (!s_status_mutex) {
        ESP_LOGE(TAG, "Failed to create status mutex — OTA disabled");
        return;
    }

    /* Spawn background task. */
    BaseType_t rc = xTaskCreate(ota_check_task,
                                 "ota_check",
                                 OTA_TASK_STACK_WORDS,
                                 NULL,
                                 OTA_TASK_PRIORITY,
                                 NULL);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task (rc=%d)", (int)rc);
        return;
    }

    ESP_LOGI(TAG, "OTA manager initialised — device_id=%s, url=%s",
             s_device_id, s_base_url);
}

ota_status_t ota_manager_get_status(void)
{
    ota_status_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = OTA_STATE_IDLE;

    if (!s_status_mutex) {
        return snapshot;
    }

    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        snapshot = s_status;    /* struct copy */
        xSemaphoreGive(s_status_mutex);
    } else {
        ESP_LOGW(TAG, "ota_manager_get_status: mutex timeout");
    }

    return snapshot;
}

void ota_manager_trigger_check(void)
{
    if (!s_status_mutex) {
        ESP_LOGW(TAG, "ota_manager_trigger_check called before init");
        return;
    }

    /* Only trigger if we are currently idle or in error — don't interrupt an
     * active download. */
    ota_status_t snap = ota_manager_get_status();
    if (snap.state == OTA_STATE_IDLE || snap.state == OTA_STATE_ERROR) {
        s_trigger_check = true;
        ESP_LOGI(TAG, "OTA check triggered by caller");
    } else {
        ESP_LOGW(TAG, "ota_manager_trigger_check: OTA already in progress (state=%d)",
                 (int)snap.state);
    }
}
