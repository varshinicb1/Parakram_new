/**
 * @file drv_rain.c
 * @brief Rain sensor driver (digital + analog).
 *
 * The typical rain sensor module provides two outputs:
 *   DO pin (digital)  – comparator output: LOW when rain detected.
 *   AO pin (analog)   – raw ADC voltage proportional to wetness.
 *
 * cfg->pins[0]: Digital output GPIO (DO) — input with pull-up.
 * cfg->pins[1]: ADC GPIO (AO) — cfg->pins[1].adc_channel used.
 *
 * Read CAP_RAIN_LEVEL:
 *   Returns rain intensity 0.0–100.0 %.
 *   If DO indicates dry (HIGH) → 0%.
 *   Otherwise: intensity = 100.0 - (raw_adc / 4095.0 * 100.0)
 *   (Sensor conductance increases with rain → ADC voltage drops →
 *    lower ADC value means more rain; we invert for intuitive output.)
 *
 * Uses ESP-IDF ADC oneshot driver (adc_oneshot_*).
 * Default attenuation: ADC_ATTEN_DB_11 (0–3.3 V input range on ESP32-S3).
 *
 * Max 4 rain sensor instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "DRV_RAIN";

#define RAIN_MAX_INSTANCES      4
#define RAIN_ADC_UNIT           ADC_UNIT_1
#define RAIN_ADC_ATTEN          ADC_ATTEN_DB_11
#define RAIN_ADC_BITWIDTH       ADC_BITWIDTH_12    /* 12-bit → 0–4095 */
#define RAIN_ADC_MAX            4095

typedef struct {
    gpio_num_t          do_pin;
    adc_channel_t       adc_channel;
    adc_oneshot_unit_handle_t adc_handle;
    bool                has_digital;
    bool                has_analog;
    bool                initialized;
} rain_state_t;

static rain_state_t s_state[RAIN_MAX_INSTANCES];
/* Shared ADC unit handle; re-use if already initialised */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static uint8_t s_count = 0;

static esp_err_t rain_init(const driver_config_t *cfg)
{
    if (s_count >= RAIN_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max rain sensor instances (%d) reached", RAIN_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    rain_state_t *st = &s_state[s_count];
    st->do_pin      = (gpio_num_t)cfg->pins[0].gpio_num;
    st->has_digital = (st->do_pin >= 0);
    st->has_analog  = (cfg->pin_count >= 2 && cfg->pins[1].adc_channel != 0xFF);
    st->adc_channel = st->has_analog ? (adc_channel_t)cfg->pins[1].adc_channel
                                     : ADC_CHANNEL_0;

    /* Configure digital GPIO */
    if (st->has_digital) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << st->do_pin),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO config failed for DO pin %d: %d", st->do_pin, err);
            return err;
        }
    }

    /* Configure ADC */
    if (st->has_analog) {
        if (s_adc_handle == NULL) {
            adc_oneshot_unit_init_cfg_t adc_cfg = {
                .unit_id  = RAIN_ADC_UNIT,
                .ulp_mode = ADC_ULP_MODE_DISABLE,
            };
            esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ADC unit init failed: %d", err);
                return err;
            }
        }

        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = RAIN_ADC_BITWIDTH,
            .atten    = RAIN_ADC_ATTEN,
        };
        esp_err_t err = adc_oneshot_config_channel(s_adc_handle, st->adc_channel, &chan_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC channel config failed: %d", err);
            return err;
        }
        st->adc_handle = s_adc_handle;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Rain[%d]: DO=GPIO%d digital=%d analog=%d ch%d",
             s_count - 1, st->do_pin, st->has_digital, st->has_analog, st->adc_channel);
    return ESP_OK;
}

static esp_err_t rain_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    rain_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_RAIN_LEVEL) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    float level = 0.0f;

    /* Check digital pin first: HIGH = dry, LOW = rain */
    if (st->has_digital && gpio_get_level(st->do_pin) == 1) {
        /* Digital says dry — short-circuit to 0% */
        level = 0.0f;
    } else if (st->has_analog) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(st->adc_handle, st->adc_channel, &raw);
        if (err != ESP_OK) {
            out->error = DRV_ERR_BUS_FAIL;
            return err;
        }
        /* Lower ADC → more rain (more conductance). Invert for intuitive output. */
        level = 100.0f - ((float)raw / (float)RAIN_ADC_MAX * 100.0f);
        if (level < 0.0f)   level = 0.0f;
        if (level > 100.0f) level = 100.0f;
    } else if (st->has_digital) {
        /* Only digital: LOW = some rain → report 100% */
        level = 100.0f;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = level;
    out->capability   = CAP_RAIN_LEVEL;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t rain_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t rain_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count) {
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t rain_meta = {
    .name             = "drv_rain",
    .display_name     = "Rain Sensor (DO+AO)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ADC,
    .capabilities     = {CAP_RAIN_LEVEL},
    .num_capabilities = 1,
    .max_latency_us   = 2000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "ADC read failure", {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(rain_state_t),
};

const driver_vtable_t drv_rain_vtable = {
    .init   = rain_init,
    .read   = rain_read,
    .write  = rain_write,
    .deinit = rain_deinit,
    .meta   = &rain_meta,
};
