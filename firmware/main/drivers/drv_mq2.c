/**
 * @file drv_mq2.c
 * @brief MQ-2 gas / smoke sensor driver — ADC input.
 *
 * The MQ-2 outputs an analog voltage proportional to gas concentration.
 * Simplified power-law conversion (sensor in clean air RS/R0 ≈ 9.8):
 *   ratio = raw / 4095.0
 *   ppm   = 1000 * pow(ratio, -2.3)
 *
 * Both CAP_GAS_PPM and CAP_SMOKE_PPM report the same value; callers may
 * distinguish by the `field` requested.
 *
 * Channel via cfg->pins[0].adc_channel.  ADC1 only.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/adc.h"
#include <math.h>

static const char *TAG = "DRV_MQ2";

#define MQ2_MAX_INSTANCES   4
#define MQ2_SAMPLES         8

typedef struct {
    adc1_channel_t  channel;
    bool            initialized;
    float           ppm;
} mq2_state_t;

static mq2_state_t s_state[MQ2_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t mq2_init(const driver_config_t *cfg)
{
    if (s_instance_count >= MQ2_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    mq2_state_t *st = &s_state[s_instance_count];
    st->channel = (adc1_channel_t)cfg->pins[0].adc_channel;

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
    ESP_LOGI(TAG, "MQ-2 initialized on ADC1 channel %d", st->channel);
    return ESP_OK;
}

static esp_err_t mq2_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    mq2_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_GAS_PPM && field != CAP_SMOKE_PPM) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    int32_t sum = 0;
    for (int i = 0; i < MQ2_SAMPLES; i++) {
        int raw = adc1_get_raw(st->channel);
        if (raw < 0) {
            out->error = DRV_ERR_HW_FAULT;
            return ESP_FAIL;
        }
        sum += raw;
    }
    int32_t raw = sum / MQ2_SAMPLES;

    float ratio = (float)raw / 4095.0f;
    if (ratio < 0.001f) ratio = 0.001f;  /* avoid zero/negative base */
    st->ppm = 1000.0f * powf(ratio, -2.3f);

    out->type         = VAL_TYPE_FLOAT;
    out->f            = st->ppm;
    out->capability   = field;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t mq2_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mq2_meta = {
    .name             = "drv_mq2",
    .display_name     = "MQ-2 Gas / Smoke Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ADC,
    .capabilities     = {CAP_GAS_PPM, CAP_SMOKE_PPM},
    .num_capabilities = 2,
    .max_latency_us   = 5000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "ADC read error", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(mq2_state_t),
};

const driver_vtable_t drv_mq2_vtable = {
    .init   = mq2_init,
    .read   = mq2_read,
    .write  = NULL,
    .deinit = mq2_deinit,
    .meta   = &mq2_meta,
};
