/**
 * @file drv_fan_pwm.c
 * @brief PWM fan driver with optional tachometer RPM measurement.
 *
 * PWM output: LEDC timer 3, 25 kHz (standard 4-pin PC fan frequency),
 *             10-bit resolution (0–1023 counts).
 *
 * Tachometer (optional): GPIO input, falling-edge interrupt.
 *   Fan typically generates 2 pulses per revolution.
 *   RPM = (pulse_count / 2) * 60 / measurement_interval_s
 *   Measurement interval is 1 second using esp_timer periodic callback.
 *
 * cfg->pins[0] – PWM output (cfg->pins[0].pwm_channel for LEDC channel)
 * cfg->pins[1] – Tachometer GPIO input (optional; gpio_num=-1 to disable)
 *
 * Write CAP_SPEED_PERCENT (0–100): sets fan PWM duty cycle.
 * Read  CAP_SPEED_PERCENT: returns current duty %.
 *
 * Max 4 fan instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>

static const char *TAG = "DRV_FAN_PWM";

#define FAN_MAX_INSTANCES       4
#define FAN_TIMER               LEDC_TIMER_3
#define FAN_MODE                LEDC_LOW_SPEED_MODE
#define FAN_FREQ_HZ             25000U
#define FAN_RESOLUTION          LEDC_TIMER_10_BIT
#define FAN_PULSES_PER_REV      2
#define FAN_TACH_MEASURE_US     1000000  /* 1 second measurement window */

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pwm_pin;
    gpio_num_t      tach_pin;       /* -1 = no tachometer */
    ledc_channel_t  channel;
    int32_t         speed_pct;      /* 0–100 */
    volatile uint32_t pulse_count;  /* incremented in ISR */
    uint32_t        rpm;            /* calculated RPM */
    int64_t         last_measure_us;
    bool            tach_enabled;
    bool            initialized;
} fan_state_t;

static fan_state_t s_state[FAN_MAX_INSTANCES];
static uint8_t s_count = 0;

/* ISR: count tachometer pulses (falling edge = one pulse) */
static void IRAM_ATTR fan_tach_isr(void *arg)
{
    fan_state_t *st = (fan_state_t *)arg;
    st->pulse_count++;
}

static void fan_update_rpm(fan_state_t *st)
{
    if (!st->tach_enabled) return;

    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - st->last_measure_us;
    if (elapsed >= FAN_TACH_MEASURE_US) {
        /* Atomically snapshot and reset pulse count */
        portDISABLE_INTERRUPTS();
        uint32_t pulses = st->pulse_count;
        st->pulse_count = 0;
        portENABLE_INTERRUPTS();

        float elapsed_s = (float)elapsed / 1000000.0f;
        st->rpm = (uint32_t)((float)pulses / (float)FAN_PULSES_PER_REV / elapsed_s * 60.0f);
        st->last_measure_us = now;
    }
}

static esp_err_t fan_pwm_init(const driver_config_t *cfg)
{
    if (s_count >= FAN_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max fan instances (%d) reached", FAN_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    fan_state_t *st = &s_state[s_count];
    st->pwm_pin      = (gpio_num_t)cfg->pins[0].gpio_num;
    st->channel      = (ledc_channel_t)(cfg->pins[0].pwm_channel % LEDC_CHANNEL_MAX);
    st->speed_pct    = 0;
    st->pulse_count  = 0;
    st->rpm          = 0;
    st->last_measure_us = esp_timer_get_time();

    /* Optional tachometer pin */
    st->tach_pin     = (cfg->pin_count >= 2) ? (gpio_num_t)cfg->pins[1].gpio_num : -1;
    st->tach_enabled = (st->tach_pin >= 0);

    /* Install LEDC timer once */
    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = FAN_MODE,
            .timer_num       = FAN_TIMER,
            .duty_resolution = FAN_RESOLUTION,
            .freq_hz         = FAN_FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = st->pwm_pin,
        .speed_mode = FAN_MODE,
        .channel    = st->channel,
        .timer_sel  = FAN_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    esp_err_t err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %d", err);
        return err;
    }

    /* Configure tachometer GPIO if present */
    if (st->tach_enabled) {
        gpio_config_t tach_io = {
            .pin_bit_mask = (1ULL << st->tach_pin),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,
        };
        err = gpio_config(&tach_io);
        if (err == ESP_OK) {
            gpio_isr_handler_add(st->tach_pin, fan_tach_isr, st);
            ESP_LOGI(TAG, "Fan tachometer on GPIO%d", st->tach_pin);
        } else {
            ESP_LOGW(TAG, "Tachometer GPIO config failed (%d), RPM disabled", err);
            st->tach_enabled = false;
        }
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Fan PWM[%d]: GPIO%d ch%d 25kHz", s_count - 1, st->pwm_pin, st->channel);
    return ESP_OK;
}

static esp_err_t fan_pwm_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    fan_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_SPEED_PERCENT) return ESP_ERR_NOT_SUPPORTED;

    int32_t pct;
    switch (cmd->type) {
        case VAL_TYPE_INT:   pct = cmd->i; break;
        case VAL_TYPE_FLOAT: pct = (int32_t)cmd->f; break;
        case VAL_TYPE_BOOL:  pct = cmd->b ? 100 : 0; break;
        default:             return ESP_ERR_INVALID_ARG;
    }
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint32_t duty = (uint32_t)(pct * 1023 / 100);
    esp_err_t err = ledc_set_duty(FAN_MODE, st->channel, duty);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(FAN_MODE, st->channel);
    if (err == ESP_OK) {
        st->speed_pct = pct;
        ESP_LOGD(TAG, "Fan[%d] speed=%ld%% duty=%lu", idx, (long)pct, (unsigned long)duty);
    }
    return err;
}

static esp_err_t fan_pwm_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    fan_state_t *st = &s_state[idx];

    if (field != CAP_SPEED_PERCENT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    fan_update_rpm(st);

    out->type         = VAL_TYPE_INT;
    out->i            = st->speed_pct;
    out->capability   = CAP_SPEED_PERCENT;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t fan_pwm_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        ledc_set_duty(FAN_MODE, s_state[idx].channel, 0);
        ledc_update_duty(FAN_MODE, s_state[idx].channel);
        ledc_stop(FAN_MODE, s_state[idx].channel, 0);
        if (s_state[idx].tach_enabled) {
            gpio_isr_handler_remove(s_state[idx].tach_pin);
        }
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t fan_pwm_meta = {
    .name             = "drv_fan_pwm",
    .display_name     = "PWM Fan (25 kHz)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_PWM,
    .capabilities     = {CAP_SPEED_PERCENT},
    .num_capabilities = 1,
    .max_latency_us   = 500,
    .min_interval_ms  = 100,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "PWM or tachometer fault", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(fan_state_t),
};

const driver_vtable_t drv_fan_pwm_vtable = {
    .init   = fan_pwm_init,
    .read   = fan_pwm_read,
    .write  = fan_pwm_write,
    .deinit = fan_pwm_deinit,
    .meta   = &fan_pwm_meta,
};
