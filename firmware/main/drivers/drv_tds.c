/**
 * @file drv_tds.c
 * @brief TDS (Total Dissolved Solids) water quality sensor driver.
 *
 * Analog sensor read via ADC.
 *
 * Formula (standard TDS sensor polynomial):
 *   voltage = (adc_raw / 4095.0) * 3.3           [V]
 *   kComp   = 1.0 + 0.02 * (temperature - 25.0)  [temperature compensation]
 *   TDS     = (133.42*v^3 - 255.86*v^2 + 857.39*v) * 0.5 / kComp   [ppm]
 *
 * Temperature assumed 25°C unless updated via an optional separate read
 * (state.temperature field can be set externally via a custom cmd path
 *  not exposed in this ABI — default 25°C is used).
 *
 * cfg->pins[0].adc_channel: ADC channel for TDS analog signal.
 * cfg->bus_index: ADC unit (0 = ADC1, 1 = ADC2; default ADC1).
 *
 * Read CAP_TDS_PPM: returns TDS in ppm (float).
 *
 * Averages 10 ADC samples per read to reduce noise.
 * Max 4 instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include <math.h>

static const char *TAG = "DRV_TDS";

#define TDS_MAX_INSTANCES       4
#define TDS_ADC_UNIT            ADC_UNIT_1
#define TDS_ADC_ATTEN           ADC_ATTEN_DB_11
#define TDS_ADC_BITWIDTH        ADC_BITWIDTH_12
#define TDS_ADC_SAMPLES         10
#define TDS_VREF                3.3f
#define TDS_ADC_MAX             4095.0f

typedef struct {
    adc_channel_t               adc_channel;
    adc_oneshot_unit_handle_t   adc_handle;
    float                       temperature;    /* °C for compensation, default 25 */
    float                       last_tds_ppm;
    bool                        initialized;
} tds_state_t;

static tds_state_t s_state[TDS_MAX_INSTANCES];
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static uint8_t s_count = 0;

static esp_err_t tds_init(const driver_config_t *cfg)
{
    if (s_count >= TDS_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max TDS instances (%d) reached", TDS_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    tds_state_t *st = &s_state[s_count];
    st->adc_channel  = (adc_channel_t)cfg->pins[0].adc_channel;
    st->temperature  = 25.0f;
    st->last_tds_ppm = 0.0f;

    if (s_adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t adc_cfg = {
            .unit_id  = TDS_ADC_UNIT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC unit init failed: %d", err);
            return err;
        }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = TDS_ADC_BITWIDTH,
        .atten    = TDS_ADC_ATTEN,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc_handle, st->adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", err);
        return err;
    }

    st->adc_handle  = s_adc_handle;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "TDS[%d] ADC ch%d", s_count - 1, st->adc_channel);
    return ESP_OK;
}

static esp_err_t tds_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    tds_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_TDS_PPM) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Average multiple ADC samples */
    int32_t sum = 0;
    for (int i = 0; i < TDS_ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(st->adc_handle, st->adc_channel, &raw);
        if (err != ESP_OK) {
            out->error = DRV_ERR_BUS_FAIL;
            return err;
        }
        sum += raw;
    }
    float raw_avg = (float)sum / (float)TDS_ADC_SAMPLES;

    float v = (raw_avg / TDS_ADC_MAX) * TDS_VREF;

    /* Temperature compensation coefficient */
    float kComp = 1.0f + 0.02f * (st->temperature - 25.0f);
    if (kComp < 0.01f) kComp = 0.01f;

    /* TDS polynomial (manufacturer-provided empirical fit) */
    float tds = (133.42f * v * v * v
               - 255.86f * v * v
               + 857.39f * v) * 0.5f / kComp;
    if (tds < 0.0f) tds = 0.0f;

    st->last_tds_ppm = tds;

    out->type         = VAL_TYPE_FLOAT;
    out->f            = tds;
    out->capability   = CAP_TDS_PPM;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;

    ESP_LOGD(TAG, "TDS[%d]: raw=%.1f V=%.3f TDS=%.1f ppm", idx, raw_avg, v, tds);
    return ESP_OK;
}

static esp_err_t tds_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t tds_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count) {
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t tds_meta = {
    .name             = "drv_tds",
    .display_name     = "TDS Water Quality Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ADC,
    .capabilities     = {CAP_TDS_PPM},
    .num_capabilities = 1,
    .max_latency_us   = 5000,
    .min_interval_ms  = 200,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "ADC read failure", {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(tds_state_t),
};

const driver_vtable_t drv_tds_vtable = {
    .init   = tds_init,
    .read   = tds_read,
    .write  = tds_write,
    .deinit = tds_deinit,
    .meta   = &tds_meta,
};
