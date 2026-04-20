#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_CONFIG_URL_MAX   128
#define DEVICE_CONFIG_ID_MAX    64
#define DEVICE_CONFIG_TOKEN_MAX 512
#define DEVICE_CONFIG_WIFI_MAX  64

typedef struct {
    char backend_url[DEVICE_CONFIG_URL_MAX];   // e.g. "http://192.168.1.10:8400"
    char device_id[DEVICE_CONFIG_ID_MAX];      // UUID assigned by backend on pairing
    char auth_token[DEVICE_CONFIG_TOKEN_MAX];  // JWT bearer token
    char wifi_ssid[DEVICE_CONFIG_WIFI_MAX];
    char wifi_pass[DEVICE_CONFIG_WIFI_MAX];
    bool provisioned;                          // true once paired with backend
} device_config_t;

// Load config from NVS. Fills defaults if not yet provisioned.
void device_config_load(device_config_t *cfg);

// Persist config to NVS. Call after any field change.
bool device_config_save(const device_config_t *cfg);

// Reset to factory defaults (clears NVS namespace).
void device_config_reset(void);
