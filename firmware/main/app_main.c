/**
 * @file app_main.c
 * @brief Parakram firmware entry point — boot sequence orchestrator.
 *
 * Boot sequence:
 *   1. NVS init
 *   2. Event bus init
 *   3. Fault handler init
 *   4. State store init
 *   5. VM init
 *   6. Driver registry init
 *   7. Device identity init
 *   8. Payload verifier init
 *   9. Scheduler init
 *  10. WiFi init + start
 *  11. BLE init + advertise
 *  12. Load program from flash (if exists)
 *  13. Start scheduler
 *  14. Watchdog init
 */

#include "vm.h"
#include "scheduler.h"
#include "state_store.h"
#include "event_bus.h"
#include "driver_registry.h"
#include "payload_verify.h"
#include "comms.h"
#include "safety.h"
#include "system_config.h"
#include "ota_manager.h"
#include "device_config.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "PARAKRAM";

/* External driver vtables */
extern const driver_vtable_t drv_bme280_vtable;
extern const driver_vtable_t drv_relay_vtable;

/* External functions */
extern esp_err_t device_identity_init(void);
extern uint32_t device_identity_get_hash(void);
extern esp_err_t i2c_bus_init(uint8_t port, int sda, int scl, uint32_t freq);

/* Development-mode verification key (32 bytes of zeros = accept all) */
static const uint8_t DEV_PUBKEY[SYS_PUBKEY_SIZE] = {0};

/**
 * Handle incoming deployment payload (from WiFi or BLE).
 */
static void on_payload_received(const uint8_t *data, uint32_t len) {
    ESP_LOGI(TAG, "Payload received: %lu bytes, verifying...", (unsigned long)len);

    verified_payload_t verified;
    uint32_t dev_hash = device_identity_get_hash();

    esp_err_t err = payload_verify(data, len, dev_hash, &verified);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Payload verification failed: %d", err);
        event_t evt = {.type = EVT_DEPLOY_FAILURE, .data = (uint32_t)err};
        event_bus_publish(&evt);
        return;
    }

    /* Stop scheduler before loading new program */
    scheduler_stop();

    /* Load program into VM */
    err = vm_load_program(verified.instructions, verified.header.num_instructions,
                          verified.constants, verified.header.num_constants,
                          verified.header.program_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Program load failed: %d", err);
        event_t evt = {.type = EVT_DEPLOY_FAILURE, .data = (uint32_t)err};
        event_bus_publish(&evt);
        return;
    }

    /* Save to flash for persistence */
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x80, SYS_PROGRAM_PARTITION);
    if (part) {
        esp_partition_erase_range(part, 0, part->size);
        /* Write length header + payload */
        esp_partition_write(part, 0, &len, sizeof(len));
        esp_partition_write(part, sizeof(len), data, len);
        ESP_LOGI(TAG, "Program saved to flash partition '%s'", SYS_PROGRAM_PARTITION);
    }

    /* Re-initialize scheduler with the new program's pipeline metadata */
    scheduler_init();

    /* Parse PIPELINE_START instructions to configure scheduler */
    const uint8_t *instr_ptr = verified.instructions;
    for (uint16_t i = 0; i < verified.header.num_instructions; i++) {
        const uint8_t *instr = instr_ptr + (i * SYS_INSTRUCTION_SIZE);
        if (instr[0] == OP_PIPELINE_START) {
            uint8_t pl_id = instr[1];
            uint8_t num_nodes = instr[2];
            uint16_t max_exec = (uint16_t)instr[4] | ((uint16_t)instr[5] << 8);

            /* Add a timer trigger (default 5s) — in production, parse from constant pool */
            trigger_config_t trig = {
                .id = pl_id,
                .type = TRIGGER_TIMER,
                .interval_ms = 5000,
            };
            scheduler_add_trigger(&trig);

            pipeline_config_t pl = {
                .id = pl_id,
                .trigger_id = pl_id,
                .priority = 5,
                .enabled = true,
                .instruction_offset = i,
                .instruction_count = num_nodes + 2, /* +START +END+HALT */
                .max_execution_ms = max_exec,
            };
            scheduler_add_pipeline(&pl);
        }
    }

    /* Start scheduler */
    scheduler_start();

    event_t evt = {.type = EVT_DEPLOY_SUCCESS};
    event_bus_publish(&evt);
    ESP_LOGI(TAG, "=== DEPLOYMENT SUCCESSFUL ===");
}

