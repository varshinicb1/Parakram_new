/**
 * @file drv_ft6236.c
 * @brief FT6236 I2C capacitive touch controller (5-point multitouch).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DRV_FT6236";

#define FT6236_ADDR         0x38
#define FT6236_REG_TD_STATUS 0x02
#define FT6236_REG_P1_XH    0x03
#define FT6236_REG_CHIP_ID  0xA3

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    int16_t x, y;
    bool    touched;
    uint8_t touch_count;
} ft6236_state_t;

static ft6236_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t ft6236_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    ft6236_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t chip_id = 0;
    i2c_bus_read(st->i2c_port, FT6236_ADDR, FT6236_REG_CHIP_ID, &chip_id, 1);
    ESP_LOGI(TAG, "FT6236 chip ID: 0x%02X on I2C%d", chip_id, st->i2c_port);

    /* Set threshold */
    uint8_t thresh = 22;
    i2c_bus_write(st->i2c_port, FT6236_ADDR, 0x80, &thresh, 1);

    st->initialized = true;
    s_count++;
    return ESP_OK;
}

static esp_err_t ft6236_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    ft6236_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;
    if (field != CAP_PROXIMITY) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t data[5];
    if (i2c_bus_read(st->i2c_port, FT6236_ADDR, FT6236_REG_TD_STATUS, data, 5) == ESP_OK) {
        st->touch_count = data[0] & 0x0F;
        st->touched = (st->touch_count > 0);
        if (st->touched) {
            st->x = (int16_t)(((data[1] & 0x0F) << 8) | data[2]);
            st->y = (int16_t)(((data[3] & 0x0F) << 8) | data[4]);
        }
    }

    out->type = VAL_TYPE_BOOL;
    out->b = st->touched;
    out->capability = CAP_PROXIMITY;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t ft6236_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) s_state[h.driver_index].initialized = false;
    return ESP_OK;
}

static const driver_meta_t ft6236_meta = {
    .name = "drv_ft6236", .display_name = "FT6236 Capacitive Touch",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_PROXIMITY}, .num_capabilities = 1,
    .max_latency_us = 2000, .min_interval_ms = 16,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(ft6236_state_t),
};

const driver_vtable_t drv_ft6236_vtable = {
    .init=ft6236_init, .read=ft6236_read, .write=NULL, .deinit=ft6236_deinit, .meta=&ft6236_meta
};
