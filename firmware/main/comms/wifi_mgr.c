/**
 * @file wifi_mgr.c
 * @brief WiFi station manager with payload reception via TCP.
 */

#include "comms.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WIFI";

static bool s_connected = false;
static char s_ip[16] = {0};
static wifi_payload_rx_cb_t s_payload_cb = NULL;
static TaskHandle_t s_tcp_task = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            event_t evt = {.type = EVT_WIFI_DISCONNECTED};
            event_bus_publish(&evt);
            esp_wifi_connect(); /* Auto-reconnect */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        ESP_LOGI(TAG, "Connected, IP: %s", s_ip);
        event_t evt = {.type = EVT_WIFI_CONNECTED};
        event_bus_publish(&evt);
    }
}

/* TCP server task for receiving deployment payloads */
static void tcp_server_task(void *arg) {
    static uint8_t rx_buffer[SYS_PROGRAM_MAX_SIZE];
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(SYS_WIFI_CONFIG_PORT),
    };

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) { ESP_LOGE(TAG, "Socket failed"); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);

    ESP_LOGI(TAG, "TCP server listening on port %d", SYS_WIFI_CONFIG_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { vTaskDelay(100); continue; }

        ESP_LOGI(TAG, "Client connected");
        uint32_t total_rx = 0;
        int rx_len;
        while ((rx_len = recv(client_fd, rx_buffer + total_rx,
                              sizeof(rx_buffer) - total_rx, 0)) > 0) {
            total_rx += rx_len;
            if (total_rx >= sizeof(rx_buffer)) break;
        }

        close(client_fd);

        if (total_rx > 0 && s_payload_cb) {
            ESP_LOGI(TAG, "Received payload: %lu bytes", (unsigned long)total_rx);
            s_payload_cb(rx_buffer, total_rx);
        }
    }
}

esp_err_t wifi_mgr_init(const char *ssid, const char *password) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    ESP_LOGI(TAG, "WiFi initialized, SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void) {
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    esp_wifi_connect();

    /* Start TCP server */
    xTaskCreatePinnedToCore(tcp_server_task, "tcp_srv", STACK_SIZE_WIFI,
                            NULL, TASK_PRIORITY_COMMS, &s_tcp_task, 0);
    return ESP_OK;
}

bool wifi_mgr_is_connected(void) { return s_connected; }
const char *wifi_mgr_get_ip(void) { return s_ip; }
void wifi_mgr_set_payload_callback(wifi_payload_rx_cb_t cb) { s_payload_cb = cb; }
