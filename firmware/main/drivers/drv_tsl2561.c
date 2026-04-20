/**
 * @file drv_tsl2561.c
 * @brief TSL2561 I2C ambient light sensor driver.
 *
 * I2C address: 0x29 (ADDR=GND), 0x39 (ADDR=float), 0x49 (ADDR=VDD).
 * Default used here: 0x39.
 *
 * Protocol note: TSL2561 uses command register (0x80 | reg).
 *   Power on:  write 0x03 to CMD|0x00 → 0x80
 *   CH0 low:   read  CMD|0x0C → 0x8C (2 bytes, little-endian)
 *   CH1 low:   read  CMD|0x0E → 0x8E (2 bytes, little-endian)
 *
 * Simplified lux formula for T/FN/CL package (Appendix I, Table 2):
 *   ratio = ch1 / ch0  (if ch0 == 0, lux = 0)
 *   For ratio ≤ 0.5:  lux = 0.0304 * ch0 - 0.062 * ch0 * ratio^1.4
 *   Simple linear approximation used here for embedded footprint:
 *   lux = 16.18 * ch0 - 30.96 * ch1
 *   (Accurate enough for ratio 0.0–0.5 typical indoor light.)
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_TSL2561";

#define TSL2561_DEFAULT_ADDR    0x39
#define TSL2561_CMD_CTRL        0x80    /* CMD | CONTROL */
#define TSL2561_CMD_CH0         0x8C    /* CMD | WORD | CH0 low byte */
#define TSL2561_CMD_CH1         0x8E    /* CMD | WORD | CH1 low byte */
#define TSL2561_MAX_INSTANCES   4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       lux;
} tsl2561_state_t;

static tsl2561_state_t s_state[TSL2561_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t tsl2561_init(const driver_config_t *cfg)
{
    if (s_instance_count >= TSL2561_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    tsl2561_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : TSL2561_DEFAULT_ADDR;

    /* Power on: write 0x03 to control register */
    uint8_t pwr = 0x03;
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, TSL2561_CMD_CTRL, &pwr, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Power-on write failed: %d", err);
        return err;
    }
    /* Allow 400 ms integration (default timing) */
    vTaskDelay(pdMS_TO_TICKS(420));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "TSL2561 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t tsl2561_sample(tsl2561_state_t *st)
{
    uint8_t raw[2];

    /* Read CH0 (broadband) */
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, TSL2561_CMD_CH0, raw, 2);
    if (err != ESP_OK) return err;
    uint16_t ch0 = ((uint16_t)raw[1] << 8) | raw[0];

    /* Read CH1 (IR) */
    err = i2c_bus_read(st->i2c_port, st->address, TSL2561_CMD_CH1, raw, 2);
    if (err != ESP_OK) return err;
    uint16_t ch1 = ((uint16_t)raw[1] << 8) | raw[0];

    if (ch0 == 0) {
        st->lux = 0.0f;
        return ESP_OK;
    }

    float lux = 16.18f * (float)ch0 - 30.96f * (float)ch1;
    if (lux < 0.0f) lux = 0.0f;
    st->lux = lux;
    return ESP_OK;
}

static esp_err_t tsl2561_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    tsl2561_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_LIGHT_LUX) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = tsl2561_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = st->lux;
    out->capability   = CAP_LIGHT_LUX;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t tsl2561_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Power down: write 0x00 to control */
        uint8_t off = 0x00;
        tsl2561_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, TSL2561_CMD_CTRL, &off, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t tsl2561_meta = {
    .name             = "drv_tsl2561",
    .display_name     = "TSL2561 Ambient Light Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_LIGHT_LUX},
    .num_capabilities = 1,
    .max_latency_us   = 420000,
    .min_interval_ms  = 500,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(tsl2561_state_t),
};

const driver_vtable_t drv_tsl2561_vtable = {
    .init   = tsl2561_init,
    .read   = tsl2561_read,
    .write  = NULL,
    .deinit = tsl2561_deinit,
    .meta   = &tsl2561_meta,
};
