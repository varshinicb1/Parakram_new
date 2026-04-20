/**
 * @file driver_registry.c
 * @brief Static driver dispatch table.
 */

#include "driver_registry.h"
#include "system_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DRVREG";

static registered_driver_t s_drivers[SYS_MAX_DEVICES];
static uint8_t s_count = 0;

esp_err_t driver_registry_init(void) {
    memset(s_drivers, 0, sizeof(s_drivers));
    s_count = 0;
    ESP_LOGI(TAG, "Driver registry initialized (max=%d)", SYS_MAX_DEVICES);
    return ESP_OK;
}

esp_err_t driver_registry_register(uint8_t index, const driver_vtable_t *vtable, const driver_config_t *config) {
    if (index >= SYS_MAX_DEVICES) return ESP_ERR_INVALID_ARG;
    if (vtable == NULL) return ESP_ERR_INVALID_ARG;

    s_drivers[index].vtable = vtable;
    s_drivers[index].config = *config;
    s_drivers[index].handle.driver_index = index;
    s_drivers[index].handle.internal = NULL;
    s_drivers[index].initialized = false;

    if (index >= s_count) s_count = index + 1;
    ESP_LOGI(TAG, "Registered driver %d: %s", index,
             vtable->meta ? vtable->meta->name : "unknown");
    return ESP_OK;
}

registered_driver_t *driver_registry_get(uint8_t index) {
    if (index >= SYS_MAX_DEVICES) return NULL;
    if (s_drivers[index].vtable == NULL) return NULL;
    return &s_drivers[index];
}

uint8_t driver_registry_count(void) { return s_count; }

esp_err_t driver_registry_init_all(void) {
    uint8_t ok = 0, fail = 0;
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_drivers[i].vtable && s_drivers[i].vtable->init) {
            esp_err_t err = s_drivers[i].vtable->init(&s_drivers[i].config);
            if (err == ESP_OK) {
                s_drivers[i].initialized = true;
                ok++;
            } else {
                ESP_LOGE(TAG, "Driver %d init failed: %d", i, err);
                fail++;
            }
        }
    }
    ESP_LOGI(TAG, "Driver init complete: %d ok, %d failed", ok, fail);
    return (fail == 0) ? ESP_OK : ESP_FAIL;
}

void driver_registry_deinit_all(void) {
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_drivers[i].vtable && s_drivers[i].vtable->deinit && s_drivers[i].initialized) {
            s_drivers[i].vtable->deinit(s_drivers[i].handle);
            s_drivers[i].initialized = false;
        }
    }
}
