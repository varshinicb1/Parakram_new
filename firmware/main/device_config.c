#include "device_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG      = "DEV_CFG";
static const char *NVS_NS   = "parakram";

#define KEY_BACKEND_URL  "backend_url"
#define KEY_DEVICE_ID    "device_id"
#define KEY_AUTH_TOKEN   "auth_token"
#define KEY_WIFI_SSID    "wifi_ssid"
#define KEY_WIFI_PASS    "wifi_pass"
#define KEY_PROVISIONED  "provisioned"

static void nvs_get_str_or_default(nvs_handle_t h, const char *key,
                                   char *dst, size_t max, const char *def) {
    size_t len = max;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) {
        strncpy(dst, def, max - 1);
        dst[max - 1] = '\0';
    }
}

void device_config_load(device_config_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // NVS namespace doesn't exist yet — fill defaults
        strncpy(cfg->backend_url, "http://192.168.1.1:8400", DEVICE_CONFIG_URL_MAX - 1);
        strncpy(cfg->device_id,   "parakram-device-001",     DEVICE_CONFIG_ID_MAX - 1);
        cfg->auth_token[0]  = '\0';
        cfg->wifi_ssid[0]   = '\0';
        cfg->wifi_pass[0]   = '\0';
        cfg->provisioned    = false;
        ESP_LOGI(TAG, "No config in NVS, using defaults");
        return;
    }

    nvs_get_str_or_default(h, KEY_BACKEND_URL, cfg->backend_url,
                           DEVICE_CONFIG_URL_MAX, "http://192.168.1.1:8400");
    nvs_get_str_or_default(h, KEY_DEVICE_ID, cfg->device_id,
                           DEVICE_CONFIG_ID_MAX, "parakram-device-001");
    nvs_get_str_or_default(h, KEY_AUTH_TOKEN, cfg->auth_token,
                           DEVICE_CONFIG_TOKEN_MAX, "");
    nvs_get_str_or_default(h, KEY_WIFI_SSID, cfg->wifi_ssid,
                           DEVICE_CONFIG_WIFI_MAX, "");
    nvs_get_str_or_default(h, KEY_WIFI_PASS, cfg->wifi_pass,
                           DEVICE_CONFIG_WIFI_MAX, "");

    uint8_t prov = 0;
    nvs_get_u8(h, KEY_PROVISIONED, &prov);
    cfg->provisioned = (prov != 0);

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: url=%s id=%s provisioned=%d",
             cfg->backend_url, cfg->device_id, cfg->provisioned);
}

bool device_config_save(const device_config_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %d", err);
        return false;
    }

    nvs_set_str(h, KEY_BACKEND_URL, cfg->backend_url);
    nvs_set_str(h, KEY_DEVICE_ID,   cfg->device_id);
    nvs_set_str(h, KEY_AUTH_TOKEN,  cfg->auth_token);
    nvs_set_str(h, KEY_WIFI_SSID,   cfg->wifi_ssid);
    nvs_set_str(h, KEY_WIFI_PASS,   cfg->wifi_pass);
    nvs_set_u8(h,  KEY_PROVISIONED, cfg->provisioned ? 1 : 0);

    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %d", err);
        return false;
    }
    ESP_LOGI(TAG, "Config saved to NVS");
    return true;
}

void device_config_reset(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Device config reset to factory defaults");
}
