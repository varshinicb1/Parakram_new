/**
 * @file drv_bme280.c
 * @brief BME280 I2C driver — temperature, humidity, pressure, altitude.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DRV_BME280";

/* BME280 registers */
#define BME280_REG_CHIP_ID      0xD0
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_PRESS_MSB    0xF7
#define BME280_CHIP_ID          0x60

/* Internal state — NO heap allocation */
typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    /* Calibration data */
    uint16_t    dig_T1;
    int16_t     dig_T2, dig_T3;
    uint16_t    dig_P1;
    int16_t     dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t     dig_H1, dig_H3;
    int16_t     dig_H2, dig_H4, dig_H5;
    int8_t      dig_H6;
    int32_t     t_fine;
    /* Last values */
    float       temperature;
    float       humidity;
    float       pressure;
} bme280_state_t;

/* Static allocation — one per possible instance */
static bme280_state_t s_state[2]; /* max 2 BME280 sensors (0x76, 0x77) */
static uint8_t s_instance_count = 0;

/* Forward declarations for I2C bus functions */
extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

static esp_err_t bme280_init(const driver_config_t *cfg) {
    if (s_instance_count >= 2) return ESP_ERR_NO_MEM;

    bme280_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address = cfg->i2c_address;

    /* Verify chip ID */
    uint8_t chip_id = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, BME280_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Wrong chip ID: 0x%02X (expected 0x%02X)", chip_id, BME280_CHIP_ID);
        return ESP_FAIL;
    }

    /* Read calibration data */
    uint8_t calib[26];
    i2c_bus_read(st->i2c_port, st->address, 0x88, calib, 26);
    st->dig_T1 = (uint16_t)(calib[1] << 8) | calib[0];
    st->dig_T2 = (int16_t)((calib[3] << 8) | calib[2]);
    st->dig_T3 = (int16_t)((calib[5] << 8) | calib[4]);
    st->dig_P1 = (uint16_t)(calib[7] << 8) | calib[6];
    st->dig_P2 = (int16_t)((calib[9] << 8) | calib[8]);
    st->dig_P3 = (int16_t)((calib[11] << 8) | calib[10]);
    st->dig_P4 = (int16_t)((calib[13] << 8) | calib[12]);
    st->dig_P5 = (int16_t)((calib[15] << 8) | calib[14]);
    st->dig_P6 = (int16_t)((calib[17] << 8) | calib[16]);
    st->dig_P7 = (int16_t)((calib[19] << 8) | calib[18]);
    st->dig_P8 = (int16_t)((calib[21] << 8) | calib[20]);
    st->dig_P9 = (int16_t)((calib[23] << 8) | calib[22]);

    /* Humidity calibration */
    i2c_bus_read(st->i2c_port, st->address, 0xA1, &st->dig_H1, 1);
    uint8_t hcal[7];
    i2c_bus_read(st->i2c_port, st->address, 0xE1, hcal, 7);
    st->dig_H2 = (int16_t)((hcal[1] << 8) | hcal[0]);
    st->dig_H3 = hcal[2];
    st->dig_H4 = (int16_t)((hcal[3] << 4) | (hcal[4] & 0x0F));
    st->dig_H5 = (int16_t)((hcal[5] << 4) | (hcal[4] >> 4));
    st->dig_H6 = (int8_t)hcal[6];

    /* Configure: 1x oversampling, normal mode */
    uint8_t ctrl_hum = 0x01; /* Humidity oversampling x1 */
    i2c_bus_write(st->i2c_port, st->address, BME280_REG_CTRL_HUM, &ctrl_hum, 1);

    uint8_t config = 0xA0; /* t_sb = 1000ms, filter = off */
    i2c_bus_write(st->i2c_port, st->address, BME280_REG_CONFIG, &config, 1);

    uint8_t ctrl_meas = 0x27; /* temp os x1, press os x1, normal mode */
    i2c_bus_write(st->i2c_port, st->address, BME280_REG_CTRL_MEAS, &ctrl_meas, 1);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "BME280 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t bme280_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_instance_count) return ESP_ERR_INVALID_ARG;
    bme280_state_t *st = &s_state[h.driver_index >= 2 ? 0 : h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    /* Read raw data */
    uint8_t raw[8];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, BME280_REG_PRESS_MSB, raw, 8);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return ESP_FAIL;
    }

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];

    /* Compensate temperature */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)st->dig_T1 << 1))) * st->dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)st->dig_T1) *
                      ((adc_T >> 4) - (int32_t)st->dig_T1)) >> 12) * st->dig_T3) >> 14;
    st->t_fine = var1 + var2;
    st->temperature = ((st->t_fine * 5 + 128) >> 8) / 100.0f;

    /* Compensate pressure */
    int64_t p_var1 = (int64_t)st->t_fine - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)st->dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)st->dig_P5) << 17);
    p_var2 = p_var2 + ((int64_t)st->dig_P4 << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)st->dig_P3) >> 8) +
             ((p_var1 * (int64_t)st->dig_P2) << 12);
    p_var1 = (((int64_t)1 << 47) + p_var1) * (int64_t)st->dig_P1 >> 33;
    if (p_var1 == 0) {
        st->pressure = 0;
    } else {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p_var2) * 3125) / p_var1;
        p_var1 = ((int64_t)st->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
        p_var2 = ((int64_t)st->dig_P8 * p) >> 19;
        st->pressure = ((p + p_var1 + p_var2) >> 8) / 25600.0f; /* hPa */
    }

    /* Compensate humidity */
    int32_t h_val = st->t_fine - 76800;
    h_val = (((((adc_H << 14) - ((int32_t)st->dig_H4 << 20) - ((int32_t)st->dig_H5 * h_val)) +
               16384) >> 15) * (((((((h_val * (int32_t)st->dig_H6) >> 10) *
               (((h_val * (int32_t)st->dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
               (int32_t)st->dig_H2 + 8192) >> 14));
    h_val = h_val - (((((h_val >> 15) * (h_val >> 15)) >> 7) * (int32_t)st->dig_H1) >> 4);
    h_val = (h_val < 0) ? 0 : h_val;
    h_val = (h_val > 419430400) ? 419430400 : h_val;
    st->humidity = (h_val >> 12) / 1024.0f;

    /* Return requested field */
    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability = field;

    switch (field) {
        case CAP_TEMPERATURE: out->f = st->temperature; break;
        case CAP_HUMIDITY:    out->f = st->humidity; break;
        case CAP_PRESSURE:    out->f = st->pressure; break;
        case CAP_ALTITUDE:    out->f = 44330.0f * (1.0f - powf(st->pressure / 1013.25f, 0.1903f)); break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

static esp_err_t bme280_deinit(driver_handle_t h) {
    if (h.driver_index < 2) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t bme280_meta = {
    .name = "drv_bme280",
    .display_name = "BME280 Environment Sensor",
    .version = "1.0.0",
    .type = DRIVER_TYPE_SENSOR,
    .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_TEMPERATURE, CAP_HUMIDITY, CAP_PRESSURE, CAP_ALTITUDE},
    .num_capabilities = 4,
    .max_latency_us = 2000,
    .min_interval_ms = 500,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}}},
    .num_failure_modes = 1,
    .internal_state_size = sizeof(bme280_state_t),
};

const driver_vtable_t drv_bme280_vtable = {
    .init   = bme280_init,
    .read   = bme280_read,
    .write  = NULL,
    .deinit = bme280_deinit,
    .meta   = &bme280_meta,
};
