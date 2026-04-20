/**
 * @file drv_motor_dc.c
 * @brief DC motor driver for L298N / generic H-bridge.
 *
 * Pin layout (cfg->pins[]):
 *   pins[0] – IN1 GPIO   (direction A)
 *   pins[1] – IN2 GPIO   (direction B)
 *   pins[2] – ENA PWM    (speed, uses cfg->pins[2].pwm_channel)
 *
 * Logic table:
 *   Forward : IN1=1, IN2=0, ENA = |speed|%
 *   Reverse : IN1=0, IN2=1, ENA = |speed|%
 *   Stop    : IN1=0, IN2=0, ENA = 0
 *
 * Write CAP_SPEED_PERCENT: int32 or float -100 … +100
 *   Positive → forward, negative → reverse, 0 → stop (coast).
 *
 * LEDC timer 2, 1 kHz, 10-bit resolution.
 * Max 4 instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_MOTOR_DC";

#define MOTOR_DC_MAX_INSTANCES  4
#define MOTOR_DC_TIMER          LEDC_TIMER_2
#define MOTOR_DC_MODE           LEDC_LOW_SPEED_MODE
#define MOTOR_DC_FREQ_HZ        1000U
#define MOTOR_DC_RESOLUTION     LEDC_TIMER_10_BIT  /* 0–1023 */

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pin_in1;
    gpio_num_t      pin_in2;
    gpio_num_t      pin_ena;
    ledc_channel_t  channel;
    int32_t         speed;      /* -100 … +100 */
    bool            initialized;
} motor_dc_state_t;

static motor_dc_state_t s_state[MOTOR_DC_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t motor_apply(motor_dc_state_t *st, int32_t speed)
{
    if (speed > 100)  speed =  100;
    if (speed < -100) speed = -100;

    int32_t abs_speed = speed < 0 ? -speed : speed;
    /* Map 0-100 → 0-1023 */
    uint32_t duty = (uint32_t)(abs_speed * 1023 / 100);

    if (speed > 0) {
        gpio_set_level(st->pin_in1, 1);
        gpio_set_level(st->pin_in2, 0);
    } else if (speed < 0) {
        gpio_set_level(st->pin_in1, 0);
        gpio_set_level(st->pin_in2, 1);
    } else {
        gpio_set_level(st->pin_in1, 0);
        gpio_set_level(st->pin_in2, 0);
        duty = 0;
    }

    esp_err_t err = ledc_set_duty(MOTOR_DC_MODE, st->channel, duty);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(MOTOR_DC_MODE, st->channel);
    if (err == ESP_OK) st->speed = speed;
    return err;
}

static esp_err_t motor_dc_init(const driver_config_t *cfg)
{
    if (s_count >= MOTOR_DC_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max DC motor instances (%d) reached", MOTOR_DC_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    motor_dc_state_t *st = &s_state[s_count];
    st->pin_in1 = (gpio_num_t)cfg->pins[0].gpio_num;
    st->pin_in2 = (gpio_num_t)cfg->pins[1].gpio_num;
    st->pin_ena = (gpio_num_t)cfg->pins[2].gpio_num;
    st->channel = (ledc_channel_t)(cfg->pins[2].pwm_channel % LEDC_CHANNEL_MAX);
    st->speed   = 0;

    /* Configure direction GPIO pins */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->pin_in1) | (1ULL << st->pin_in2),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", err);
        return err;
    }
    gpio_set_level(st->pin_in1, 0);
    gpio_set_level(st->pin_in2, 0);

    /* Install LEDC timer once */
    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = MOTOR_DC_MODE,
            .timer_num       = MOTOR_DC_TIMER,
            .duty_resolution = MOTOR_DC_RESOLUTION,
            .freq_hz         = MOTOR_DC_FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
    }

    /* Configure ENA channel */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = st->pin_ena,
        .speed_mode = MOTOR_DC_MODE,
        .channel    = st->channel,
        .timer_sel  = MOTOR_DC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "DC Motor[%d]: IN1=GPIO%d IN2=GPIO%d ENA=GPIO%d ch%d",
             s_count - 1, st->pin_in1, st->pin_in2, st->pin_ena, st->channel);
    return ESP_OK;
}

static esp_err_t motor_dc_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    motor_dc_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_SPEED_PERCENT) return ESP_ERR_NOT_SUPPORTED;

    int32_t speed;
    switch (cmd->type) {
        case VAL_TYPE_INT:   speed = cmd->i; break;
        case VAL_TYPE_FLOAT: speed = (int32_t)cmd->f; break;
        case VAL_TYPE_BOOL:  speed = cmd->b ? 100 : 0; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "DC Motor[%d] speed=%ld", idx, (long)speed);
    return motor_apply(st, speed);
}

static esp_err_t motor_dc_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    motor_dc_state_t *st = &s_state[idx];

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

static esp_err_t motor_dc_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        motor_apply(&s_state[idx], 0);
        ledc_stop(MOTOR_DC_MODE, s_state[idx].channel, 0);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t motor_dc_meta = {
    .name             = "drv_motor_dc",
    .display_name     = "DC Motor (H-Bridge)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_PWM,
    .capabilities     = {CAP_SPEED_PERCENT},
    .num_capabilities = 1,
    .max_latency_us   = 200,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "H-bridge fault", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(motor_dc_state_t),
};

const driver_vtable_t drv_motor_dc_vtable = {
    .init   = motor_dc_init,
    .read   = motor_dc_read,
    .write  = motor_dc_write,
    .deinit = motor_dc_deinit,
    .meta   = &motor_dc_meta,
};
