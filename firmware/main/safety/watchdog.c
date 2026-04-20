/**
 * @file watchdog.c
 * @brief Hardware watchdog timer management.
 */

#include "safety.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "system_config.h"

static const char *TAG = "WDT";

esp_err_t watchdog_init(void) {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = SYS_WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask = 0, /* Don't watch idle tasks */
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WDT reconfigure returned %d (may already be configured)", err);
    }
    ESP_LOGI(TAG, "Watchdog initialized: %ds timeout", SYS_WATCHDOG_TIMEOUT_S);
    return ESP_OK;
}

void watchdog_feed(void) {
    esp_task_wdt_reset();
}

void watchdog_register_task(void) {
    esp_task_wdt_add(NULL);
}
