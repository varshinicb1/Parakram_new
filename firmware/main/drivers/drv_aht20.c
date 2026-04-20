/**
 * @file drv_aht20.c
 * @brief AHT20 I2C temperature + humidity sensor driver.
 *
 * I2C address: 0x38.
 *
 * Init sequence: send [0xBE, 0x08, 0x00].
 * Trigger measurement: send [0xAC, 0x33, 0x00].
 * Wait 80 ms.
 * Read 6 bytes: [state, H_19-12, H_11-4, H_3-0|T_19-16, T_15-8, T_7-0].
 *
 * Humidity [%RH]  = ((data[1]<<12)|(data[2]<<4)|(data[3]>>4)) / 1048576.0 * 100
 * Temperature [°C]= (((data[3]&0x0F)<<16)|(data[4]<<8)|data[5]) / 1048576.0 * 200 - 50
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_AHT20";

#define AHT20_DEFAULT_ADDR  0x38
#define AHT20_MAX_INSTANCES 4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       temperature;
    float       humidity;
} aht20_state_t;

static aht20_state_t s_state[AHT20_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t aht20_init(const driver_config_t *cfg)
{
    if (s_instance_count >= AHT20_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    aht20_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : AHT20_DEFAULT_ADDR;

    /* Power-on delay */
    vTaskDelay(pdMS_TO_TICKS(40));

    /* Initialization: [0x08, 0x00] after command byte 0xBE */
    uint8_t init_args[2] = {0x08, 0x00};
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, 0xBE, init_args, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Init command failed: %d", err);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "AHT20 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t aht20_sample(aht20_state_t *st)
{
    /* Trigger measurement: [0x33, 0x00] after command byte 0xAC */
    uint8_t trig[2] = {0x33, 0x00};
    esp_err_t err = i2c_bus_write(st->i2c_port, st->address, 0xAC, trig, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Trigger failed: %d", err);
        return err;
    }

    /* Wait for measurement (80 ms typical) */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Read 6 bytes — use reg 0x00 as dummy (HAL reads without sub-address) */
    uint8_t data[6];
    err = i2c_bus_read(st->i2c_port, st->address, 0x00, data, 6);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Data read failed: %d", err);
        return err;
    }

    /* Check busy bit (bit 7 of data[0]) */
    if (data[0] & 0x80) {
        ESP_LOGD(TAG, "Sensor still busy");
        return ESP_ERR_NOT_FINISHED;
    }

    uint32_t raw_h = ((uint32_t)data[1] << 12) |
                     ((uint32_t)data[2] << 4)  |
                     ((uint32_t)data[3] >> 4);
    uint32_t raw_t = (((uint32_t)data[3] & 0x0F) << 16) |
                     ((uint32_t)data[4] << 8)             |
                     (uint32_t)data[5];

    st->humidity    = (float)raw_h / 1048576.0f * 100.0f;
    st->temperature = (float)raw_t / 1048576.0f * 200.0f - 50.0f;
    return ESP_OK;
}

static esp_err_t aht20_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    aht20_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = aht20_sample(st);
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

static esp_err_t aht20_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t aht20_meta = {
    .name             = "drv_aht20",
    .display_name     = "AHT20 Temperature & Humidity",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE, CAP_HUMIDITY},
    .num_capabilities = 2,
    .max_latency_us   = 100000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
        {DRV_ERR_BUSY,     "Sensor measurement in progress", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(aht20_state_t),
};

const driver_vtable_t drv_aht20_vtable = {
    .init   = aht20_init,
    .read   = aht20_read,
    .write  = NULL,
    .deinit = aht20_deinit,
    .meta   = &aht20_meta,
};
