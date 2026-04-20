/**
 * @file drv_drv8833.c
 * @brief DRV8833 dual H-bridge motor driver.
 *
 * The DRV8833 drives two DC motors using 4 PWM-capable pins:
 *   AIN1, AIN2 — Motor A control
 *   BIN1, BIN2 — Motor B control
 *
 * Each motor channel is driven by two pins. By applying PWM to one or both
 * pins and holding the other low/high, we achieve forward, reverse, coast,
 * and brake modes.
 *
 * Mode encoding per channel (xIN1, xIN2):
 *   Coast   : 0, 0
 *   Forward : PWM, 0  (xIN1 has duty, xIN2=0)
 *   Reverse : 0, PWM  (xIN1=0, xIN2 has duty)
 *   Brake   : 1, 1    (both high, PWM=full duty)
 *
 * cfg->pins[]:
 *   pins[0] – AIN1 (GPIO + PWM channel)
 *   pins[1] – AIN2 (GPIO + PWM channel)
 *   pins[2] – BIN1 (GPIO + PWM channel, second motor — optional)
 *   pins[3] – BIN2 (GPIO + PWM channel, second motor — optional)
 *
 * Write CAP_SPEED_PERCENT: int32/float -100…+100.
 *   +100 = full forward, -100 = full reverse, 0 = coast.
 * Read  CAP_SPEED_PERCENT: returns current speed.
 *
 * LEDC timer 2, 20 kHz, 10-bit resolution.
 * Max 8 channel instances (4 boards × 2 channels each).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_DRV8833";

#define DRV8833_MAX_INSTANCES   8
#define DRV8833_TIMER           LEDC_TIMER_2
#define DRV8833_MODE            LEDC_LOW_SPEED_MODE
#define DRV8833_FREQ_HZ         20000U  /* 20 kHz — above audible range */
#define DRV8833_RESOLUTION      LEDC_TIMER_10_BIT

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pin_in1;
    gpio_num_t      pin_in2;
    ledc_channel_t  ch_in1;
    ledc_channel_t  ch_in2;
    int32_t         speed;      /* -100…+100 */
    bool            initialized;
} drv8833_state_t;

static drv8833_state_t s_state[DRV8833_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t drv8833_apply(drv8833_state_t *st, int32_t speed)
{
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;

    int32_t abs_spd = speed < 0 ? -speed : speed;
    uint32_t duty   = (uint32_t)(abs_spd * 1023 / 100);

    if (speed > 0) {
        /* Forward: IN1=PWM, IN2=0 */
        ledc_set_duty(DRV8833_MODE, st->ch_in1, duty);
        ledc_update_duty(DRV8833_MODE, st->ch_in1);
        ledc_set_duty(DRV8833_MODE, st->ch_in2, 0);
        ledc_update_duty(DRV8833_MODE, st->ch_in2);
    } else if (speed < 0) {
        /* Reverse: IN1=0, IN2=PWM */
        ledc_set_duty(DRV8833_MODE, st->ch_in1, 0);
        ledc_update_duty(DRV8833_MODE, st->ch_in1);
        ledc_set_duty(DRV8833_MODE, st->ch_in2, duty);
        ledc_update_duty(DRV8833_MODE, st->ch_in2);
    } else {
        /* Coast: both 0 */
        ledc_set_duty(DRV8833_MODE, st->ch_in1, 0);
        ledc_update_duty(DRV8833_MODE, st->ch_in1);
        ledc_set_duty(DRV8833_MODE, st->ch_in2, 0);
        ledc_update_duty(DRV8833_MODE, st->ch_in2);
    }

    st->speed = speed;
    return ESP_OK;
}

static esp_err_t drv8833_init(const driver_config_t *cfg)
{
    if (s_count >= DRV8833_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max DRV8833 instances (%d) reached", DRV8833_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    drv8833_state_t *st = &s_state[s_count];
    st->pin_in1 = (gpio_num_t)cfg->pins[0].gpio_num;
    st->pin_in2 = (gpio_num_t)cfg->pins[1].gpio_num;
    st->ch_in1  = (ledc_channel_t)(cfg->pins[0].pwm_channel % LEDC_CHANNEL_MAX);
    st->ch_in2  = (ledc_channel_t)(cfg->pins[1].pwm_channel % LEDC_CHANNEL_MAX);
    st->speed   = 0;

    /* Install LEDC timer once */
    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = DRV8833_MODE,
            .timer_num       = DRV8833_TIMER,
            .duty_resolution = DRV8833_RESOLUTION,
            .freq_hz         = DRV8833_FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
    }

    /* Configure IN1 channel */
    ledc_channel_config_t ch1 = {
        .gpio_num   = st->pin_in1,
        .speed_mode = DRV8833_MODE,
        .channel    = st->ch_in1,
        .timer_sel  = DRV8833_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    esp_err_t err = ledc_channel_config(&ch1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC ch_in1 config failed: %d", err);
        return err;
    }

    /* Configure IN2 channel */
    ledc_channel_config_t ch2 = {
        .gpio_num   = st->pin_in2,
        .speed_mode = DRV8833_MODE,
        .channel    = st->ch_in2,
        .timer_sel  = DRV8833_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    err = ledc_channel_config(&ch2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC ch_in2 config failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "DRV8833[%d]: IN1=GPIO%d(ch%d) IN2=GPIO%d(ch%d)",
             s_count - 1, st->pin_in1, st->ch_in1, st->pin_in2, st->ch_in2);
    return ESP_OK;
}

static esp_err_t drv8833_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    drv8833_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_SPEED_PERCENT) return ESP_ERR_NOT_SUPPORTED;

    int32_t speed;
    switch (cmd->type) {
        case VAL_TYPE_INT:   speed = cmd->i; break;
        case VAL_TYPE_FLOAT: speed = (int32_t)cmd->f; break;
        case VAL_TYPE_BOOL:  speed = cmd->b ? 100 : 0; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "DRV8833[%d] speed=%ld", idx, (long)speed);
    return drv8833_apply(st, speed);
}

static esp_err_t drv8833_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    drv8833_state_t *st = &s_state[idx];

    if (field != CAP_SPEED_PERCENT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_INT;
    out->i            = st->speed;
    out->capability   = CAP_SPEED_PERCENT;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t drv8833_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        drv8833_apply(&s_state[idx], 0);
        ledc_stop(DRV8833_MODE, s_state[idx].ch_in1, 0);
        ledc_stop(DRV8833_MODE, s_state[idx].ch_in2, 0);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t drv8833_meta = {
    .name             = "drv_drv8833",
    .display_name     = "DRV8833 Dual H-Bridge",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_SPEED_PERCENT},
    .num_capabilities = 1,
    .max_latency_us   = 100,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "H-bridge fault / overcurrent",
         {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(drv8833_state_t),
};

const driver_vtable_t drv_drv8833_vtable = {
    .init   = drv8833_init,
    .read   = drv8833_read,
    .write  = drv8833_write,
    .deinit = drv8833_deinit,
    .meta   = &drv8833_meta,
};
