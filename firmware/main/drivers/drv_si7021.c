/**
 * @file drv_si7021.c
 * @brief Si7021 I2C temperature + relative humidity sensor driver.
 *
 * I2C address: 0x40 (fixed).
 *
 * Measurement sequence (no-hold master mode):
 *   1. Send 0xF3 (Measure Humidity, No Hold).
 *   2. Wait 25 ms.
 *   3. Read 2 bytes raw humidity.
 *      H [%RH] = 125.0 * raw / 65536.0 - 6.0
 *   4. Send 0xE0 (Read Temperature from Previous RH Measurement).
 *   5. Read 2 bytes raw temperature.
 *      T [°C]  = 175.72 * raw / 65536.0 - 46.85
 *
 * Note: 0xE0 reuses the temperature measured during the humidity conversion,
 * so no separate temperature trigger is needed.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_SI7021";

#define SI7021_ADDR             0x40
#define SI7021_CMD_MEAS_HUM     0xF3    /* No Hold */
#define SI7021_CMD_READ_TEMP    0xE0    /* From last RH measurement */
#define SI7021_CMD_RESET        0xFE
#define SI7021_MAX_INSTANCES    4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       temperature;
    float       humidity;
} si7021_state_t;

static si7021_state_t s_state[SI7021_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t si7021_init(const driver_config_t *cfg)
{
    if (s_instance_count >= SI7021_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    si7021_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = SI7021_ADDR;  /* Fixed address */

    /* Software reset */
    i2c_bus_write(st->i2c_port, st->address, SI7021_CMD_RESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(15));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "Si7021 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t si7021_sample(si7021_state_t *st)
{
    uint8_t raw[2];

    /* Trigger humidity measurement */
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, SI7021_CMD_MEAS_HUM, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Humidity trigger failed: %d", err);
        return err;
    }

    /* Wait for conversion (max 23 ms for 12-bit humidity) */
    vTaskDelay(pdMS_TO_TICKS(25));

    /* Read humidity raw */
    err = i2c_bus_read(st->i2c_port, st->address, SI7021_CMD_MEAS_HUM, raw, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Humidity read failed: %d", err);
        return err;
    }
    uint16_t raw_h = ((uint16_t)raw[0] << 8) | raw[1];
    st->humidity = 125.0f * (float)raw_h / 65536.0f - 6.0f;
    if (st->humidity < 0.0f)   st->humidity = 0.0f;
    if (st->humidity > 100.0f) st->humidity = 100.0f;

    /* Read temperature from previous RH measurement */
    err = i2c_bus_read(st->i2c_port, st->address, SI7021_CMD_READ_TEMP, raw, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Temp read failed: %d", err);
        return err;
    }
    uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    st->temperature = 175.72f * (float)raw_t / 65536.0f - 46.85f;
    return ESP_OK;
}

static esp_err_t si7021_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    si7021_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = si7021_sample(st);
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

static esp_err_t si7021_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t si7021_meta = {
    .name             = "drv_si7021",
    .display_name     = "Si7021 Temperature & Humidity",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE, CAP_HUMIDITY},
    .num_capabilities = 2,
    .max_latency_us   = 25000,
    .min_interval_ms  = 50,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(si7021_state_t),
};

const driver_vtable_t drv_si7021_vtable = {
    .init   = si7021_init,
    .read   = si7021_read,
    .write  = NULL,
    .deinit = si7021_deinit,
    .meta   = &si7021_meta,
};
