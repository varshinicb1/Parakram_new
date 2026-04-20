/**
 * @file event_bus.c
 * @brief Lightweight publish-subscribe event bus.
 */

#include "event_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "EVTBUS";

#define MAX_SUBSCRIBERS_PER_EVENT  4

typedef struct {
    event_handler_t handler;
    void           *user_data;
} subscriber_t;

static subscriber_t s_subscribers[EVT_MAX][MAX_SUBSCRIBERS_PER_EVENT];
static uint8_t      s_sub_counts[EVT_MAX];

esp_err_t event_bus_init(void) {
    memset(s_subscribers, 0, sizeof(s_subscribers));
    memset(s_sub_counts, 0, sizeof(s_sub_counts));
    ESP_LOGI(TAG, "Event bus initialized (%d event types)", EVT_MAX);
    return ESP_OK;
}

esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void *user_data) {
    if (type >= EVT_MAX || handler == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t idx = s_sub_counts[type];
    if (idx >= MAX_SUBSCRIBERS_PER_EVENT) return ESP_ERR_NO_MEM;
    s_subscribers[type][idx].handler = handler;
    s_subscribers[type][idx].user_data = user_data;
    s_sub_counts[type]++;
    return ESP_OK;
}

esp_err_t event_bus_publish(const event_t *event) {
    if (event == NULL || event->type >= EVT_MAX) return ESP_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < s_sub_counts[event->type]; i++) {
        subscriber_t *sub = &s_subscribers[event->type][i];
        if (sub->handler) {
            sub->handler(event, sub->user_data);
        }
    }
    return ESP_OK;
}
