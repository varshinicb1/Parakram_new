/**
 * @file drv_neo6m.c
 * @brief NEO-6M GPS UART driver — NMEA 0183 GGA parsing.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "DRV_NEO6M";

#define GPS_UART_BUF_SIZE   512
#define GPS_NMEA_MAX_LEN    100

typedef struct {
    uart_port_t uart_num;
    gpio_num_t  tx_pin;
    gpio_num_t  rx_pin;
    bool        initialized;
    bool        fix_valid;
    float       latitude;   /* degrees */
    float       longitude;  /* degrees */
    float       altitude_m;
    uint8_t     satellites;
    char        nmea_buf[GPS_NMEA_MAX_LEN];
    uint8_t     nmea_len;
} neo6m_state_t;

static neo6m_state_t s_state[2];
static uint8_t s_count = 0;

/* Parse NMEA DDMM.MMMM format to decimal degrees */
static float nmea_to_deg(const char *val, char hemi) {
    if (!val || val[0] == '\0') return 0.0f;
    float raw = (float)atof(val);
    int deg = (int)(raw / 100.0f);
    float min = raw - deg * 100.0f;
    float result = deg + min / 60.0f;
    if (hemi == 'S' || hemi == 'W') result = -result;
    return result;
}

static void parse_gga(neo6m_state_t *st, char *sentence) {
    /* $GPGGA,hhmmss.ss,lat,N,lon,E,fix,sats,hdop,alt,M,... */
    char *fields[15];
    int n = 0;
    char *tok = strtok(sentence, ",");
    while (tok && n < 15) { fields[n++] = tok; tok = strtok(NULL, ","); }
    if (n < 10) return;

    int fix = atoi(fields[6]);
    st->fix_valid = (fix >= 1);
    if (!st->fix_valid) return;

    st->latitude   = nmea_to_deg(fields[2], fields[3][0]);
    st->longitude  = nmea_to_deg(fields[4], fields[5][0]);
    st->satellites = (uint8_t)atoi(fields[7]);
    st->altitude_m = (float)atof(fields[9]);
}

static void neo6m_poll(neo6m_state_t *st) {
    uint8_t byte;
    while (uart_read_bytes(st->uart_num, &byte, 1, 0) > 0) {
        if (byte == '$') {
            st->nmea_len = 0;
        }
        if (st->nmea_len < GPS_NMEA_MAX_LEN - 1) {
            st->nmea_buf[st->nmea_len++] = (char)byte;
        }
        if (byte == '\n' && st->nmea_len > 6) {
            st->nmea_buf[st->nmea_len] = '\0';
            if (strncmp(st->nmea_buf, "$GPGGA", 6) == 0 ||
                strncmp(st->nmea_buf, "$GNGGA", 6) == 0) {
                char copy[GPS_NMEA_MAX_LEN];
                strncpy(copy, st->nmea_buf + 1, GPS_NMEA_MAX_LEN - 1);
                parse_gga(st, copy);
            }
            st->nmea_len = 0;
        }
    }
}

static esp_err_t neo6m_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    neo6m_state_t *st = &s_state[s_count];
    st->uart_num = (uart_port_t)cfg->bus_index;
    st->tx_pin = cfg->pins[0].gpio_num;
    st->rx_pin = cfg->pins[1].gpio_num;

    uart_config_t uart_cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_driver_install(st->uart_num, GPS_UART_BUF_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed");
        return ESP_FAIL;
    }
    uart_param_config(st->uart_num, &uart_cfg);
    uart_set_pin(st->uart_num, st->tx_pin, st->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    st->initialized = true;
    st->fix_valid = false;
    s_count++;
    ESP_LOGI(TAG, "NEO-6M init OK on UART%d", st->uart_num);
    return ESP_OK;
}

static esp_err_t neo6m_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    neo6m_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    neo6m_poll(st);

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (!st->fix_valid) {
        out->error = DRV_ERR_TIMEOUT;
        out->f = 0.0f;
        return ESP_OK; /* not a hard error — just no fix yet */
    }

    switch (field) {
        /* Reuse CAP_ALTITUDE for lat packed as float (simplification) */
        case CAP_ALTITUDE:  out->f = st->altitude_m; break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t neo6m_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        uart_driver_delete(s_state[h.driver_index].uart_num);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t neo6m_meta = {
    .name = "drv_neo6m", .display_name = "NEO-6M GPS",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_UART,
    .capabilities = {CAP_ALTITUDE}, .num_capabilities = 1,
    .max_latency_us = 1000000, .min_interval_ms = 1000,
    .failure_modes = {{DRV_ERR_TIMEOUT, "No GPS fix", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(neo6m_state_t),
};

const driver_vtable_t drv_neo6m_vtable = {
    .init=neo6m_init, .read=neo6m_read, .write=NULL, .deinit=neo6m_deinit, .meta=&neo6m_meta
};
