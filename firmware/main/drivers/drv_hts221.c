/**
 * @file drv_hts221.c
 * @brief HTS221 I2C temperature + humidity sensor driver (STMicroelectronics).
 *
 * I2C address: 0x5F (fixed).
 * WHO_AM_I register 0x0F = 0xBC.
 *
 * CTRL_REG1 (0x20) = 0x87: Power ON, BDU=1, ODR=12.5 Hz.
 *
 * Raw output registers (16-bit, little-endian):
 *   H_OUT: 0x28 (L), 0x29 (H)
 *   T_OUT: 0x2A (L), 0x2B (H)
 *
 * Calibration registers (read with multi-byte auto-increment; set bit7 of reg
 * for auto-increment → OR reg with 0x80):
 *   H0_rH_x2  0x30, H1_rH_x2  0x31
 *   T0_degC_x8 0x32, T1_degC_x8 0x33
 *   T1_T0 MSBs 0x35 (bits [3:2]=T1 bits [9:8], bits [1:0]=T0 bits [9:8])
 *   H0_T0_OUT  0x36-0x37, H1_T0_OUT  0x3A-0x3B
 *   T0_OUT     0x3C-0x3D, T1_OUT     0x3E-0x3F
 *
 * Calibration formula (linear interpolation):
 *   H [%RH] = H0 + (H1 - H0) * (H_OUT - H0_OUT) / (H1_OUT - H0_OUT)
 *   T [°C]  = T0 + (T1 - T0) * (T_OUT - T0_OUT) / (T1_OUT - T0_OUT)
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_HTS221";

#define HTS221_ADDR             0x5F
#define HTS221_REG_WHO_AM_I     0x0F
#define HTS221_REG_CTRL1        0x20
#define HTS221_REG_H_OUT_L      0x28
#define HTS221_REG_T_OUT_L      0x2A
#define HTS221_WHO_AM_I_VAL     0xBC
#define HTS221_MAX_INSTANCES    4

/* Auto-increment flag for multi-byte reads on HTS221 */
#define HTS221_AI(reg)          ((reg) | 0x80)

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       temperature;
    float       humidity;
    /* Calibration */
    float       h0_rh, h1_rh;
    int16_t     h0_out, h1_out;
    float       t0_degc, t1_degc;
    int16_t     t0_out, t1_out;
} hts221_state_t;

static hts221_state_t s_state[HTS221_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t hts221_load_cal(hts221_state_t *st)
{
    uint8_t cal[16];
    /* Read calibration block 0x30–0x3F (16 bytes) with auto-increment */
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, HTS221_AI(0x30), cal, 16);
    if (err != ESP_OK) return err;

    /* H0_rH_x2 @ 0x30, H1_rH_x2 @ 0x31 */
    st->h0_rh = (float)cal[0] / 2.0f;
    st->h1_rh = (float)cal[1] / 2.0f;

    /* T0_degC_x8 @ 0x32, T1_degC_x8 @ 0x33, MSBs @ 0x35 */
    uint8_t msbs = cal[5];
    uint16_t t0_x8 = ((uint16_t)(msbs & 0x03) << 8) | cal[2];
    uint16_t t1_x8 = ((uint16_t)((msbs >> 2) & 0x03) << 8) | cal[3];
    st->t0_degc = (float)t0_x8 / 8.0f;
    st->t1_degc = (float)t1_x8 / 8.0f;

    /* H0_T0_OUT @ 0x36-0x37, H1_T0_OUT @ 0x3A-0x3B */
    st->h0_out = (int16_t)(((uint16_t)cal[7]  << 8) | cal[6]);
    st->h1_out = (int16_t)(((uint16_t)cal[11] << 8) | cal[10]);

    /* T0_OUT @ 0x3C-0x3D, T1_OUT @ 0x3E-0x3F */
    st->t0_out = (int16_t)(((uint16_t)cal[13] << 8) | cal[12]);
    st->t1_out = (int16_t)(((uint16_t)cal[15] << 8) | cal[14]);

    ESP_LOGD(TAG, "CAL H0=%.1f H1=%.1f h0out=%d h1out=%d",
             st->h0_rh, st->h1_rh, st->h0_out, st->h1_out);
    ESP_LOGD(TAG, "CAL T0=%.2f T1=%.2f t0out=%d t1out=%d",
             st->t0_degc, st->t1_degc, st->t0_out, st->t1_out);
    return ESP_OK;
}

static esp_err_t hts221_init(const driver_config_t *cfg)
{
    if (s_instance_count >= HTS221_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    hts221_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = HTS221_ADDR;

    /* Verify WHO_AM_I */
    uint8_t who = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, HTS221_REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (who != HTS221_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Wrong WHO_AM_I: 0x%02X (expected 0x%02X)", who, HTS221_WHO_AM_I_VAL);
        return ESP_FAIL;
    }

    /* Load calibration */
    err = hts221_load_cal(st);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Calibration load failed: %d", err);
        return err;
    }

    /* Enable sensor: PD=1, BDU=1, ODR=12.5 Hz */
    uint8_t ctrl1 = 0x87;
    i2c_bus_write(st->i2c_port, st->address, HTS221_REG_CTRL1, &ctrl1, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "HTS221 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t hts221_sample(hts221_state_t *st)
{
    uint8_t raw[2];

    /* Read humidity output */
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, HTS221_AI(HTS221_REG_H_OUT_L), raw, 2);
    if (err != ESP_OK) return err;
    int16_t h_out = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);

    /* Read temperature output */
    err = i2c_bus_read(st->i2c_port, st->address, HTS221_AI(HTS221_REG_T_OUT_L), raw, 2);
    if (err != ESP_OK) return err;
    int16_t t_out = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);

    /* Apply calibration */
    float h_slope = (st->h1_rh - st->h0_rh) / (float)(st->h1_out - st->h0_out);
    st->humidity = st->h0_rh + h_slope * (float)(h_out - st->h0_out);
    if (st->humidity < 0.0f)   st->humidity = 0.0f;
    if (st->humidity > 100.0f) st->humidity = 100.0f;

    float t_slope = (st->t1_degc - st->t0_degc) / (float)(st->t1_out - st->t0_out);
    st->temperature = st->t0_degc + t_slope * (float)(t_out - st->t0_out);
    return ESP_OK;
}

static esp_err_t hts221_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    hts221_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = hts221_sample(st);
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
        case CAP_HUMIDITY:    out->f = st->humidity;    break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t hts221_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Power down: PD=0 */
        uint8_t ctrl1 = 0x07;
        hts221_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, HTS221_REG_CTRL1, &ctrl1, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t hts221_meta = {
    .name             = "drv_hts221",
    .display_name     = "HTS221 Temperature & Humidity (ST)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE, CAP_HUMIDITY},
    .num_capabilities = 2,
    .max_latency_us   = 100000,
    .min_interval_ms  = 80,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(hts221_state_t),
};

const driver_vtable_t drv_hts221_vtable = {
    .init   = hts221_init,
    .read   = hts221_read,
    .write  = NULL,
    .deinit = hts221_deinit,
    .meta   = &hts221_meta,
};
