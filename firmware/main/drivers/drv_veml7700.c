/**
 * @file drv_veml7700.c
 * @brief VEML7700 I2C ambient light sensor — high dynamic range.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_VEML7700";

#define VEML7700_ADDR       0x10
#define VEML7700_ALS_CONF   0x00
#define VEML7700_ALS_DATA   0x04
#define VEML7700_WHITE_DATA 0x05

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
} veml7700_state_t;

static veml7700_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t veml7700_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    veml7700_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    /* ALS_CONF: gain 1/8, 100ms integration, no interrupt, power on */
    uint8_t conf[2] = {0x00, 0x18}; /* ALS_GAIN=11 (1/8), ALS_IT=01 (100ms) */
    i2c_bus_write(st->i2c_port, VEML7700_ADDR, VEML7700_ALS_CONF, conf, 2);
    vTaskDelay(pdMS_TO_TICKS(110));

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "VEML7700 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t veml7700_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    veml7700_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (field != CAP_LIGHT_LUX) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t raw[2];
    if (i2c_bus_read(st->i2c_port, VEML7700_ADDR, VEML7700_ALS_DATA, raw, 2) != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return ESP_FAIL;
    }

    uint16_t counts = (uint16_t)((raw[1] << 8) | raw[0]);
    /* Resolution at gain 1/8 and 100ms: 1.8432 lux/count */
    float lux = counts * 1.8432f;
    /* Lux correction for non-linearity above 1000 lux */
    if (lux > 1000.0f) {
        lux = 6.0135e-13f * lux * lux * lux * lux
            - 9.3924e-9f  * lux * lux * lux
            + 8.1488e-5f  * lux * lux
            + 1.0023f     * lux;
    }

    out->type = VAL_TYPE_FLOAT;
    out->f = lux;
    out->capability = CAP_LIGHT_LUX;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t veml7700_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        uint8_t sd[2] = {0x01, 0x00}; /* ALS_SD=1 = shutdown */
        i2c_bus_write(s_state[h.driver_index].i2c_port, VEML7700_ADDR, VEML7700_ALS_CONF, sd, 2);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t veml7700_meta = {
    .name = "drv_veml7700", .display_name = "VEML7700 Ambient Light",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_LIGHT_LUX}, .num_capabilities = 1,
    .max_latency_us = 110000, .min_interval_ms = 120,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(veml7700_state_t),
};

const driver_vtable_t drv_veml7700_vtable = {
    .init=veml7700_init, .read=veml7700_read, .write=NULL, .deinit=veml7700_deinit, .meta=&veml7700_meta
};
