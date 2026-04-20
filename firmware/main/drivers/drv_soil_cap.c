/**
 * @file drv_soil_cap.c
 * @brief Capacitive soil moisture sensor driver — ADC input.
 *
 * ADC raw range: 0–4095 (12-bit).
 * Calibration constants:
 *   DRY_VALUE  = 3000  (sensor in dry air / bone-dry soil)
 *   WET_VALUE  = 1000  (sensor fully submerged in water)
 * Moisture [%] = clamp(100 * (DRY - raw) / (DRY - WET), 0, 100)
 *
 * Uses ADC1; channel is supplied via cfg->pins[0].adc_channel.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/adc.h"

static const char *TAG = "DRV_SOIL_CAP";

#define SOIL_MAX_INSTANCES  4
#define SOIL_DRY_VALUE      3000
#define SOIL_WET_VALUE      1000
#define SOIL_SAMPLES        8       /* Average N samples per read */

typedef struct {
    adc1_channel_t  channel;
    bool            initialized;
    float           moisture_pct;
} soil_state_t;

static soil_state_t s_state[SOIL_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t soil_init(const driver_config_t *cfg)
{
    if (s_instance_count >= SOIL_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    soil_state_t *st = &s_state[s_instance_count];
    st->channel = (adc1_channel_t)cfg->pins[0].adc_channel;

    /* Configure ADC1 */
    esp_err_t err = adc1_config_width(ADC_WIDTH_BIT_12);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC width config failed: %d", err);
        return err;
    }
    err = adc1_config_channel_atten(st->channel, ADC_ATTEN_DB_11);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "Soil moisture sensor initialized on ADC1 channel %d", st->channel);
    return ESP_OK;
}

static esp_err_t soil_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    soil_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_SOIL_MOISTURE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Average multiple ADC samples to reduce noise */
    int32_t sum = 0;
    for (int i = 0; i < SOIL_SAMPLES; i++) {
        int raw = adc1_get_raw(st->channel);
        if (raw < 0) {
            out->error = DRV_ERR_HW_FAULT;
            return ESP_FAIL;
        }
        sum += raw;
    }
    int32_t raw = sum / SOIL_SAMPLES;

    /* Compute moisture percentage */
    float pct = 100.0f * (float)(SOIL_DRY_VALUE - raw) /
                         (float)(SOIL_DRY_VALUE - SOIL_WET_VALUE);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    st->moisture_pct = pct;

    out->type         = VAL_TYPE_FLOAT;
    out->f            = pct;
    out->capability   = CAP_SOIL_MOISTURE;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t soil_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t soil_meta = {
    .name             = "drv_soil_cap",
    .display_name     = "Capacitive Soil Moisture Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ADC,
    .capabilities     = {CAP_SOIL_MOISTURE},
    .num_capabilities = 1,
    .max_latency_us   = 5000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "ADC read error", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(soil_state_t),
};

const driver_vtable_t drv_soil_cap_vtable = {
    .init   = soil_init,
    .read   = soil_read,
    .write  = NULL,
    .deinit = soil_deinit,
    .meta   = &soil_meta,
};
