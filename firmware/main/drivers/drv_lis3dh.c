/**
 * @file drv_lis3dh.c
 * @brief LIS3DH I2C 3-axis accelerometer driver.
 *
 * I2C address: 0x18 (SDO/SA0 = GND) or 0x19 (SDO/SA0 = VCC).
 * WHO_AM_I register 0x0F must return 0x33.
 *
 * Initialisation:
 *   CTRL_REG1 (0x20) = 0x57  → ODR 100 Hz, normal mode, XYZ enabled
 *   CTRL_REG4 (0x23) = 0x00  → ±2g full scale, high-resolution disabled
 *
 * Data registers (0x28|0x80 for multi-byte read in auto-increment mode):
 *   OUT_X_L (0x28), OUT_X_H (0x29)
 *   OUT_Y_L (0x2A), OUT_Y_H (0x2B)
 *   OUT_Z_L (0x2C), OUT_Z_H (0x2D)
 *
 * Raw 16-bit signed values, left-justified in 12-bit mode.
 * Scale at ±2g: 1 mg/LSB for 10-bit (normal mode) = raw >> 6 * (4/4096) g.
 * Using the common formula for ±2g, 16-bit raw:
 *   accel_g = (int16_t)raw / 16384.0f   (16384 LSB/g at ±2g full scale)
 *   accel_ms2 = accel_g * 9.81f
 *
 * Read CAP_ACCELERATION_X / Y / Z: float m/s².
 *
 * Max 4 instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DRV_LIS3DH";

/* Register map */
#define LIS3DH_REG_WHO_AM_I     0x0F
#define LIS3DH_REG_CTRL_REG1    0x20
#define LIS3DH_REG_CTRL_REG4    0x23
#define LIS3DH_REG_OUT_X_L      0x28
#define LIS3DH_WHO_AM_I_VAL     0x33
#define LIS3DH_DEFAULT_ADDR     0x18

/* Multi-byte read: set bit 7 (auto-increment) */
#define LIS3DH_AUTO_INC         0x80

#define LIS3DH_MAX_INSTANCES    4
#define LIS3DH_SCALE            16384.0f   /* LSB/g at ±2g */
#define GRAVITY_MS2             9.81f

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t i2c_port;
    uint8_t address;
    float   ax, ay, az;    /* m/s² */
    bool    initialized;
} lis3dh_state_t;

static lis3dh_state_t s_state[LIS3DH_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t lis3dh_init(const driver_config_t *cfg)
{
    if (s_count >= LIS3DH_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max LIS3DH instances (%d) reached", LIS3DH_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    lis3dh_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : LIS3DH_DEFAULT_ADDR;

    /* Verify WHO_AM_I */
    uint8_t who = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, LIS3DH_REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read WHO_AM_I failed: %d", err);
        return ESP_FAIL;
    }
    if (who != LIS3DH_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: 0x%02X (expected 0x%02X)", who, LIS3DH_WHO_AM_I_VAL);
        return ESP_FAIL;
    }

    /* CTRL_REG1: ODR=100Hz, normal mode, X/Y/Z enabled */
    uint8_t ctrl1 = 0x57;
    err = i2c_bus_write(st->i2c_port, st->address, LIS3DH_REG_CTRL_REG1, &ctrl1, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CTRL_REG1 write failed: %d", err);
        return err;
    }

    /* CTRL_REG4: ±2g, high-resolution disabled, little-endian */
    uint8_t ctrl4 = 0x00;
    err = i2c_bus_write(st->i2c_port, st->address, LIS3DH_REG_CTRL_REG4, &ctrl4, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CTRL_REG4 write failed: %d", err);
        return err;
    }

    st->ax = st->ay = st->az = 0.0f;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "LIS3DH[%d] at 0x%02X on I2C%d (±2g, 100Hz)",
             s_count - 1, st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t lis3dh_sample(lis3dh_state_t *st)
{
    uint8_t raw[6];
    /* Bit 7 set = auto-increment register address across 6 bytes */
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address,
                                  LIS3DH_REG_OUT_X_L | LIS3DH_AUTO_INC, raw, 6);
    if (err != ESP_OK) return err;

    /* Little-endian 16-bit, left-justified 12-bit value */
    int16_t rx = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ry = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t rz = (int16_t)((raw[5] << 8) | raw[4]);

    st->ax = (rx / LIS3DH_SCALE) * GRAVITY_MS2;
    st->ay = (ry / LIS3DH_SCALE) * GRAVITY_MS2;
    st->az = (rz / LIS3DH_SCALE) * GRAVITY_MS2;
    return ESP_OK;
}

static esp_err_t lis3dh_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    lis3dh_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = lis3dh_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = field;

    switch (field) {
        case CAP_ACCELERATION_X: out->f = st->ax; break;
        case CAP_ACCELERATION_Y: out->f = st->ay; break;
        case CAP_ACCELERATION_Z: out->f = st->az; break;
        default:
            out->error = DRV_ERR_NOT_SUPPORTED;
            return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGD(TAG, "LIS3DH[%d] ax=%.3f ay=%.3f az=%.3f m/s²",
             idx, st->ax, st->ay, st->az);
    return ESP_OK;
}

static esp_err_t lis3dh_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t lis3dh_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        /* Power-down mode: ODR = 0000 in CTRL_REG1 */
        uint8_t pd = 0x00;
        i2c_bus_write(s_state[idx].i2c_port, s_state[idx].address,
                      LIS3DH_REG_CTRL_REG1, &pd, 1);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t lis3dh_meta = {
    .name             = "drv_lis3dh",
    .display_name     = "LIS3DH 3-Axis Accelerometer",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_ACCELERATION_X, CAP_ACCELERATION_Y, CAP_ACCELERATION_Z},
    .num_capabilities = 3,
    .max_latency_us   = 2000,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C read failure",   {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
        {DRV_ERR_HW_FAULT, "WHO_AM_I mismatch", {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(lis3dh_state_t),
};

const driver_vtable_t drv_lis3dh_vtable = {
    .init   = lis3dh_init,
    .read   = lis3dh_read,
    .write  = lis3dh_write,
    .deinit = lis3dh_deinit,
    .meta   = &lis3dh_meta,
};
