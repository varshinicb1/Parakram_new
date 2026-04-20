/**
 * @file safety.h
 * @brief Safety subsystem — watchdog, rate limiter, fault handler.
 */
#ifndef SAFETY_H
#define SAFETY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog */
esp_err_t watchdog_init(void);
void      watchdog_feed(void);
void      watchdog_register_task(void);

/* Rate Limiter */
typedef struct {
    uint32_t min_interval_ms;
    uint32_t last_call_tick;
} rate_limiter_t;

void      rate_limiter_init(rate_limiter_t *rl, uint32_t min_interval_ms);
bool      rate_limiter_check(rate_limiter_t *rl);

/* Fault Handler */
typedef enum {
    FAULT_VM_STACK_OVERFLOW = 0, FAULT_VM_STACK_UNDERFLOW, FAULT_VM_INVALID_OPCODE,
    FAULT_VM_TIMEOUT, FAULT_DRIVER_INIT_FAIL, FAULT_DRIVER_READ_FAIL,
    FAULT_DRIVER_WRITE_FAIL, FAULT_PAYLOAD_SIGNATURE_INVALID, FAULT_PAYLOAD_DEVICE_MISMATCH,
    FAULT_COMMS_WIFI_FAIL, FAULT_COMMS_BLE_FAIL, FAULT_WATCHDOG_TIMEOUT,
    FAULT_MAX
} fault_type_t;

typedef struct {
    fault_type_t type;
    uint32_t     count;
    uint32_t     last_occurrence;
    uint32_t     data;
} fault_record_t;

esp_err_t fault_handler_init(void);
void      fault_raise(fault_type_t type, uint32_t data);
const fault_record_t *fault_get(fault_type_t type);
uint32_t  fault_total_count(void);
void      fault_clear_all(void);

#ifdef __cplusplus
}
#endif
#endif /* SAFETY_H */
