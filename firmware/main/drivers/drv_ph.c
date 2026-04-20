/**
 * @file drv_ph.c
 * @brief pH sensor (analog) driver.
 *
 * Reads an analog pH electrode amplifier module (e.g. DFRobot SEN0161).
 * The module outputs a voltage proportional to pH.
 *
 * Conversion formula (standard pH module calibration):
 *   voltage = (adc_raw / 4095.0) * 3.3           [V]
 *   pH      = 7.0 + (2.5 - voltage) / 0.18       [pH units, factory cal]
 *   pH      += calibration_offset                 [user trimming]
 *
 * The 0.18 V/pH slope and 2.5 V at pH 7 are typical for this class of module.
 * Users can store a calibration offset in state via a specific write command
 * (write CAP_PH_LEVEL with a float value that sets the offset; this is a
 *  convention since the ABI has no dedicated calibration command).
 *
 * cfg->pins[0].adc_channel: ADC channel.
 * cfg->bus_index:            ADC unit (default ADC1).
 *
 * Read  CAP_PH_LEVEL: float pH value (0–14).
 * Write CAP_PH_LEVEL: float calibration offset in pH units (stored in state).
 *
 * Averages 16 ADC samples per read.
 * Max 4 instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "DRV_PH";

#define PH_MAX_INSTANCES        4
#define PH_ADC_UNIT             ADC_UNIT_1
#define PH_ADC_ATTEN            ADC_ATTEN_DB_11
#define PH_ADC_BITWIDTH         ADC_BITWIDTH_12
#define PH_ADC_SAMPLES          16
#define PH_VREF                 3.3f
#define PH_ADC_MAX              4095.0f
#define PH_MID_VOLTAGE          2.5f   /* V at pH 7.0 */
#define PH_SLOPE                0.18f  /* V / pH unit */

typedef struct {
    adc_channel_t               adc_channel;
    adc_oneshot_unit_handle_t   adc_handle;
    float                       calibration_offset;  /* pH units */
    float                       last_ph;
    bool                        initialized;
} ph_state_t;

static ph_state_t s_state[PH_MAX_INSTANCES];
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static uint8_t s_count = 0;

static esp_err_t ph_init(const driver_config_t *cfg)
{
    if (s_count >= PH_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max pH sensor instances (%d) reached", PH_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    ph_state_t *st = &s_state[s_count];
    st->adc_channel        = (adc_channel_t)cfg->pins[0].adc_channel;
    st->calibration_offset = 0.0f;
    st->last_ph            = 7.0f;

    if (s_adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t adc_cfg = {
            .unit_id  = PH_ADC_UNIT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC unit init failed: %d", err);
            return err;
        }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = PH_ADC_BITWIDTH,
        .atten    = PH_ADC_ATTEN,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc_handle, st->adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", err);
        return err;
    }

    st->adc_handle  = s_adc_handle;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "pH[%d] ADC ch%d", s_count - 1, st->adc_channel);
    return ESP_OK;
}

static esp_err_t ph_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    ph_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_PH_LEVEL) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Average ADC samples */
    int32_t sum = 0;
    for (int i = 0; i < PH_ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(st->adc_handle, st->adc_channel, &raw);
        if (err != ESP_OK) {
            out->error = DRV_ERR_BUS_FAIL;
            return err;
        }
        sum += raw;
    }
    float raw_avg = (float)sum / (float)PH_ADC_SAMPLES;
    float voltage = (raw_avg / PH_ADC_MAX) * PH_VREF;

    /* pH calculation */
    float ph = 7.0f + (PH_MID_VOLTAGE - voltage) / PH_SLOPE;
    ph += st->calibration_offset;
    if (ph < 0.0f)  ph = 0.0f;
    if (ph > 14.0f) ph = 14.0f;

    st->last_ph = ph;

    out->type         = VAL_TYPE_FLOAT;
    out->f            = ph;
    out->capability   = CAP_PH_LEVEL;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;

    ESP_LOGD(TAG, "pH[%d]: raw=%.1f V=%.3fV pH=%.2f (offset=%.2f)",
             idx, raw_avg, voltage, ph, st->calibration_offset);
    return ESP_OK;
}

static esp_err_t ph_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    /* Write CAP_PH_LEVEL with a float to set calibration offset */
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    ph_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_PH_LEVEL) return ESP_ERR_NOT_SUPPORTED;

    float offset;
    switch (cmd->type) {
        case VAL_TYPE_FLOAT: offset = cmd->f; break;
        case VAL_TYPE_INT:   offset = (float)cmd->i; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    if (offset < -7.0f || offset > 7.0f) return ESP_ERR_INVALID_ARG;

    st->calibration_offset = offset;
    ESP_LOGI(TAG, "pH[%d] calibration offset set to %.3f pH units", idx, offset);
    return ESP_OK;
}

static esp_err_t ph_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count) {
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ph_meta = {
    .name             = "drv_ph",
    .display_name     = "Analog pH Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ADC,
    .capabilities     = {CAP_PH_LEVEL},
    .num_capabilities = 1,
    .max_latency_us   = 5000,
    .min_interval_ms  = 200,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "ADC read failure", {.type = VAL_TYPE_FLOAT, .f = 7.0f}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(ph_state_t),
};

const driver_vtable_t drv_ph_vtable = {
    .init   = ph_init,
    .read   = ph_read,
    .write  = ph_write,
    .deinit = ph_deinit,
    .meta   = &ph_meta,
};
