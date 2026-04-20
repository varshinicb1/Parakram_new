/**
 * @file drv_sgp30.c
 * @brief SGP30 I2C TVOC and eCO2 sensor.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_SGP30";

#define SGP30_ADDR          0x58

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* SGP30 uses command pairs, not simple reg writes */
static esp_err_t sgp30_cmd(uint8_t port, uint8_t cmd_msb, uint8_t cmd_lsb,
                             uint8_t *out, uint8_t out_len, uint32_t wait_ms) {
    uint8_t cmd[2] = {cmd_msb, cmd_lsb};
    /* Write command bytes (no register address — address is embedded in cmd) */
    esp_err_t r = i2c_bus_write(port, SGP30_ADDR, cmd[0], &cmd[1], 1);
    if (r != ESP_OK) return r;
    if (wait_ms) vTaskDelay(pdMS_TO_TICKS(wait_ms));
    if (out && out_len) {
        r = i2c_bus_read(port, SGP30_ADDR, 0x00, out, out_len);
    }
    return r;
}

typedef struct {
    uint8_t  i2c_port;
    bool     initialized;
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
} sgp30_state_t;

static sgp30_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t sgp30_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    sgp30_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    /* IAQ init command */
    sgp30_cmd(st->i2c_port, 0x20, 0x03, NULL, 0, 10);

    st->tvoc_ppb = 0;
    st->eco2_ppm = 400;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "SGP30 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t sgp30_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    sgp30_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    /* Measure IAQ command 0x2008, returns 6 bytes: co2(2)+crc, tvoc(2)+crc */
    uint8_t data[6];
    if (sgp30_cmd(st->i2c_port, 0x20, 0x08, data, 6, 12) == ESP_OK) {
        st->eco2_ppm = (uint16_t)((data[0] << 8) | data[1]);
        st->tvoc_ppb = (uint16_t)((data[3] << 8) | data[4]);
    }

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (field) {
        case CAP_CO2_PPM:  out->f = (float)st->eco2_ppm; break;
        case CAP_TVOC_PPB: out->f = (float)st->tvoc_ppb; break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t sgp30_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) s_state[h.driver_index].initialized = false;
    return ESP_OK;
}

static const driver_meta_t sgp30_meta = {
    .name = "drv_sgp30", .display_name = "SGP30 TVOC/eCO2",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_CO2_PPM, CAP_TVOC_PPB}, .num_capabilities = 2,
    .max_latency_us = 15000, .min_interval_ms = 1000,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=400}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(sgp30_state_t),
};

const driver_vtable_t drv_sgp30_vtable = {
    .init=sgp30_init, .read=sgp30_read, .write=NULL, .deinit=sgp30_deinit, .meta=&sgp30_meta
};
