/**
 * @file drv_mpu6050.c
 * @brief MPU-6050 I2C 6-axis IMU driver (accelerometer + gyroscope).
 *
 * I2C address: 0x68 (AD0 low) or 0x69 (AD0 high).
 * Chip ID register 0x75 must return 0x68.
 * Wake: write 0x00 to PWR_MGMT_1 (0x6B).
 * Accel ±2g  → scale /16384.0f  (raw → g)
 * Gyro  ±250°/s → scale /131.0f (raw → °/s)
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DRV_MPU6050";

/* Register map */
#define MPU_REG_WHO_AM_I    0x75
#define MPU_REG_PWR_MGMT_1  0x6B
#define MPU_REG_ACCEL_XOUT  0x3B   /* 6 bytes: AX_H AX_L AY_H AY_L AZ_H AZ_L */
#define MPU_REG_GYRO_XOUT   0x43   /* 6 bytes: GX_H GX_L GY_H GY_L GZ_H GZ_L */
#define MPU_CHIP_ID         0x68
#define MPU_MAX_INSTANCES   4

/* Accel / Gyro full-scale defaults */
#define ACCEL_SCALE     16384.0f   /* LSB/g  for ±2g */
#define GYRO_SCALE      131.0f     /* LSB/(°/s) for ±250°/s */

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       ax, ay, az;  /* g */
    float       gx, gy, gz;  /* °/s */
} mpu6050_state_t;

static mpu6050_state_t s_state[MPU_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t mpu6050_init(const driver_config_t *cfg)
{
    if (s_instance_count >= MPU_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    mpu6050_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : 0x68;

    /* Verify chip ID */
    uint8_t who = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, MPU_REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (who != MPU_CHIP_ID) {
        ESP_LOGE(TAG, "Wrong WHO_AM_I: 0x%02X (expected 0x%02X)", who, MPU_CHIP_ID);
        return ESP_FAIL;
    }

    /* Wake up: clear SLEEP bit */
    uint8_t wake = 0x00;
    err = i2c_bus_write(st->i2c_port, st->address, MPU_REG_PWR_MGMT_1, &wake, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wake-up write failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "MPU6050 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t mpu6050_sample(mpu6050_state_t *st)
{
    uint8_t raw[6];

    /* Read accelerometer */
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, MPU_REG_ACCEL_XOUT, raw, 6);
    if (err != ESP_OK) return err;
    int16_t ax_r = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay_r = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az_r = (int16_t)((raw[4] << 8) | raw[5]);
    st->ax = ax_r / ACCEL_SCALE;
    st->ay = ay_r / ACCEL_SCALE;
    st->az = az_r / ACCEL_SCALE;

    /* Read gyroscope */
    err = i2c_bus_read(st->i2c_port, st->address, MPU_REG_GYRO_XOUT, raw, 6);
    if (err != ESP_OK) return err;
    int16_t gx_r = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t gy_r = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t gz_r = (int16_t)((raw[4] << 8) | raw[5]);
    st->gx = gx_r / GYRO_SCALE;
    st->gy = gy_r / GYRO_SCALE;
    st->gz = gz_r / GYRO_SCALE;
    return ESP_OK;
}

static esp_err_t mpu6050_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    mpu6050_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = mpu6050_sample(st);
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
        case CAP_GYROSCOPE_X:    out->f = st->gx; break;
        case CAP_GYROSCOPE_Y:    out->f = st->gy; break;
        case CAP_GYROSCOPE_Z:    out->f = st->gz; break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t mpu6050_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Put back to sleep */
        uint8_t sleep = 0x40;
        mpu6050_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, MPU_REG_PWR_MGMT_1, &sleep, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mpu6050_meta = {
    .name             = "drv_mpu6050",
    .display_name     = "MPU-6050 6-Axis IMU",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_ACCELERATION_X, CAP_ACCELERATION_Y, CAP_ACCELERATION_Z,
                         CAP_GYROSCOPE_X,    CAP_GYROSCOPE_Y,    CAP_GYROSCOPE_Z},
    .num_capabilities = 6,
    .max_latency_us   = 1000,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(mpu6050_state_t),
};

const driver_vtable_t drv_mpu6050_vtable = {
    .init   = mpu6050_init,
    .read   = mpu6050_read,
    .write  = NULL,
    .deinit = mpu6050_deinit,
    .meta   = &mpu6050_meta,
};
