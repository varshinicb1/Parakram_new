/**
 * @file drv_motor_stepper.c
 * @brief 4-wire unipolar/bipolar stepper motor driver (half-step, 8 phases).
 *
 * Pin layout (cfg->pins[0..3]): coil outputs A, B, C, D.
 *
 * Half-step sequence (8 phases):
 *   Phase  A B C D
 *     0    1 0 0 0
 *     1    1 1 0 0
 *     2    0 1 0 0
 *     3    0 1 1 0
 *     4    0 0 1 0
 *     5    0 0 1 1
 *     6    0 0 0 1
 *     7    1 0 0 1
 *
 * Write CAP_COUNT: int32 step count.
 *   Positive → clockwise (phase advances).
 *   Negative → counter-clockwise (phase retreats).
 *
 * Step delay derived from cfg->sample_rate_hz (steps per second).
 * Default 100 steps/sec if sample_rate_hz == 0.
 * Position tracked as int32_t cumulative steps from power-on.
 * Max 4 stepper instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "DRV_STEPPER";

#define STEPPER_MAX_INSTANCES   4
#define STEPPER_PHASES          8
#define STEPPER_DEFAULT_SPD     100    /* steps / second */

static const uint8_t s_half_step[STEPPER_PHASES][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

typedef struct {
    gpio_num_t  pins[4];
    int32_t     position;       /* cumulative step count from home */
    int32_t     phase;          /* current phase index 0–7 */
    uint32_t    step_delay_us;  /* inter-step delay in µs */
    bool        initialized;
} stepper_state_t;

static stepper_state_t s_state[STEPPER_MAX_INSTANCES];
static uint8_t s_count = 0;

static void stepper_apply_phase(stepper_state_t *st)
{
    int32_t ph = ((st->phase % STEPPER_PHASES) + STEPPER_PHASES) % STEPPER_PHASES;
    for (int i = 0; i < 4; i++) {
        gpio_set_level(st->pins[i], s_half_step[ph][i]);
    }
}

static void stepper_coils_off(stepper_state_t *st)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(st->pins[i], 0);
    }
}

static esp_err_t stepper_init(const driver_config_t *cfg)
{
    if (s_count >= STEPPER_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max stepper instances (%d) reached", STEPPER_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    stepper_state_t *st = &s_state[s_count];
    st->position = 0;
    st->phase    = 0;

    float rate = cfg->sample_rate_hz;
    if (rate <= 0.0f) rate = (float)STEPPER_DEFAULT_SPD;
    st->step_delay_us = (uint32_t)(1000000.0f / rate);
    if (st->step_delay_us < 500) st->step_delay_us = 500; /* hardware min */

    uint64_t pin_mask = 0;
    for (int i = 0; i < 4; i++) {
        st->pins[i] = (gpio_num_t)cfg->pins[i].gpio_num;
        pin_mask |= (1ULL << st->pins[i]);
    }

    gpio_config_t io = {
        .pin_bit_mask = pin_mask,
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

    stepper_coils_off(st);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Stepper[%d]: A=GPIO%d B=GPIO%d C=GPIO%d D=GPIO%d delay=%luµs",
             s_count - 1, st->pins[0], st->pins[1], st->pins[2], st->pins[3],
             (unsigned long)st->step_delay_us);
    return ESP_OK;
}

static esp_err_t stepper_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    stepper_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_COUNT) return ESP_ERR_NOT_SUPPORTED;

    int32_t steps;
    switch (cmd->type) {
        case VAL_TYPE_INT:   steps = cmd->i; break;
        case VAL_TYPE_FLOAT: steps = (int32_t)cmd->f; break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    int32_t direction = (steps >= 0) ? 1 : -1;
    int32_t abs_steps = steps < 0 ? -steps : steps;

    ESP_LOGD(TAG, "Stepper[%d] %ld steps (%s)", idx, (long)steps,
             direction > 0 ? "CW" : "CCW");

    for (int32_t s = 0; s < abs_steps; s++) {
        st->phase += direction;
        if (st->phase < 0)               st->phase = STEPPER_PHASES - 1;
        if (st->phase >= STEPPER_PHASES) st->phase = 0;
        stepper_apply_phase(st);
        st->position += direction;
        esp_rom_delay_us(st->step_delay_us);
    }

    /* De-energise coils after move to reduce heat */
    stepper_coils_off(st);
    return ESP_OK;
}

static esp_err_t stepper_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    stepper_state_t *st = &s_state[idx];

    if (field != CAP_COUNT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_INT;
    out->i            = st->position;
    out->capability   = CAP_COUNT;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t stepper_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        stepper_coils_off(&s_state[idx]);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t stepper_meta = {
    .name             = "drv_motor_stepper",
    .display_name     = "4-Wire Stepper Motor (Half-Step)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_COUNT},
    .num_capabilities = 1,
    .max_latency_us   = 0,      /* latency depends on step count — unbounded */
    .min_interval_ms  = 0,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "Coil drive failure", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(stepper_state_t),
};

const driver_vtable_t drv_motor_stepper_vtable = {
    .init   = stepper_init,
    .read   = stepper_read,
    .write  = stepper_write,
    .deinit = stepper_deinit,
    .meta   = &stepper_meta,
};
