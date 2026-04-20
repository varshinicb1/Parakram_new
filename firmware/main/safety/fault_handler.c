/**
 * @file fault_handler.c
 * @brief Fault tracking and recovery.
 */

#include "safety.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "FAULT";

static fault_record_t s_faults[FAULT_MAX];
static uint32_t s_total = 0;

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

esp_err_t fault_handler_init(void) {
    memset(s_faults, 0, sizeof(s_faults));
    s_total = 0;
    for (int i = 0; i < FAULT_MAX; i++) {
        s_faults[i].type = (fault_type_t)i;
    }
    ESP_LOGI(TAG, "Fault handler initialized (%d fault types)", FAULT_MAX);
    return ESP_OK;
}

void fault_raise(fault_type_t type, uint32_t data) {
    if (type >= FAULT_MAX) return;

    fault_record_t *rec = &s_faults[type];
    rec->count++;
    rec->last_occurrence = now_ms();
    rec->data = data;
    s_total++;

    static const char *fault_names[] = {
        "VM_STACK_OVERFLOW", "VM_STACK_UNDERFLOW", "VM_INVALID_OPCODE",
        "VM_TIMEOUT", "DRIVER_INIT_FAIL", "DRIVER_READ_FAIL",
        "DRIVER_WRITE_FAIL", "PAYLOAD_SIG_INVALID", "PAYLOAD_DEV_MISMATCH",
        "COMMS_WIFI_FAIL", "COMMS_BLE_FAIL", "WATCHDOG_TIMEOUT",
    };
    const char *name = (type < sizeof(fault_names)/sizeof(fault_names[0])) ?
                       fault_names[type] : "UNKNOWN";
    ESP_LOGW(TAG, "FAULT [%s] count=%lu data=0x%08lX",
             name, (unsigned long)rec->count, (unsigned long)data);

    /* Publish fault event */
    event_t evt = {.type = EVT_SYSTEM_ERROR, .data = (uint32_t)type};
    event_bus_publish(&evt);
}

const fault_record_t *fault_get(fault_type_t type) {
    if (type >= FAULT_MAX) return NULL;
    return &s_faults[type];
}

uint32_t fault_total_count(void) { return s_total; }

void fault_clear_all(void) {
    memset(s_faults, 0, sizeof(s_faults));
    s_total = 0;
    for (int i = 0; i < FAULT_MAX; i++) {
        s_faults[i].type = (fault_type_t)i;
    }
}
