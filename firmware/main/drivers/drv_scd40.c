/**
 * @file drv_scd40.c
 * @brief SCD40 I2C CO2 + temperature + humidity sensor (Sensirion).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_SCD40";

#define SCD40_ADDR              0x62
/* Commands (big-endian 16-bit) */
#define SCD40_START_PERIODIC    0x21B1
#define SCD40_READ_MEASUREMENT  0xEC05
#define SCD40_STOP_PERIODIC     0x3F86
#define SCD40_GET_SERIAL        0x3682

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

static uint8_t crc8_scd40(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    float   co2_ppm;
    float   temperature;
    float   humidity;
} scd40_state_t;

static scd40_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t scd40_send_cmd(uint8_t port, uint16_t cmd) {
    uint8_t c[1] = {(uint8_t)(cmd & 0xFF)};
    return i2c_bus_write(port, SCD40_ADDR, (uint8_t)(cmd >> 8), c, 1);
}

static esp_err_t scd40_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    scd40_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    /* Stop any ongoing measurement first */
    scd40_send_cmd(st->i2c_port, SCD40_STOP_PERIODIC);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Start periodic measurement */
    scd40_send_cmd(st->i2c_port, SCD40_START_PERIODIC);
    vTaskDelay(pdMS_TO_TICKS(100));

    st->co2_ppm = 400.0f;
    st->temperature = 25.0f;
    st->humidity = 50.0f;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "SCD40 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t scd40_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    scd40_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    /* Read measurement: 9 bytes = co2(2+crc), temp(2+crc), hum(2+crc) */
    scd40_send_cmd(st->i2c_port, SCD40_READ_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t data[9];
    if (i2c_bus_read(st->i2c_port, SCD40_ADDR, 0x00, data, 9) == ESP_OK) {
        if (crc8_scd40(&data[0], 2) == data[2]) {
            uint16_t raw_co2 = (uint16_t)((data[0] << 8) | data[1]);
            st->co2_ppm = (float)raw_co2;
        }
        if (crc8_scd40(&data[3], 2) == data[5]) {
            uint16_t raw_t = (uint16_t)((data[3] << 8) | data[4]);
            st->temperature = -45.0f + 175.0f * raw_t / 65535.0f;
        }
        if (crc8_scd40(&data[6], 2) == data[8]) {
            uint16_t raw_h = (uint16_t)((data[6] << 8) | data[7]);
            st->humidity = 100.0f * raw_h / 65535.0f;
        }
    }

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (field) {
        case CAP_CO2_PPM:    out->f = st->co2_ppm; break;
        case CAP_TEMPERATURE: out->f = st->temperature; break;
        case CAP_HUMIDITY:   out->f = st->humidity; break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t scd40_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        scd40_send_cmd(s_state[h.driver_index].i2c_port, SCD40_STOP_PERIODIC);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t scd40_meta = {
    .name = "drv_scd40", .display_name = "SCD40 CO2 + Temp + Humidity",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_CO2_PPM, CAP_TEMPERATURE, CAP_HUMIDITY}, .num_capabilities = 3,
    .max_latency_us = 5000, .min_interval_ms = 5000,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=400}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(scd40_state_t),
};

const driver_vtable_t drv_scd40_vtable = {
    .init=scd40_init, .read=scd40_read, .write=NULL, .deinit=scd40_deinit, .meta=&scd40_meta
};
