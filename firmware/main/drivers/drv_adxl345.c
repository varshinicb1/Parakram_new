/**
 * @file drv_adxl345.c
 * @brief ADXL345 I2C 3-axis accelerometer driver.
 *
 * I2C address: 0x53 (ALT ADDRESS pin low) or 0x1D (high).
 * Chip ID register 0x00 = 0xE5.
 *
 * Init:
 *   POWER_CTL 0x2D = 0x08   (measure mode)
 *   DATA_FORMAT 0x31 = 0x0B  (full resolution, ±16g, right-justify)
 *
 * Data registers 0x32–0x37: X_L X_H Y_L Y_H Z_L Z_H (little-endian int16).
 * Scale in full-resolution ±16g mode: 4 mg/LSB → raw * 0.004f [g].
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DRV_ADXL345";

#define ADXL_REG_DEVID      0x00
#define ADXL_REG_POWER_CTL  0x2D
#define ADXL_REG_DATA_FMT   0x31
#define ADXL_REG_DATAX0     0x32
#define ADXL_CHIP_ID        0xE5
#define ADXL_DEFAULT_ADDR   0x53
#define ADXL_MAX_INSTANCES  4
#define ADXL_SCALE          0.004f    /* g per LSB in full-res ±16g */

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       ax, ay, az;
} adxl345_state_t;

static adxl345_state_t s_state[ADXL_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t adxl345_init(const driver_config_t *cfg)
{
    if (s_instance_count >= ADXL_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    adxl345_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : ADXL_DEFAULT_ADDR;

    /* Verify device ID */
    uint8_t devid = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, ADXL_REG_DEVID, &devid, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (devid != ADXL_CHIP_ID) {
        ESP_LOGE(TAG, "Wrong DEVID: 0x%02X (expected 0x%02X)", devid, ADXL_CHIP_ID);
        return ESP_FAIL;
    }

    /* Enter measurement mode */
    uint8_t pwr = 0x08;
    err = i2c_bus_write(st->i2c_port, st->address, ADXL_REG_POWER_CTL, &pwr, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POWER_CTL write failed: %d", err);
        return err;
    }

    /* Full resolution, ±16g, right-justify */
    uint8_t fmt = 0x0B;
    i2c_bus_write(st->i2c_port, st->address, ADXL_REG_DATA_FMT, &fmt, 1);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "ADXL345 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t adxl345_sample(adxl345_state_t *st)
{
    uint8_t raw[6];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, ADXL_REG_DATAX0, raw, 6);
    if (err != ESP_OK) return err;

    int16_t xr = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
    int16_t yr = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
    int16_t zr = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);

    st->ax = (float)xr * ADXL_SCALE;
    st->ay = (float)yr * ADXL_SCALE;
    st->az = (float)zr * ADXL_SCALE;
    return ESP_OK;
}

static esp_err_t adxl345_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    adxl345_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = adxl345_sample(st);
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
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t adxl345_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Standby mode */
        uint8_t standby = 0x00;
        adxl345_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, ADXL_REG_POWER_CTL, &standby, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t adxl345_meta = {
    .name             = "drv_adxl345",
    .display_name     = "ADXL345 3-Axis Accelerometer",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_ACCELERATION_X, CAP_ACCELERATION_Y, CAP_ACCELERATION_Z},
    .num_capabilities = 3,
    .max_latency_us   = 1000,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(adxl345_state_t),
};

const driver_vtable_t drv_adxl345_vtable = {
    .init   = adxl345_init,
    .read   = adxl345_read,
    .write  = NULL,
    .deinit = adxl345_deinit,
    .meta   = &adxl345_meta,
};
