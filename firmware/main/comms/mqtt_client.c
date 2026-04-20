/**
 * @file mqtt_client.c
 * @brief MQTT client wrapper for telemetry publishing.
 */

#include "comms.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_client_init(const char *broker_uri) {
    if (broker_uri == NULL || strlen(broker_uri) == 0) {
        ESP_LOGW(TAG, "No MQTT broker configured");
        return ESP_OK;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_uri,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) return ESP_FAIL;

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT client started: %s", broker_uri);
    return ESP_OK;
}

esp_err_t mqtt_client_publish(const char *topic, const uint8_t *payload, uint16_t len) {
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_client, topic, (const char *)payload, len, 0, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

bool mqtt_client_is_connected(void) { return s_connected; }
