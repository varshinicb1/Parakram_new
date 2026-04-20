/**
 * @file drv_ens160.c
 * @brief ENS160 I2C digital multi-gas sensor — eCO2 and TVOC.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_ENS160";

#define ENS160_ADDR         0x53
#define ENS160_REG_PART_ID  0x00
#define ENS160_REG_OPMODE   0x10
#define ENS160_REG_STATUS   0x20
#define ENS160_REG_DATA_AQI 0x21
#define ENS160_REG_DATA_ECO2 0x24
#define ENS160_REG_DATA_TVOC 0x22

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t  i2c_port;
    bool     initialized;
    float    eco2_ppm;
    float    tvoc_ppb;
} ens160_state_t;

static ens160_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t ens160_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    ens160_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t part_id[2];
    if (i2c_bus_read(st->i2c_port, ENS160_ADDR, ENS160_REG_PART_ID, part_id, 2) != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed");
        return ESP_FAIL;
    }
    uint16_t pid = (uint16_t)((part_id[1] << 8) | part_id[0]);
    if (pid != 0x0160) {
        ESP_LOGE(TAG, "Bad part ID: 0x%04X", pid);
        return ESP_FAIL;
    }

    /* Set standard operating mode */
    uint8_t mode = 0x02; /* standard */
    i2c_bus_write(st->i2c_port, ENS160_ADDR, ENS160_REG_OPMODE, &mode, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    st->eco2_ppm = 400.0f;
    st->tvoc_ppb = 0.0f;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "ENS160 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t ens160_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    ens160_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    uint8_t status = 0;
    i2c_bus_read(st->i2c_port, ENS160_ADDR, ENS160_REG_STATUS, &status, 1);

    if ((status & 0x03) == 0x00) { /* NEWDAT bit set */
        uint8_t tvoc[2], eco2[2];
        i2c_bus_read(st->i2c_port, ENS160_ADDR, ENS160_REG_DATA_TVOC, tvoc, 2);
        i2c_bus_read(st->i2c_port, ENS160_ADDR, ENS160_REG_DATA_ECO2, eco2, 2);
        st->tvoc_ppb = (float)((uint16_t)(tvoc[1] << 8) | tvoc[0]);
        st->eco2_ppm = (float)((uint16_t)(eco2[1] << 8) | eco2[0]);
    }

    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->capability = field;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    switch (field) {
        case CAP_CO2_PPM:  out->f = st->eco2_ppm; break;
        case CAP_TVOC_PPB: out->f = st->tvoc_ppb; break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t ens160_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        uint8_t mode = 0x00; /* deep sleep */
        i2c_bus_write(s_state[h.driver_index].i2c_port, ENS160_ADDR, ENS160_REG_OPMODE, &mode, 1);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ens160_meta = {
    .name = "drv_ens160", .display_name = "ENS160 Multi-Gas Sensor",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_CO2_PPM, CAP_TVOC_PPB}, .num_capabilities = 2,
    .max_latency_us = 5000, .min_interval_ms = 1000,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_FLOAT,.f=400}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(ens160_state_t),
};

const driver_vtable_t drv_ens160_vtable = {
    .init=ens160_init, .read=ens160_read, .write=NULL, .deinit=ens160_deinit, .meta=&ens160_meta
};
