/**
 * @file state_store.c
 * @brief Fixed-size state variable store.
 */

#include "state_store.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "STATE";

static vm_value_t s_variables[SYS_MAX_STATE_VARIABLES];
static uint8_t    s_count = 0;

esp_err_t state_store_init(void) {
    memset(s_variables, 0, sizeof(s_variables));
    s_count = 0;
    ESP_LOGI(TAG, "State store initialized (max=%d)", SYS_MAX_STATE_VARIABLES);
    return ESP_OK;
}

esp_err_t state_store_set(uint8_t index, vm_value_t value) {
    if (index >= SYS_MAX_STATE_VARIABLES) return ESP_ERR_INVALID_ARG;
    s_variables[index] = value;
    if (index >= s_count) s_count = index + 1;
    return ESP_OK;
}

esp_err_t state_store_get(uint8_t index, vm_value_t *out) {
    if (index >= SYS_MAX_STATE_VARIABLES || out == NULL) return ESP_ERR_INVALID_ARG;
    *out = s_variables[index];
    return ESP_OK;
}

esp_err_t state_store_increment(uint8_t index) {
    if (index >= SYS_MAX_STATE_VARIABLES) return ESP_ERR_INVALID_ARG;
    vm_value_t *v = &s_variables[index];
    switch (v->type) {
        case VM_TYPE_INT:   v->i++; break;
        case VM_TYPE_FLOAT: v->f += 1.0f; break;
        case VM_TYPE_BOOL:  v->b = !v->b; break;
        default: return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void state_store_reset(void) {
    memset(s_variables, 0, sizeof(s_variables));
    s_count = 0;
}

uint8_t state_store_count(void) { return s_count; }
