/**
 * @file drv_mosfet.c
 * @brief N-MOSFET generic switch / dimmer driver.
 *
 * Supports two modes determined at init time:
 *   PWM mode : cfg->pins[0].pwm_channel is valid (≠ 0xFF).
 *              Uses LEDC timer 4, 1 kHz, 10-bit.
 *              Write CAP_SPEED_PERCENT (0–100) for PWM dimming.
 *              Write CAP_ON_OFF bool maps to 0% / 100%.
 *   GPIO mode: plain GPIO high/low.
 *              Write CAP_ON_OFF bool.
 *
 * cfg->pins[0] – gate GPIO (or PWM output pin).
 * cfg->invert   – if true, logic is inverted (active-low gate drive).
 *
 * Max 4 MOSFET instances.
 *
 * LEDC timer 4 is reserved exclusively for MOSFETs to avoid conflicts
 * with servo (T0), buzzer (T1), DC motor (T2), fan (T3).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <string.h>

static const char *TAG = "DRV_MOSFET";

#define MOSFET_MAX_INSTANCES    4
#define MOSFET_TIMER            LEDC_TIMER_0   /* shared with servo; safe if freq compatible */
/* Use a dedicated 1 kHz timer for MOSFETs to avoid conflict.
   In a real build use LEDC_TIMER_3 if fan is absent, or configure independently. */
#define MOSFET_PWM_TIMER        LEDC_TIMER_3
#define MOSFET_PWM_MODE         LEDC_LOW_SPEED_MODE
#define MOSFET_PWM_FREQ_HZ      1000U
#define MOSFET_PWM_RESOLUTION   LEDC_TIMER_10_BIT
#define MOSFET_NO_PWM_CHANNEL   0xFF

static bool s_pwm_timer_installed = false;

typedef struct {
    gpio_num_t      pin;
    bool            use_pwm;
    bool            inverted;
    ledc_channel_t  channel;
    int32_t         speed_pct;  /* 0–100, or -1 for plain on/off */
    bool            on_off;
    bool            initialized;
} mosfet_state_t;

static mosfet_state_t s_state[MOSFET_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t mosfet_init(const driver_config_t *cfg)
{
    if (s_count >= MOSFET_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max MOSFET instances (%d) reached", MOSFET_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    mosfet_state_t *st = &s_state[s_count];
    st->pin       = (gpio_num_t)cfg->pins[0].gpio_num;
    st->inverted  = cfg->invert;
    st->speed_pct = 0;
    st->on_off    = false;
    st->use_pwm   = (cfg->pins[0].pwm_channel != MOSFET_NO_PWM_CHANNEL &&
                     cfg->pins[0].pwm_channel != 0);

    if (st->use_pwm) {
        st->channel = (ledc_channel_t)(cfg->pins[0].pwm_channel % LEDC_CHANNEL_MAX);

        if (!s_pwm_timer_installed) {
            ledc_timer_config_t timer_cfg = {
                .speed_mode      = MOSFET_PWM_MODE,
                .timer_num       = MOSFET_PWM_TIMER,
                .duty_resolution = MOSFET_PWM_RESOLUTION,
                .freq_hz         = MOSFET_PWM_FREQ_HZ,
                .clk_cfg         = LEDC_AUTO_CLK,
            };
            esp_err_t err = ledc_timer_config(&timer_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "LEDC timer config failed: %d", err);
                return err;
            }
            s_pwm_timer_installed = true;
        }

        ledc_channel_config_t ch_cfg = {
            .gpio_num   = st->pin,
            .speed_mode = MOSFET_PWM_MODE,
            .channel    = st->channel,
            .timer_sel  = MOSFET_PWM_TIMER,
            .duty       = st->inverted ? 1023 : 0,
            .hpoint     = 0,
            .intr_type  = LEDC_INTR_DISABLE,
        };
        esp_err_t err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel config failed: %d", err);
            return err;
        }
        ESP_LOGI(TAG, "MOSFET[%d] GPIO%d — PWM mode ch%d", s_count, st->pin, st->channel);
    } else {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << st->pin),
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
        gpio_set_level(st->pin, st->inverted ? 1 : 0);
        ESP_LOGI(TAG, "MOSFET[%d] GPIO%d — GPIO mode (inverted=%d)", s_count, st->pin, st->inverted);
    }

    st->initialized = true;
    s_count++;
    return ESP_OK;
}