/**
 * Try to load a program from the flash partition (persisted from last deploy).
 */
static void try_load_from_flash(void) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x80, SYS_PROGRAM_PARTITION);
    if (!part) {
        ESP_LOGI(TAG, "No program partition found");
        return;
    }

    uint32_t stored_len = 0;
    esp_partition_read(part, 0, &stored_len, sizeof(stored_len));

    if (stored_len == 0 || stored_len == 0xFFFFFFFF || stored_len > SYS_PROGRAM_MAX_SIZE) {
        ESP_LOGI(TAG, "No valid program in flash");
        return;
    }

    static uint8_t flash_buf[SYS_PROGRAM_MAX_SIZE];
    esp_partition_read(part, sizeof(stored_len), flash_buf, stored_len);

    ESP_LOGI(TAG, "Found stored program: %lu bytes, loading...", (unsigned long)stored_len);
    on_payload_received(flash_buf, stored_len);
}

static device_config_t s_dev_cfg;

void app_main(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  PARAKRAM FIRMWARE v1.0.0");
    ESP_LOGI(TAG, "  Vidyuthlabs (c) 2025");
    ESP_LOGI(TAG, "  Zero-Code Hardware Platform");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "");

    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    device_config_load(&s_dev_cfg);
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Event bus */
    event_bus_init();

    /* 3. Fault handler */
    fault_handler_init();

    /* 4. State store */
    state_store_init();

    /* 5. VM */
    vm_init();

    /* 6. Driver registry */
    driver_registry_init();

    /* 7. I2C bus init */
    i2c_bus_init(0, 8, 9, 400000);   /* I2C_0: SDA=8, SCL=9 */

    /* 8. Device identity */
    device_identity_init();

    /* 9. Payload verifier */
    payload_verify_init(DEV_PUBKEY);

    /* 10. Scheduler */
    scheduler_init();

    /* 11. BLE (init before WiFi — BLE controller needs contiguous internal DRAM) */
    esp_err_t ble_ret = ble_gatt_init("Parakram");
    bool ble_ok = (ble_ret == ESP_OK);
    if (ble_ok) {
        ble_gatt_set_payload_callback(on_payload_received);
    }

    /* 12. WiFi */
    wifi_mgr_init("parakram", "");
    wifi_mgr_set_payload_callback(on_payload_received);
    wifi_mgr_start();

    /* 13. Start BLE advertising (only if init succeeded) */
    if (ble_ok) {
        ble_gatt_start_advertising();
    }

    /* 14. OTA manager — reads URL/token from NVS via device_config */
    ota_manager_init(s_dev_cfg.backend_url, s_dev_cfg.device_id, s_dev_cfg.auth_token);

    /* 15. Watchdog */
    watchdog_init();

    /* 16. Load saved program */
    try_load_from_flash();

    /* 17. System ready */
    event_t evt = {.type = EVT_SYSTEM_READY};
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "System ready. Waiting for deployment...");
    ESP_LOGI(TAG, "  WiFi: %s", wifi_mgr_is_connected() ? wifi_mgr_get_ip() : "connecting...");
    ESP_LOGI(TAG, "  BLE:  %s", ble_ok ? "advertising as 'Parakram'" : "disabled (WiFi-only)");

    /* Main task idles — all work done in scheduler + comms tasks */
    watchdog_register_task();
    while (1) {
        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
