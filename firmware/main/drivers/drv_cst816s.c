/**
 * @file drv_cst816s.c
 * @brief CST816S I2C capacitive touch controller (single-touch).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "DRV_CST816S";

#define CST816S_ADDR            0x15
#define CST816S_REG_GESTURE     0x01
#define CST816S_REG_FINGER_NUM  0x02
#define CST816S_REG_XPOS_H      0x03
#define CST816S_REG_XPOS_L      0x04
#define CST816S_REG_YPOS_H      0x05
#define CST816S_REG_YPOS_L      0x06
#define CST816S_REG_CHIP_ID     0xA7

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    int16_t x, y;
    bool    touched;
    uint8_t gesture;
} cst816s_state_t;

static cst816s_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t cst816s_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    cst816s_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t chip_id = 0;
    if (i2c_bus_read(st->i2c_port, CST816S_ADDR, CST816S_REG_CHIP_ID, &chip_id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "CST816S chip ID: 0x%02X", chip_id);

    /* Enable gestures and auto sleep */
    uint8_t motion = 0x71;
    i2c_bus_write(st->i2c_port, CST816S_ADDR, 0xEC, &motion, 1);

    st->initialized = true;
    s_count++;
    return ESP_OK;
}

static esp_err_t cst816s_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    cst816s_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (field != CAP_PROXIMITY) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t data[6];
    if (i2c_bus_read(st->i2c_port, CST816S_ADDR, CST816S_REG_GESTURE, data, 6) == ESP_OK) {
        st->gesture = data[0];
        st->touched = (data[1] > 0);
        st->x = (int16_t)(((data[2] & 0x0F) << 8) | data[3]);
        st->y = (int16_t)(((data[4] & 0x0F) << 8) | data[5]);
    }

    out->type = VAL_TYPE_BOOL;
    out->b = st->touched;
    out->capability = CAP_PROXIMITY;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t cst816s_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) s_state[h.driver_index].initialized = false;
    return ESP_OK;
}

static const driver_meta_t cst816s_meta = {
    .name = "drv_cst816s", .display_name = "CST816S Capacitive Touch",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_PROXIMITY}, .num_capabilities = 1,
    .max_latency_us = 2000, .min_interval_ms = 16,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(cst816s_state_t),
};

const driver_vtable_t drv_cst816s_vtable = {
    .init=cst816s_init, .read=cst816s_read, .write=NULL, .deinit=cst816s_deinit, .meta=&cst816s_meta
};
