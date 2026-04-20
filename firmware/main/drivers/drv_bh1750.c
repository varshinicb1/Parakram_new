/**
 * @file drv_bh1750.c
 * @brief BH1750 I2C ambient light sensor driver.
 *
 * I2C address: 0x23 (ADDR low) or 0x5C (ADDR high).
 * Send opcode 0x10 (continuous high-resolution mode, 1 lx resolution).
 * Read 2 bytes.  Lux = raw_count / 1.2f.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_BH1750";

#define BH1750_DEFAULT_ADDR     0x23
#define BH1750_CMD_CONT_HRES    0x10    /* Continuous H-Resolution Mode */
#define BH1750_MAX_INSTANCES    4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       lux;
} bh1750_state_t;

static bh1750_state_t s_state[BH1750_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t bh1750_init(const driver_config_t *cfg)
{
    if (s_instance_count >= BH1750_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    bh1750_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : BH1750_DEFAULT_ADDR;

    /* Power on */
    uint8_t power_on = 0x01;
    i2c_bus_write(st->i2c_port, st->address, 0x00, &power_on, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Set continuous high-resolution mode */
    uint8_t mode = BH1750_CMD_CONT_HRES;
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, 0x00, &mode, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mode set failed: %d", err);
        return err;
    }

    /* First measurement takes up to 180 ms */
    vTaskDelay(pdMS_TO_TICKS(200));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "BH1750 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t bh1750_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    bh1750_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_LIGHT_LUX) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* BH1750 continuous mode: just read 2 bytes; no register address needed */
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, 0x00, raw, 2);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    uint16_t count = ((uint16_t)raw[0] << 8) | raw[1];
    st->lux = (float)count / 1.2f;

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = CAP_LIGHT_LUX;
    out->f            = st->lux;
    return ESP_OK;
}

static esp_err_t bh1750_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Power down */
        uint8_t pd = 0x00;
        bh1750_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, 0x00, &pd, 1);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t bh1750_meta = {
    .name             = "drv_bh1750",
    .display_name     = "BH1750 Ambient Light Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_LIGHT_LUX},
    .num_capabilities = 1,
    .max_latency_us   = 200000,
    .min_interval_ms  = 200,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(bh1750_state_t),
};

const driver_vtable_t drv_bh1750_vtable = {
    .init   = bh1750_init,
    .read   = bh1750_read,
    .write  = NULL,
    .deinit = bh1750_deinit,
    .meta   = &bh1750_meta,
};
