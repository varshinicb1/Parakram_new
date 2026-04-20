/**
 * @file driver_registry.h
 * @brief Static driver dispatch table.
 */
#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include "driver_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const driver_vtable_t  *vtable;
    driver_config_t         config;
    driver_handle_t         handle;
    bool                    initialized;
} registered_driver_t;

esp_err_t           driver_registry_init(void);
esp_err_t           driver_registry_register(uint8_t index, const driver_vtable_t *vtable, const driver_config_t *config);
registered_driver_t *driver_registry_get(uint8_t index);
uint8_t             driver_registry_count(void);
esp_err_t           driver_registry_init_all(void);
void                driver_registry_deinit_all(void);

#ifdef __cplusplus
}
#endif
#endif /* DRIVER_REGISTRY_H */
