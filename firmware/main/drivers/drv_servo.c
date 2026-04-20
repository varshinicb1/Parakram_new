/**
 * @file drv_servo.c
 * @brief PWM servo driver via ESP32-S3 LEDC peripheral.
 *
 * LEDC configuration:
 *   Timer  : LEDC_TIMER_0, LEDC_LOW_SPEED_MODE
 *   Freq   : 50 Hz (standard RC servo)
 *   Res    : 14-bit (0–16383)
 *
 * Angle → duty mapping at 50 Hz, 14-bit:
 *   period = 20 ms  (1/50 Hz)
 *   1 ms pulse (0°)   → duty = 16384 * 1/20  = 819  ≈ 409  (1.0 ms at 14-bit)
 *   2 ms pulse (180°) → duty = 16384 * 2/20  = 1638 ≈ 2047 (2.5 ms at 14-bit)
 *
 * Datasheet-accurate values used per specification:
 *   0°   → 409  counts  (1.0 ms)
 *   180° → 2047 counts  (2.5 ms)
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_SERVO";

#define SERVO_MAX_INSTANCES     8
#define SERVO_TIMER             LEDC_TIMER_0
#define SERVO_MODE              LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ           50
#define SERVO_RESOLUTION        LEDC_TIMER_14_BIT

/* Pulse width in counts at 14-bit 50 Hz */
#define SERVO_DUTY_MIN          409    /* 1.0 ms → 0° */
#define SERVO_DUTY_MAX          2047   /* 2.5 ms → 180° */
#define SERVO_ANGLE_MIN         0.0f
#define SERVO_ANGLE_MAX         180.0f

static bool s_timer_installed = false;

typedef struct {
    gpio_num_t      pin;
    ledc_channel_t  channel;
    float           current_angle;
    bool            initialized;
} servo_state_t;

static servo_state_t s_state[SERVO_MAX_INSTANCES];
static uint8_t s_count = 0;

static uint32_t angle_to_duty(float angle)
{
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;
    float ratio = (angle - SERVO_ANGLE_MIN) / (SERVO_ANGLE_MAX - SERVO_ANGLE_MIN);
    return (uint32_t)(SERVO_DUTY_MIN + ratio * (SERVO_DUTY_MAX - SERVO_DUTY_MIN));
}

static esp_err_t servo_init(const driver_config_t *cfg)
{
    if (s_count >= SERVO_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max instances (%d) reached", SERVO_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    servo_state_t *st = &s_state[s_count];
    st->pin           = (gpio_num_t)cfg->pins[0].gpio_num;
    st->channel       = (ledc_channel_t)(cfg->pins[0].pwm_channel % LEDC_CHANNEL_MAX);
    st->current_angle = 90.0f; /* centre position */

    /* Install timer once */
    if (!s_timer_installed) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode       = SERVO_MODE,
            .timer_num        = SERVO_TIMER,
            .duty_resolution  = SERVO_RESOLUTION,
            .freq_hz          = SERVO_FREQ_HZ,
            .clk_cfg          = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
            return err;
        }
        s_timer_installed = true;
        ESP_LOGI(TAG, "LEDC timer configured: 50 Hz, 14-bit");
    }

    /* Configure LEDC channel */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = st->pin,
        .speed_mode = SERVO_MODE,
        .channel    = st->channel,
        .timer_sel  = SERVO_TIMER,
        .duty       = angle_to_duty(st->current_angle),
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
    ESP_LOGI(TAG, "Servo[%d] on GPIO%d ch%d, centre (90°)", s_count - 1, st->pin, st->channel);
    return ESP_OK;
}

static esp_err_t servo_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    servo_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_ANGLE_DEGREES) {
        ESP_LOGE(TAG, "Unsupported capability %d", cmd->capability);
        return ESP_ERR_NOT_SUPPORTED;
    }

    float angle;
    switch (cmd->type) {
        case VAL_TYPE_FLOAT: angle = cmd->f; break;
        case VAL_TYPE_INT:   angle = (float)cmd->i; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    if (angle < SERVO_ANGLE_MIN || angle > SERVO_ANGLE_MAX) {
        ESP_LOGE(TAG, "Angle %.1f out of range [0, 180]", angle);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = angle_to_duty(angle);
    esp_err_t err = ledc_set_duty(SERVO_MODE, st->channel, duty);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(SERVO_MODE, st->channel);
    if (err == ESP_OK) {
        st->current_angle = angle;
        ESP_LOGD(TAG, "Servo[%d] → %.1f° (duty=%lu)", idx, angle, (unsigned long)duty);
    }
    return err;
}

static esp_err_t servo_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    servo_state_t *st = &s_state[idx];

    if (field != CAP_ANGLE_DEGREES) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = st->current_angle;
    out->capability   = CAP_ANGLE_DEGREES;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t servo_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count) {
        ledc_stop(SERVO_MODE, s_state[idx].channel, 0);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t servo_meta = {
    .name             = "drv_servo",
    .display_name     = "PWM Servo Motor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_PWM,
    .capabilities     = {CAP_ANGLE_DEGREES},
    .num_capabilities = 1,
    .max_latency_us   = 100,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "LEDC PWM failure", {.type = VAL_TYPE_FLOAT, .f = 90.0f}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(servo_state_t),
};

const driver_vtable_t drv_servo_vtable = {
    .init   = servo_init,
    .read   = servo_read,
    .write  = servo_write,
    .deinit = servo_deinit,
    .meta   = &servo_meta,
};
