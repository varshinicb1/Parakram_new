/**
 * @file drv_ccs811.c
 * @brief CCS811 I2C CO2 + TVOC air quality sensor.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_CCS811";

#define CCS811_ADDR         0x5A
#define CCS811_STATUS       0x00
#define CCS811_MEAS_MODE    0x01
#define CCS811_ALG_RESULT   0x02
#define CCS811_HW_ID        0x20
#define CCS811_APP_START    0xF4
#define CCS811_RESET        0xFF

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    uint16_t eco2_ppm;
    uint16_t tvoc_ppb;
} ccs811_state_t;

static ccs811_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t ccs811_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    ccs811_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t hw_id = 0;
    if (i2c_bus_read(st->i2c_port, CCS811_ADDR, CCS811_HW_ID, &hw_id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed");
        return ESP_FAIL;
    }
    if (hw_id != 0x81) {
        ESP_LOGE(TAG, "Bad HW_ID: 0x%02X", hw_id);
        return ESP_FAIL;
    }

    /* Start app */
    uint8_t dummy = 0;
    i2c_bus_write(st->i2c_port, CCS811_ADDR, CCS811_APP_START, &dummy, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Set drive mode 1 (1 second intervals) */
    uint8_t mode = 0x10;
    i2c_bus_write(st->i2c_port, CCS811_ADDR, CCS811_MEAS_MODE, &mode, 1);

    st->eco2_ppm = 400;
    st->tvoc_ppb = 0;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "CCS811 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t ccs811_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    ccs811_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    /* Check data ready */
    uint8_t status = 0;
    i2c_bus_read(st->i2c_port, CCS811_ADDR, CCS811_STATUS, &status, 1);

    if (status & 0x08) { /* DATA_READY bit */
        uint8_t data[4];
        i2c_bus_read(st->i2c_port, CCS811_ADDR, CCS811_ALG_RESULT, data, 4);
        st->eco2_ppm = (uint16_t)((data[0] << 8) | data[1]);
        st->tvoc_ppb = (uint16_t)((data[2] << 8) | data[3]);
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

static esp_err_t ccs811_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) s_state[h.driver_index].initialized = false;
    return ESP_OK;
}

static const driver_meta_t ccs811_meta = {
    .name = "drv_ccs811", .display_name = "CCS811 Air Quality",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_CO2_PPM, CAP_TVOC_PPB}, .num_capabilities = 2,
    .max_latency_us = 10000, .min_interval_ms = 1000,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=400}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(ccs811_state_t),
};

const driver_vtable_t drv_ccs811_vtable = {
    .init=ccs811_init, .read=ccs811_read, .write=NULL, .deinit=ccs811_deinit, .meta=&ccs811_meta
};
