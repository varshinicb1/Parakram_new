/**
 * @file drv_buzzer.c
 * @brief PWM buzzer driver via ESP32-S3 LEDC peripheral.
 *
 * Uses LEDC timer 1 (to avoid conflict with drv_servo on timer 0).
 * 50% duty cycle is used for tone generation.
 * Writing frequency 0 stops the buzzer (duty = 0).
 * Write CAP_TONE_HZ (int or float): frequency in Hz.
 * Read  CAP_TONE_HZ: returns current frequency.
 *
 * Supported frequency range: 20 Hz – 20000 Hz.
 * Max 4 buzzer instances on independent LEDC channels.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_BUZZER";

#define BUZZER_MAX_INSTANCES    4
#define BUZZER_TIMER            LEDC_TIMER_1
#define BUZZER_MODE             LEDC_LOW_SPEED_MODE
#define BUZZER_RESOLUTION       LEDC_TIMER_10_BIT   /* 1024 counts */
#define BUZZER_DEFAULT_FREQ_HZ  1000U
#define BUZZER_FREQ_MIN         20U
#define BUZZER_FREQ_MAX         20000U

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pin;
    ledc_channel_t  channel;
    uint32_t        freq_hz;        /* 0 = stopped */
    bool            initialized;
} buzzer_state_t;

static buzzer_state_t s_state[BUZZER_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t buzzer_set_freq(buzzer_state_t *st, uint32_t freq_hz)
{
    if (freq_hz == 0) {
        /* Stop: set duty to 0 */
        esp_err_t err = ledc_set_duty(BUZZER_MODE, st->channel, 0);
        if (err != ESP_OK) return err;
        err = ledc_update_duty(BUZZER_MODE, st->channel);
        if (err == ESP_OK) st->freq_hz = 0;
        return err;
    }

    if (freq_hz < BUZZER_FREQ_MIN) freq_hz = BUZZER_FREQ_MIN;
    if (freq_hz > BUZZER_FREQ_MAX) freq_hz = BUZZER_FREQ_MAX;

    /* Reconfigure timer frequency */
    esp_err_t err = ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_freq(%lu) failed: %d", (unsigned long)freq_hz, err);
        return err;
    }

    /* 50% duty at 10-bit resolution = 512 */
    uint32_t duty_50pct = (1u << LEDC_TIMER_10_BIT) / 2;
    err = ledc_set_duty(BUZZER_MODE, st->channel, duty_50pct);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(BUZZER_MODE, st->channel);
    if (err == ESP_OK) st->freq_hz = freq_hz;
    return err;
}

static esp_err_t buzzer_init(const driver_config_t *cfg)
{
    if (s_count >= BUZZER_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max buzzer instances (%d) reached", BUZZER_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    buzzer_state_t *st = &s_state[s_count];
    st->pin     = (gpio_num_t)cfg->pins[0].gpio_num;
    st->channel = (ledc_channel_t)(cfg->pins[0].pwm_channel % LEDC_CHANNEL_MAX);
    st->freq_hz = 0;

    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = BUZZER_MODE,
            .timer_num       = BUZZER_TIMER,
            .duty_resolution = BUZZER_RESOLUTION,
            .freq_hz         = BUZZER_DEFAULT_FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
        ESP_LOGI(TAG, "LEDC timer 1 configured for buzzer");
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = st->pin,
        .speed_mode = BUZZER_MODE,
        .channel    = st->channel,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,    /* start silent */
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    esp_err_t err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed for GPIO%d: %d", st->pin, err);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Buzzer[%d] on GPIO%d ch%d", s_count - 1, st->pin, st->channel);
    return ESP_OK;
}

static esp_err_t buzzer_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    buzzer_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_TONE_HZ) return ESP_ERR_NOT_SUPPORTED;

    uint32_t freq;
    switch (cmd->type) {
        case VAL_TYPE_INT:   freq = (uint32_t)cmd->i; break;
        case VAL_TYPE_FLOAT: freq = (uint32_t)cmd->f; break;
        case VAL_TYPE_BOOL:  freq = cmd->b ? BUZZER_DEFAULT_FREQ_HZ : 0; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Buzzer[%d] → %lu Hz", idx, (unsigned long)freq);
    return buzzer_set_freq(st, freq);
}

static esp_err_t buzzer_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    buzzer_state_t *st = &s_state[idx];

    if (field != CAP_TONE_HZ) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_INT;
    out->i            = (int32_t)st->freq_hz;
    out->capability   = CAP_TONE_HZ;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t buzzer_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        ledc_stop(BUZZER_MODE, s_state[idx].channel, 0);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t buzzer_meta = {
    .name             = "drv_buzzer",
    .display_name     = "PWM Buzzer",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_PWM,
    .capabilities     = {CAP_TONE_HZ},
    .num_capabilities = 1,
    .max_latency_us   = 500,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "LEDC PWM failure", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(buzzer_state_t),
};

const driver_vtable_t drv_buzzer_vtable = {
    .init   = buzzer_init,
    .read   = buzzer_read,
    .write  = buzzer_write,
    .deinit = buzzer_deinit,
    .meta   = &buzzer_meta,
};
