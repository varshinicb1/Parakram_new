/**
 * @file state_store.h
 * @brief State variable store — fixed-size typed variables for the VM.
 */
#ifndef STATE_STORE_H
#define STATE_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "system_config.h"
#include "vm.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t   state_store_init(void);
esp_err_t   state_store_set(uint8_t index, vm_value_t value);
esp_err_t   state_store_get(uint8_t index, vm_value_t *out);
esp_err_t   state_store_increment(uint8_t index);
void        state_store_reset(void);
uint8_t     state_store_count(void);

#ifdef __cplusplus
}
#endif
#endif /* STATE_STORE_H */
