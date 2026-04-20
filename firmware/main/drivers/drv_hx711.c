/**
 * @file drv_hx711.c
 * @brief HX711 24-bit ADC for load cells (weight scale).
 *
 * Two-wire bit-bang interface:
 *   DOUT: input (data ready when low)
 *   SCK:  output (clock pulses)
 *
 * Read cycle:
 *   1. Wait for DOUT to go low (data ready; timeout 100 ms).
 *   2. Send 24 SCK pulses, reading MSB-first on each rising edge.
 *   3. Send 1 extra pulse (total 25) → selects Channel A gain 128.
 *   4. Result is a signed 24-bit two's complement value.
 *
 * Zero offset and calibration factor:
 *   raw_tared = raw - tare_offset
 *   grams = raw_tared / cal_factor
 *
 * Default cal_factor = 2280.0f (adjust per hardware).
 * The tare offset is set during init by averaging 10 samples.
 * CAP_COUNT stores the gram value as an integer.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "DRV_HX711";

#define HX711_MAX_INSTANCES     4
#define HX711_READY_TIMEOUT_US  200000   /* 200 ms */
#define HX711_DEFAULT_CAL       2280.0f
#define HX711_TARE_SAMPLES      10

typedef struct {
    gpio_num_t  dout_pin;
    gpio_num_t  sck_pin;
    bool        initialized;
    int32_t     tare_offset;
    float       cal_factor;
    float       grams;
} hx711_state_t;

static hx711_state_t s_state[HX711_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* Read one 24-bit sample from HX711 (interrupts NOT disabled — works on ESP32-S3
 * where the clock is stable enough; for critical precision, wrap in taskENTER_CRITICAL) */
static bool hx711_read_raw(hx711_state_t *st, int32_t *out)
{
    /* Wait for DOUT to go low */
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(st->dout_pin)) {
        if ((esp_timer_get_time() - t0) > HX711_READY_TIMEOUT_US) {
            ESP_LOGD(TAG, "HX711 not ready");
            return false;
        }
        esp_rom_delay_us(10);
    }

    /* Read 24 bits MSB first */
    int32_t raw = 0;
    for (int i = 0; i < 24; i++) {
        gpio_set_level(st->sck_pin, 1);
        esp_rom_delay_us(1);
        raw <<= 1;
        if (gpio_get_level(st->dout_pin)) raw |= 1;
        gpio_set_level(st->sck_pin, 0);
        esp_rom_delay_us(1);
    }

    /* 25th pulse: select channel A, gain 128 */
    gpio_set_level(st->sck_pin, 1);
    esp_rom_delay_us(1);
    gpio_set_level(st->sck_pin, 0);
    esp_rom_delay_us(1);

    /* Sign-extend 24-bit to 32-bit */
    if (raw & 0x800000) raw |= (int32_t)0xFF000000;

    *out = raw;
    return true;
}

static esp_err_t hx711_init(const driver_config_t *cfg)
{
    if (s_instance_count >= HX711_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    hx711_state_t *st = &s_state[s_instance_count];
    st->dout_pin   = (gpio_num_t)cfg->pins[0].gpio_num;
    st->sck_pin    = (gpio_num_t)cfg->pins[1].gpio_num;
    st->cal_factor = HX711_DEFAULT_CAL;

    /* Configure DOUT as input */
    gpio_config_t dout_cfg = {
        .pin_bit_mask  = (1ULL << st->dout_pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&dout_cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "DOUT config: %d", err); return err; }

    /* Configure SCK as output, initially low */
    gpio_config_t sck_cfg = {
        .pin_bit_mask  = (1ULL << st->sck_pin),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&sck_cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "SCK config: %d", err); return err; }
    gpio_set_level(st->sck_pin, 0);

    /* Tare: average HX711_TARE_SAMPLES readings */
    int64_t sum = 0;
    int good = 0;
    for (int i = 0; i < HX711_TARE_SAMPLES; i++) {
        int32_t r = 0;
        if (hx711_read_raw(st, &r)) { sum += r; good++; }
    }
    st->tare_offset = (good > 0) ? (int32_t)(sum / good) : 0;

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "HX711 initialized DOUT=GPIO%d SCK=GPIO%d tare=%ld",
             st->dout_pin, st->sck_pin, (long)st->tare_offset);
    return ESP_OK;
}

static esp_err_t hx711_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    hx711_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_COUNT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    int32_t raw = 0;
    if (!hx711_read_raw(st, &raw)) {
        out->error = DRV_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    st->grams = (float)(raw - st->tare_offset) / st->cal_factor;

    out->type         = VAL_TYPE_INT;
    out->i            = (int32_t)st->grams;
    out->capability   = CAP_COUNT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t hx711_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Pull SCK high → power down */
        gpio_set_level(s_state[h.driver_index].sck_pin, 1);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t hx711_meta = {
    .name             = "drv_hx711",
    .display_name     = "HX711 Load Cell ADC (Weight)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_COUNT},
    .num_capabilities = 1,
    .max_latency_us   = 210000,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_TIMEOUT, "HX711 data not ready (check power)", {.type=VAL_TYPE_INT, .i=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(hx711_state_t),
};

const driver_vtable_t drv_hx711_vtable = {
    .init   = hx711_init,
    .read   = hx711_read,
    .write  = NULL,
    .deinit = hx711_deinit,
    .meta   = &hx711_meta,
};
