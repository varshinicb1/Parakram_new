/**
 * @file event_bus.h
 * @brief Lightweight event bus for inter-component communication.
 */
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVT_PROGRAM_LOADED = 0, EVT_PROGRAM_UNLOADED, EVT_PIPELINE_COMPLETE,
    EVT_PIPELINE_ERROR, EVT_DRIVER_ERROR, EVT_WIFI_CONNECTED, EVT_WIFI_DISCONNECTED,
    EVT_BLE_CONNECTED, EVT_BLE_DISCONNECTED, EVT_PAYLOAD_RECEIVED,
    EVT_DEPLOY_SUCCESS, EVT_DEPLOY_FAILURE, EVT_WATCHDOG_FAULT,
    EVT_SYSTEM_READY, EVT_SYSTEM_ERROR,
    EVT_MAX
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t     data;
    void        *ptr;
} event_t;

typedef void (*event_handler_t)(const event_t *event, void *user_data);

esp_err_t event_bus_init(void);
esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void *user_data);
esp_err_t event_bus_publish(const event_t *event);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_BUS_H */
