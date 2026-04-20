/**
 * @file drv_tb6612.c
 * @brief TB6612FNG dual DC motor driver.
 *
 * The TB6612FNG drives two independent DC motors (Motor A and Motor B).
 * Each motor uses 2 direction GPIO pins + 1 PWM pin.
 * An optional STBY (standby) pin enables/disables both H-bridges.
 *
 * Pin layout (cfg->pins[]):
 *   pins[0] – AIN1  (Motor A direction 1)
 *   pins[1] – AIN2  (Motor A direction 2)
 *   pins[2] – PWMA  (Motor A speed PWM, pwm_channel)
 *   pins[3] – STBY  (standby, active-low; optional — set gpio_num=-1 to skip)
 *
 * For Motor B, a second instance is created using the same driver with:
 *   pins[0] – BIN1, pins[1] – BIN2, pins[2] – PWMB
 *
 * LEDC timer 2 (shared with drv_motor_dc), 1 kHz, 10-bit.
 *
 * Logic table per channel:
 *   Forward : IN1=1, IN2=0
 *   Reverse : IN1=0, IN2=1
 *   Short brake: IN1=1, IN2=1, PWM=0 (duty=0)
 *   Stop (coast): IN1=0, IN2=0, PWM=0
 *
 * Write CAP_SPEED_PERCENT: int32 or float -100…+100.
 * Read  CAP_SPEED_PERCENT: returns current speed.
 *
 * Max 8 instances (4 dual-motor boards, each board = 2 instances).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_TB6612";

#define TB6612_MAX_INSTANCES    8
#define TB6612_TIMER            LEDC_TIMER_2
#define TB6612_MODE             LEDC_LOW_SPEED_MODE
#define TB6612_FREQ_HZ          1000U
#define TB6612_RESOLUTION       LEDC_TIMER_10_BIT   /* 0–1023 */
#define TB6612_NO_PIN           (-1)

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pin_in1;
    gpio_num_t      pin_in2;
    gpio_num_t      pin_pwm;
    gpio_num_t      pin_stby;  /* -1 = not connected */
    ledc_channel_t  channel;
    int32_t         speed;     /* -100…+100 */
    bool            initialized;
} tb6612_state_t;

static tb6612_state_t s_state[TB6612_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t tb6612_apply(tb6612_state_t *st, int32_t speed)
{
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;

    int32_t abs_spd = speed < 0 ? -speed : speed;
    uint32_t duty   = (uint32_t)(abs_spd * 1023 / 100);

    if (speed > 0) {
        gpio_set_level(st->pin_in1, 1);
        gpio_set_level(st->pin_in2, 0);
    } else if (speed < 0) {
        gpio_set_level(st->pin_in1, 0);
        gpio_set_level(st->pin_in2, 1);
    } else {
        /* Coast stop */
        gpio_set_level(st->pin_in1, 0);
        gpio_set_level(st->pin_in2, 0);
        duty = 0;
    }

    esp_err_t err = ledc_set_duty(TB6612_MODE, st->channel, duty);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(TB6612_MODE, st->channel);
    if (err == ESP_OK) st->speed = speed;
    return err;
}

static esp_err_t tb6612_init(const driver_config_t *cfg)
{
    if (s_count >= TB6612_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max TB6612 instances (%d) reached", TB6612_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    tb6612_state_t *st = &s_state[s_count];
    st->pin_in1  = (gpio_num_t)cfg->pins[0].gpio_num;
    st->pin_in2  = (gpio_num_t)cfg->pins[1].gpio_num;
    st->pin_pwm  = (gpio_num_t)cfg->pins[2].gpio_num;
    st->pin_stby = (cfg->pin_count >= 4) ? (gpio_num_t)cfg->pins[3].gpio_num : (gpio_num_t)TB6612_NO_PIN;
    st->channel  = (ledc_channel_t)(cfg->pins[2].pwm_channel % LEDC_CHANNEL_MAX);
    st->speed    = 0;

    /* Configure direction GPIO */
    uint64_t mask = (1ULL << st->pin_in1) | (1ULL << st->pin_in2);
    if (st->pin_stby >= 0) mask |= (1ULL << st->pin_stby);

    gpio_config_t io = {
        .pin_bit_mask = mask,
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
    if (st->pin_stby >= 0) {
        gpio_set_level(st->pin_stby, 1); /* STBY high = active (not in standby) */
    }

    /* LEDC timer — share with drv_motor_dc (same freq/res) */
    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = TB6612_MODE,
            .timer_num       = TB6612_TIMER,
            .duty_resolution = TB6612_RESOLUTION,
            .freq_hz         = TB6612_FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = st->pin_pwm,
        .speed_mode = TB6612_MODE,
        .channel    = st->channel,
        .timer_sel  = TB6612_TIMER,
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
    ESP_LOGI(TAG, "TB6612[%d]: IN1=GPIO%d IN2=GPIO%d PWM=GPIO%d ch%d STBY=GPIO%d",
             s_count - 1, st->pin_in1, st->pin_in2, st->pin_pwm, st->channel, st->pin_stby);
    return ESP_OK;
}

static esp_err_t tb6612_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    tb6612_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_SPEED_PERCENT) return ESP_ERR_NOT_SUPPORTED;

    int32_t speed;
    switch (cmd->type) {
        case VAL_TYPE_INT:   speed = cmd->i; break;
        case VAL_TYPE_FLOAT: speed = (int32_t)cmd->f; break;
        case VAL_TYPE_BOOL:  speed = cmd->b ? 100 : 0; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "TB6612[%d] speed=%ld", idx, (long)speed);
    return tb6612_apply(st, speed);
}

static esp_err_t tb6612_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    tb6612_state_t *st = &s_state[idx];

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

static esp_err_t tb6612_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        tb6612_apply(&s_state[idx], 0);
        if (s_state[idx].pin_stby >= 0) {
            gpio_set_level(s_state[idx].pin_stby, 0); /* pull STBY low = standby */
        }
        ledc_stop(TB6612_MODE, s_state[idx].channel, 0);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t tb6612_meta = {
    .name             = "drv_tb6612",
    .display_name     = "TB6612FNG Dual DC Motor Driver",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_PWM,
    .capabilities     = {CAP_SPEED_PERCENT},
    .num_capabilities = 1,
    .max_latency_us   = 200,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "H-bridge overcurrent / thermal shutdown",
         {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(tb6612_state_t),
};

const driver_vtable_t drv_tb6612_vtable = {
    .init   = tb6612_init,
    .read   = tb6612_read,
    .write  = tb6612_write,
    .deinit = tb6612_deinit,
    .meta   = &tb6612_meta,
};
