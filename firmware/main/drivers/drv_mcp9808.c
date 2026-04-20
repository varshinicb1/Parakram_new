/**
 * @file drv_mcp9808.c
 * @brief MCP9808 I2C precision temperature sensor driver.
 *
 * I2C address: 0x18 (A2=A1=A0=GND).
 *
 * Manufacturer ID register 0x06: reads 0x0054.
 * Ambient Temperature register 0x05: 2 bytes.
 *   Bits [15:13]: alert/sign flags.
 *   Bits [12]:    sign bit (negative if set).
 *   Bits [11:0]:  temperature magnitude in 1/16 °C.
 *
 * Decode:
 *   raw = reg16 & 0x1FFF
 *   if (reg16 & 0x1000) temp = (raw & 0x0FFF) / 16.0f - 256.0f
 *   else                temp = (raw & 0x0FFF) / 16.0f
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DRV_MCP9808";

#define MCP9808_REG_CONFIG      0x01
#define MCP9808_REG_AMBIENT     0x05
#define MCP9808_REG_MFR_ID      0x06
#define MCP9808_REG_DEV_ID      0x07
#define MCP9808_DEFAULT_ADDR    0x18
#define MCP9808_MFR_ID          0x0054
#define MCP9808_MAX_INSTANCES   4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       temperature;
} mcp9808_state_t;

static mcp9808_state_t s_state[MCP9808_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t mcp9808_init(const driver_config_t *cfg)
{
    if (s_instance_count >= MCP9808_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    mcp9808_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : MCP9808_DEFAULT_ADDR;

    /* Verify manufacturer ID */
    uint8_t mfr[2];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, MCP9808_REG_MFR_ID, mfr, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    uint16_t mfr_id = ((uint16_t)mfr[0] << 8) | mfr[1];
    if (mfr_id != MCP9808_MFR_ID) {
        ESP_LOGE(TAG, "Wrong MFR ID: 0x%04X (expected 0x%04X)", mfr_id, MCP9808_MFR_ID);
        return ESP_FAIL;
    }

    /* Clear CONFIG register → continuous conversion, default settings */
    uint8_t cfg_bytes[2] = {0x00, 0x00};
    i2c_bus_write(st->i2c_port, st->address, MCP9808_REG_CONFIG, cfg_bytes, 2);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "MCP9808 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t mcp9808_sample(mcp9808_state_t *st)
{
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, MCP9808_REG_AMBIENT, raw, 2);
    if (err != ESP_OK) return err;

    uint16_t reg16 = ((uint16_t)raw[0] << 8) | raw[1];

    /* Decode temperature */
    uint16_t magnitude = reg16 & 0x0FFF;
    float temp = (float)magnitude / 16.0f;
    if (reg16 & 0x1000) {   /* Sign bit: negative */
        temp -= 256.0f;
    }
    st->temperature = temp;
    return ESP_OK;
}

static esp_err_t mcp9808_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    mcp9808_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_TEMPERATURE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = mcp9808_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = st->temperature;
    out->capability   = CAP_TEMPERATURE;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t mcp9808_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Shutdown mode: CONFIG bit 8 = 1 */
        uint8_t shutdown[2] = {0x01, 0x00};
        mcp9808_state_t *st = &s_state[h.driver_index];
        i2c_bus_write(st->i2c_port, st->address, MCP9808_REG_CONFIG, shutdown, 2);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mcp9808_meta = {
    .name             = "drv_mcp9808",
    .display_name     = "MCP9808 Precision Temperature",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE},
    .num_capabilities = 1,
    .max_latency_us   = 250000,
    .min_interval_ms  = 250,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(mcp9808_state_t),
};

const driver_vtable_t drv_mcp9808_vtable = {
    .init   = mcp9808_init,
    .read   = mcp9808_read,
    .write  = NULL,
    .deinit = mcp9808_deinit,
    .meta   = &mcp9808_meta,
};
