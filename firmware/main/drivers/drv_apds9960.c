/**
 * @file drv_apds9960.c
 * @brief APDS-9960 I2C proximity, color, and gesture sensor.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DRV_APDS9960";

#define APDS9960_ADDR       0x39
#define APDS9960_ID_REG     0x92
#define APDS9960_ID_VAL     0xAB
#define APDS9960_ENABLE     0x80
#define APDS9960_ATIME      0x81
#define APDS9960_CONTROL    0x8F
#define APDS9960_STATUS     0x93
#define APDS9960_CDATAL     0x94  /* clear/color data */
#define APDS9960_RDATAL     0x96
#define APDS9960_GDATAL     0x98
#define APDS9960_BDATAL     0x9A
#define APDS9960_PDATA      0x9C  /* proximity */

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    float   last_r, last_g, last_b, last_proximity;
} apds9960_state_t;

static apds9960_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t apds9960_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    apds9960_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t id = 0;
    if (i2c_bus_read(st->i2c_port, APDS9960_ADDR, APDS9960_ID_REG, &id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed");
        return ESP_FAIL;
    }
    if (id != APDS9960_ID_VAL && id != 0xA8) {
        ESP_LOGE(TAG, "Bad chip ID: 0x%02X", id);
        return ESP_FAIL;
    }

    /* Enable: power on, ALS, proximity */
    uint8_t en = 0x0F;
    i2c_bus_write(st->i2c_port, APDS9960_ADDR, APDS9960_ENABLE, &en, 1);
    /* ALS integration time 100ms */
    uint8_t atime = 0xDB;
    i2c_bus_write(st->i2c_port, APDS9960_ADDR, APDS9960_ATIME, &atime, 1);
    /* Gain x4 */
    uint8_t ctrl = 0x02;
    i2c_bus_write(st->i2c_port, APDS9960_ADDR, APDS9960_CONTROL, &ctrl, 1);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "APDS9960 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t apds9960_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    apds9960_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (field == CAP_PROXIMITY) {
        uint8_t p = 0;
        i2c_bus_read(st->i2c_port, APDS9960_ADDR, APDS9960_PDATA, &p, 1);
        out->f = (float)p;
    } else if (field == CAP_LIGHT_LUX) {
        uint8_t raw[8];
        i2c_bus_read(st->i2c_port, APDS9960_ADDR, APDS9960_CDATAL, raw, 8);
        uint16_t r = (uint16_t)(raw[2] | (raw[3] << 8));
        uint16_t g = (uint16_t)(raw[4] | (raw[5] << 8));
        uint16_t b = (uint16_t)(raw[6] | (raw[7] << 8));
        /* Simple lux estimate from RGB */
        out->f = (float)(r * 0.3f + g * 0.59f + b * 0.11f) / 100.0f;
    } else {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t apds9960_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        uint8_t en = 0x00;
        i2c_bus_write(s_state[h.driver_index].i2c_port, APDS9960_ADDR, APDS9960_ENABLE, &en, 1);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t apds9960_meta = {
    .name = "drv_apds9960", .display_name = "APDS-9960 Proximity+Color",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_PROXIMITY, CAP_LIGHT_LUX}, .num_capabilities = 2,
    .max_latency_us = 5000, .min_interval_ms = 100,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(apds9960_state_t),
};

const driver_vtable_t drv_apds9960_vtable = {
    .init=apds9960_init, .read=apds9960_read, .write=NULL, .deinit=apds9960_deinit, .meta=&apds9960_meta
};
