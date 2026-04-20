/**
 * @file drv_vl53l0x.c
 * @brief VL53L0X I2C Time-of-Flight distance sensor driver.
 *
 * I2C address: 0x29.
 * Chip ID: reg 0xC0 = 0xEE.
 *
 * Minimal init sequence (avoids the 200+ register ST API):
 *   1. Verify chip ID.
 *   2. Send mandatory VHV / phase calibration init.
 *   3. Configure for single-ranging mode.
 *
 * Single ranging:
 *   Write 0x01 to reg 0x00 (SYSRANGE_START).
 *   Poll reg 0x13 (RESULT_INTERRUPT_STATUS) bit 0 for result ready.
 *   Read 2 bytes from reg 0x1E (RESULT_RANGE_STATUS+10 = range in mm).
 *   Clear interrupt: write 0x01 to reg 0x0B.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_VL53L0X";

#define VL53L0X_DEFAULT_ADDR    0x29
#define VL53L0X_REG_ID          0xC0
#define VL53L0X_CHIP_ID         0xEE
#define VL53L0X_REG_SYSRANGE    0x00
#define VL53L0X_REG_INT_STATUS  0x13
#define VL53L0X_REG_RANGE_MM    0x1E
#define VL53L0X_REG_INT_CLEAR   0x0B
#define VL53L0X_POLL_TIMEOUT_MS 100
#define VL53L0X_MAX_INSTANCES   4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    uint16_t    distance_mm;
} vl53l0x_state_t;

static vl53l0x_state_t s_state[VL53L0X_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* Write single register byte */
static esp_err_t vl_wr(vl53l0x_state_t *st, uint8_t reg, uint8_t val)
{
    return i2c_bus_write(st->i2c_port, st->address, reg, &val, 1);
}

/* Read single register byte */
static esp_err_t vl_rd(vl53l0x_state_t *st, uint8_t reg, uint8_t *val)
{
    return i2c_bus_read(st->i2c_port, st->address, reg, val, 1);
}

static esp_err_t vl53l0x_init(const driver_config_t *cfg)
{
    if (s_instance_count >= VL53L0X_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    vl53l0x_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : VL53L0X_DEFAULT_ADDR;

    /* Verify chip */
    uint8_t id = 0;
    esp_err_t err = vl_rd(st, VL53L0X_REG_ID, &id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (id != VL53L0X_CHIP_ID) {
        ESP_LOGE(TAG, "Wrong chip ID: 0x%02X (expected 0x%02X)", id, VL53L0X_CHIP_ID);
        return ESP_FAIL;
    }

    /* Standard init sequence per ST application note AN4545 §3 */
    vl_wr(st, 0x88, 0x00);   /* I2C std mode */
    vl_wr(st, 0x80, 0x01);
    vl_wr(st, 0xFF, 0x01);
    vl_wr(st, 0x00, 0x00);
    uint8_t stop_var = 0;
    vl_rd(st, 0x91, &stop_var);
    vl_wr(st, 0x00, 0x01);
    vl_wr(st, 0xFF, 0x00);
    vl_wr(st, 0x80, 0x00);

    /* Disable SIGNAL_RATE_MSRC & SIGNAL_RATE_PRE_RANGE limit checks */
    vl_wr(st, 0x60, 0x00);

    /* Set signal rate limit 0.25 MCPS (0x00A0 in 9.7 fixed point) */
    uint8_t sr[2] = {0x00, 0xA0};
    i2c_bus_write(st->i2c_port, st->address, 0x44, sr, 2);

    /* Enable the data ready interrupt */
    vl_wr(st, 0x0A, 0x04);

    /* Set VCSEL period pre-range = 14 */
    vl_wr(st, 0x50, 0x0D);
    /* Set VCSEL period final-range = 10 */
    vl_wr(st, 0x70, 0x09);

    /* Set timing budget — use 200 ms default */
    vl_wr(st, 0x01, 0xFF);   /* sequence steps enabled */
    vl_wr(st, 0x02, 0x00);

    /* Perform VHV calibration */
    vl_wr(st, VL53L0X_REG_SYSRANGE, 0x01);
    vTaskDelay(pdMS_TO_TICKS(50));
    vl_wr(st, VL53L0X_REG_INT_CLEAR, 0x01);

    /* Perform phase calibration */
    vl_wr(st, 0x01, 0x02);
    vl_wr(st, VL53L0X_REG_SYSRANGE, 0x01);
    vTaskDelay(pdMS_TO_TICKS(50));
    vl_wr(st, VL53L0X_REG_INT_CLEAR, 0x01);
    vl_wr(st, 0x01, 0xFF);

    /* Restore stop variable */
    vl_wr(st, 0x80, 0x01);
    vl_wr(st, 0xFF, 0x01);
    vl_wr(st, 0x00, 0x00);
    vl_wr(st, 0x91, stop_var);
    vl_wr(st, 0x00, 0x01);
    vl_wr(st, 0xFF, 0x00);
    vl_wr(st, 0x80, 0x00);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "VL53L0X initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t vl53l0x_sample(vl53l0x_state_t *st)
{
    /* Start single ranging */
    vl_wr(st, 0x80, 0x01);
    vl_wr(st, 0xFF, 0x01);
    vl_wr(st, 0x00, 0x00);
    vl_wr(st, 0x91, 0x00);   /* stop_var already handled in init */
    vl_wr(st, 0x00, 0x01);
    vl_wr(st, 0xFF, 0x00);
    vl_wr(st, 0x80, 0x00);
    vl_wr(st, VL53L0X_REG_SYSRANGE, 0x01);

    /* Poll for result ready (bit 0 of 0x13) */
    int64_t t0 = esp_timer_get_time();
    uint8_t status = 0;
    do {
        if ((esp_timer_get_time() - t0) > (VL53L0X_POLL_TIMEOUT_MS * 1000LL)) {
            ESP_LOGE(TAG, "Range result timeout");
            return ESP_ERR_TIMEOUT;
        }
        vl_rd(st, VL53L0X_REG_INT_STATUS, &status);
    } while ((status & 0x07) == 0);

    /* Read 2-byte range mm */
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, VL53L0X_REG_RANGE_MM, raw, 2);
    if (err != ESP_OK) return err;
    st->distance_mm = ((uint16_t)raw[0] << 8) | raw[1];

    /* Clear interrupt */
    vl_wr(st, VL53L0X_REG_INT_CLEAR, 0x01);
    return ESP_OK;
}

static esp_err_t vl53l0x_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    vl53l0x_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_DISTANCE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = vl53l0x_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_TIMEOUT;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = (float)st->distance_mm;   /* mm */
    out->capability   = CAP_DISTANCE;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t vl53l0x_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t vl53l0x_meta = {
    .name             = "drv_vl53l0x",
    .display_name     = "VL53L0X ToF Distance Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_DISTANCE},
    .num_capabilities = 1,
    .max_latency_us   = 110000,
    .min_interval_ms  = 30,
    .failure_modes    = {
        {DRV_ERR_TIMEOUT,  "Range measurement timeout",  {.type=VAL_TYPE_FLOAT, .f=0}},
        {DRV_ERR_BUS_FAIL, "I2C communication failure",  {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(vl53l0x_state_t),
};

const driver_vtable_t drv_vl53l0x_vtable = {
    .init   = vl53l0x_init,
    .read   = vl53l0x_read,
    .write  = NULL,
    .deinit = vl53l0x_deinit,
    .meta   = &vl53l0x_meta,
};
