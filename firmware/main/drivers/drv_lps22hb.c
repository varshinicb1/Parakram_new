/**
 * @file drv_lps22hb.c
 * @brief LPS22HB I2C barometric pressure + temperature sensor (ST).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "DRV_LPS22HB";

#define LPS22HB_ADDR        0x5C
#define LPS22HB_WHO_AM_I    0x0F
#define LPS22HB_CTRL_REG1   0x10
#define LPS22HB_CTRL_REG2   0x11
#define LPS22HB_STATUS      0x27
#define LPS22HB_PRESS_OUT_XL 0x28
#define LPS22HB_TEMP_OUT_L  0x2B

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
} lps22hb_state_t;

static lps22hb_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t lps22hb_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    lps22hb_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t who = 0;
    if (i2c_bus_read(st->i2c_port, LPS22HB_ADDR, LPS22HB_WHO_AM_I, &who, 1) != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed");
        return ESP_FAIL;
    }
    if (who != 0xB1) {
        ESP_LOGE(TAG, "Bad WHO_AM_I: 0x%02X", who);
        return ESP_FAIL;
    }

    /* ODR 10 Hz, LPF enabled */
    uint8_t ctrl1 = 0x20;
    i2c_bus_write(st->i2c_port, LPS22HB_ADDR, LPS22HB_CTRL_REG1, &ctrl1, 1);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "LPS22HB init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t lps22hb_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    lps22hb_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (field == CAP_PRESSURE || field == CAP_ALTITUDE) {
        uint8_t raw[3];
        i2c_bus_read(st->i2c_port, LPS22HB_ADDR, LPS22HB_PRESS_OUT_XL, raw, 3);
        int32_t raw_p = (int32_t)((raw[2] << 16) | (raw[1] << 8) | raw[0]);
        float hpa = (float)raw_p / 4096.0f;
        if (field == CAP_PRESSURE) out->f = hpa;
        else out->f = 44330.0f * (1.0f - powf(hpa / 1013.25f, 0.1903f));
    } else if (field == CAP_TEMPERATURE) {
        uint8_t raw[2];
        i2c_bus_read(st->i2c_port, LPS22HB_ADDR, LPS22HB_TEMP_OUT_L, raw, 2);
        int16_t raw_t = (int16_t)((raw[1] << 8) | raw[0]);
        out->f = (float)raw_t / 100.0f;
    } else {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t lps22hb_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        uint8_t ctrl1 = 0x00; /* power down */
        i2c_bus_write(s_state[h.driver_index].i2c_port, LPS22HB_ADDR, LPS22HB_CTRL_REG1, &ctrl1, 1);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t lps22hb_meta = {
    .name = "drv_lps22hb", .display_name = "LPS22HB Pressure + Temp",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_PRESSURE, CAP_TEMPERATURE, CAP_ALTITUDE}, .num_capabilities = 3,
    .max_latency_us = 2000, .min_interval_ms = 100,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(lps22hb_state_t),
};

const driver_vtable_t drv_lps22hb_vtable = {
    .init=lps22hb_init, .read=lps22hb_read, .write=NULL, .deinit=lps22hb_deinit, .meta=&lps22hb_meta
};
