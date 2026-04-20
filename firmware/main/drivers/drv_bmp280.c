/**
 * @file drv_bmp280.c
 * @brief BMP280 I2C pressure + temperature sensor driver.
 *
 * Identical to BME280 but without humidity.
 * I2C address: 0x76 or 0x77.
 * Chip ID register 0xD0: 0x58 (BMP280) or 0xBF (BMP280 engineering sample).
 * Calibration: dig_T1..T3, dig_P1..P9 from 0x88–0x9F.
 * Compensation formulas: same as BME280 datasheet §4.2.3.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DRV_BMP280";

/* Register addresses */
#define BMP280_REG_CHIP_ID      0xD0
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_PRESS_MSB    0xF7   /* 6 bytes: P_MSB P_LSB P_XLSB T_MSB T_LSB T_XLSB */

#define BMP280_MAX_INSTANCES    4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    /* Calibration */
    uint16_t    dig_T1;
    int16_t     dig_T2, dig_T3;
    uint16_t    dig_P1;
    int16_t     dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    int32_t     t_fine;
    /* Results */
    float       temperature;
    float       pressure;
} bmp280_state_t;

static bmp280_state_t s_state[BMP280_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t bmp280_init(const driver_config_t *cfg)
{
    if (s_instance_count >= BMP280_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    bmp280_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : 0x76;

    /* Verify chip ID */
    uint8_t id = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, BMP280_REG_CHIP_ID, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (id != 0x58 && id != 0xBF) {
        ESP_LOGE(TAG, "Wrong chip ID: 0x%02X", id);
        return ESP_FAIL;
    }

    /* Read calibration data (0x88–0x9D = 24 bytes) */
    uint8_t cal[24];
    i2c_bus_read(st->i2c_port, st->address, 0x88, cal, 24);
    st->dig_T1 = (uint16_t)(cal[1]  << 8) | cal[0];
    st->dig_T2 = (int16_t)((cal[3]  << 8) | cal[2]);
    st->dig_T3 = (int16_t)((cal[5]  << 8) | cal[4]);
    st->dig_P1 = (uint16_t)(cal[7]  << 8) | cal[6];
    st->dig_P2 = (int16_t)((cal[9]  << 8) | cal[8]);
    st->dig_P3 = (int16_t)((cal[11] << 8) | cal[10]);
    st->dig_P4 = (int16_t)((cal[13] << 8) | cal[12]);
    st->dig_P5 = (int16_t)((cal[15] << 8) | cal[14]);
    st->dig_P6 = (int16_t)((cal[17] << 8) | cal[16]);
    st->dig_P7 = (int16_t)((cal[19] << 8) | cal[18]);
    st->dig_P8 = (int16_t)((cal[21] << 8) | cal[20]);
    st->dig_P9 = (int16_t)((cal[23] << 8) | cal[22]);

    /* Configure: normal mode, T os x1, P os x1, filter off, t_sb 0.5 ms */
    uint8_t config   = 0x00; /* filter off, t_sb = 0.5 ms */
    uint8_t ctrl     = 0x27; /* T os x1, P os x1, normal mode */
    i2c_bus_write(st->i2c_port, st->address, BMP280_REG_CONFIG,    &config, 1);
    i2c_bus_write(st->i2c_port, st->address, BMP280_REG_CTRL_MEAS, &ctrl,   1);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "BMP280 (ID=0x%02X) initialized at 0x%02X on I2C%d",
             id, st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t bmp280_sample(bmp280_state_t *st)
{
    uint8_t raw[6];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, BMP280_REG_PRESS_MSB, raw, 6);
    if (err != ESP_OK) return err;

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

    /* Temperature compensation */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)st->dig_T1 << 1))) * st->dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)st->dig_T1) *
                      ((adc_T >> 4) - (int32_t)st->dig_T1)) >> 12) * st->dig_T3) >> 14;
    st->t_fine  = var1 + var2;
    st->temperature = ((st->t_fine * 5 + 128) >> 8) / 100.0f;

    /* Pressure compensation */
    int64_t pv1 = (int64_t)st->t_fine - 128000;
    int64_t pv2 = pv1 * pv1 * (int64_t)st->dig_P6;
    pv2 = pv2 + ((pv1 * (int64_t)st->dig_P5) << 17);
    pv2 = pv2 + ((int64_t)st->dig_P4 << 35);
    pv1 = ((pv1 * pv1 * (int64_t)st->dig_P3) >> 8) +
          ((pv1 * (int64_t)st->dig_P2) << 12);
    pv1 = (((int64_t)1 << 47) + pv1) * (int64_t)st->dig_P1 >> 33;
    if (pv1 == 0) {
        st->pressure = 0;
    } else {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - pv2) * 3125) / pv1;
        pv1 = ((int64_t)st->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
        pv2 = ((int64_t)st->dig_P8 * p) >> 19;
        st->pressure = ((p + pv1 + pv2) >> 8) / 25600.0f; /* hPa */
    }
    return ESP_OK;
}

static esp_err_t bmp280_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    bmp280_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = bmp280_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = field;

    switch (field) {
        case CAP_TEMPERATURE: out->f = st->temperature; break;
        case CAP_PRESSURE:    out->f = st->pressure;    break;
        case CAP_ALTITUDE:
            out->f = 44330.0f * (1.0f - powf(st->pressure / 1013.25f, 0.1903f));
            break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t bmp280_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Sleep mode: ctrl_meas = 0x00 */
        uint8_t sleep = 0x00;
        bmp280_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, BMP280_REG_CTRL_MEAS, &sleep, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t bmp280_meta = {
    .name             = "drv_bmp280",
    .display_name     = "BMP280 Pressure & Temperature",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE, CAP_PRESSURE, CAP_ALTITUDE},
    .num_capabilities = 3,
    .max_latency_us   = 2000,
    .min_interval_ms  = 500,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(bmp280_state_t),
};

const driver_vtable_t drv_bmp280_vtable = {
    .init   = bmp280_init,
    .read   = bmp280_read,
    .write  = NULL,
    .deinit = bmp280_deinit,
    .meta   = &bmp280_meta,
};
