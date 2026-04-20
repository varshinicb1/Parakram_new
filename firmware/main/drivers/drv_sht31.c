/**
 * @file drv_sht31.c
 * @brief SHT31 I2C temperature + humidity sensor driver.
 *
 * I2C address: 0x44 (ADDR pin low) or 0x45 (ADDR pin high).
 * Single-shot measurement command: 0x24, 0x00 (high-repeatability, no clock stretch).
 * Wait 15 ms, then read 6 bytes:
 *   [0] temp_MSB  [1] temp_LSB  [2] temp_CRC
 *   [3] hum_MSB   [4] hum_LSB   [5] hum_CRC
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DRV_SHT31";

#define SHT31_DEFAULT_ADDR  0x44
#define SHT31_MAX_INSTANCES 4

/* SHT31 uses 16-bit register-free commands; we send raw bytes */
extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       temperature;
    float       humidity;
} sht31_state_t;

static sht31_state_t s_state[SHT31_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* CRC-8 polynomial 0x31, init 0xFF */
static uint8_t sht31_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

static esp_err_t sht31_init(const driver_config_t *cfg)
{
    if (s_instance_count >= SHT31_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    sht31_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : SHT31_DEFAULT_ADDR;

    /* Send soft-reset 0x30A2 to clear any previous state */
    uint8_t rst = 0xA2;
    i2c_bus_write(st->i2c_port, st->address, 0x30, &rst, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "SHT31 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t sht31_sample(sht31_state_t *st)
{
    /* Send single-shot command: 0x24 0x00 */
    uint8_t cmd = 0x00;
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, 0x24, &cmd, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Command write failed: %d", err);
        return err;
    }

    /* Wait for measurement (15 ms high-repeatability) */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Read 6 bytes; SHT31 doesn't use a register address for reads.
     * We pass 0x00 as dummy reg — the ABI i2c_bus_read will handle the
     * register-less read if the HAL is configured accordingly.
     * Using reg=0x00 with len=6 is the standard approach. */
    uint8_t raw[6];
    err = i2c_bus_read(st->i2c_port, st->address, 0x00, raw, 6);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Data read failed: %d", err);
        return err;
    }

    /* Verify CRCs */
    if (sht31_crc8(raw, 2) != raw[2]) {
        ESP_LOGE(TAG, "Temperature CRC error");
        return ESP_FAIL;
    }
    if (sht31_crc8(raw + 3, 2) != raw[5]) {
        ESP_LOGE(TAG, "Humidity CRC error");
        return ESP_FAIL;
    }

    uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_h = ((uint16_t)raw[3] << 8) | raw[4];

    st->temperature = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    st->humidity    = 100.0f * (float)raw_h / 65535.0f;
    return ESP_OK;
}

static esp_err_t sht31_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    sht31_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = sht31_sample(st);
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

static esp_err_t sht31_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t sht31_meta = {
    .name             = "drv_sht31",
    .display_name     = "SHT31 Temperature & Humidity",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE, CAP_HUMIDITY},
    .num_capabilities = 2,
    .max_latency_us   = 20000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
        {DRV_ERR_CRC,      "CRC mismatch",              {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(sht31_state_t),
};

const driver_vtable_t drv_sht31_vtable = {
    .init   = sht31_init,
    .read   = sht31_read,
    .write  = NULL,
    .deinit = sht31_deinit,
    .meta   = &sht31_meta,
};