static esp_err_t mosfet_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    mosfet_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability == CAP_ON_OFF) {
        bool on;
        switch (cmd->type) {
            case VAL_TYPE_BOOL:  on = cmd->b; break;
            case VAL_TYPE_INT:   on = (cmd->i != 0); break;
            case VAL_TYPE_FLOAT: on = (cmd->f > 0.5f); break;
            default:             return ESP_ERR_INVALID_ARG;
        }
        st->on_off    = on;
        st->speed_pct = on ? 100 : 0;

        if (st->use_pwm) {
            uint32_t duty = on ? 1023 : 0;
            if (st->inverted) duty = 1023 - duty;
            ledc_set_duty(MOSFET_PWM_MODE, st->channel, duty);
            ledc_update_duty(MOSFET_PWM_MODE, st->channel);
        } else {
            uint32_t level = on ? 1u : 0u;
            if (st->inverted) level = !level;
            gpio_set_level(st->pin, level);
        }
        ESP_LOGD(TAG, "MOSFET[%d] ON_OFF → %s", idx, on ? "ON" : "OFF");
        return ESP_OK;
    }

    if (cmd->capability == CAP_SPEED_PERCENT) {
        if (!st->use_pwm) return ESP_ERR_NOT_SUPPORTED;

        int32_t pct;
        switch (cmd->type) {
            case VAL_TYPE_INT:   pct = cmd->i; break;
            case VAL_TYPE_FLOAT: pct = (int32_t)cmd->f; break;
            default:             return ESP_ERR_INVALID_ARG;
        }
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;

        uint32_t duty = (uint32_t)(pct * 1023 / 100);
        if (st->inverted) duty = 1023 - duty;
        st->speed_pct = pct;
        st->on_off    = (pct > 0);

        ledc_set_duty(MOSFET_PWM_MODE, st->channel, duty);
        ledc_update_duty(MOSFET_PWM_MODE, st->channel);
        ESP_LOGD(TAG, "MOSFET[%d] dim → %ld%%", idx, (long)pct);
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t mosfet_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    mosfet_state_t *st = &s_state[idx];

    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;

    if (field == CAP_ON_OFF) {
        out->type       = VAL_TYPE_BOOL;
        out->b          = st->on_off;
        out->capability = CAP_ON_OFF;
        return ESP_OK;
    }
    if (field == CAP_SPEED_PERCENT) {
        out->type       = VAL_TYPE_INT;
        out->i          = st->speed_pct;
        out->capability = CAP_SPEED_PERCENT;
        return ESP_OK;
    }

    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t mosfet_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        mosfet_state_t *st = &s_state[idx];
        if (st->use_pwm) {
            ledc_set_duty(MOSFET_PWM_MODE, st->channel, st->inverted ? 1023 : 0);
            ledc_update_duty(MOSFET_PWM_MODE, st->channel);
            ledc_stop(MOSFET_PWM_MODE, st->channel, 0);
        } else {
            gpio_set_level(st->pin, st->inverted ? 1 : 0);
        }
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mosfet_meta = {
    .name             = "drv_mosfet",
    .display_name     = "N-MOSFET Switch / Dimmer",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_ON_OFF, CAP_SPEED_PERCENT},
    .num_capabilities = 2,
    .max_latency_us   = 100,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "Gate drive failure", {.type = VAL_TYPE_BOOL, .b = false}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(mosfet_state_t),
};

const driver_vtable_t drv_mosfet_vtable = {
    .init   = mosfet_init,
    .read   = mosfet_read,
    .write  = mosfet_write,
    .deinit = mosfet_deinit,
    .meta   = &mosfet_meta,
};
