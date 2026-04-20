/**
 * @file comms.h
 * @brief Communication layer headers.
 */
#ifndef COMMS_H
#define COMMS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi Manager */
esp_err_t wifi_mgr_init(const char *ssid, const char *password);
esp_err_t wifi_mgr_start(void);
bool      wifi_mgr_is_connected(void);
const char *wifi_mgr_get_ip(void);

/* BLE GATT */
esp_err_t ble_gatt_init(const char *device_name);
esp_err_t ble_gatt_start_advertising(void);
esp_err_t ble_gatt_notify(uint16_t char_handle, const uint8_t *data, uint16_t len);
bool      ble_gatt_is_connected(void);

/* Callback set by app layer: called when BLE receives a deployment payload */
typedef void (*ble_payload_rx_cb_t)(const uint8_t *data, uint32_t len);
void      ble_gatt_set_payload_callback(ble_payload_rx_cb_t cb);

/* MQTT Client */
esp_err_t mqtt_client_init(const char *broker_uri);
esp_err_t mqtt_client_publish(const char *topic, const uint8_t *payload, uint16_t len);
bool      mqtt_client_is_connected(void);

/* Telemetry payload callback */
typedef void (*wifi_payload_rx_cb_t)(const uint8_t *data, uint32_t len);
void      wifi_mgr_set_payload_callback(wifi_payload_rx_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif /* COMMS_H */
