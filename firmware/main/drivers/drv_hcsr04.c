/**
 * @file drv_hcsr04.c
 * @brief HC-SR04 ultrasonic distance sensor driver.
 *
 * Trigger pin: output.  Echo pin: input.
 * Measurement cycle:
 *   1. Assert TRIG high for 10 µs.
 *   2. Wait for ECHO to go high.
 *   3. Measure duration ECHO stays high (µs).
 *   4. Distance [cm] = pulse_width_us / 58.0f.
 *
 * Max range ~400 cm, min ~2 cm.  No-echo timeout → error.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "DRV_HCSR04";

#define HCSR04_MAX_INSTANCES    4
#define HCSR04_TIMEOUT_US       30000   /* ~5 m round-trip + margin */

typedef struct {
    gpio_num_t  trig_pin;
    gpio_num_t  echo_pin;
    bool        initialized;
    float       distance_cm;
} hcsr04_state_t;

static hcsr04_state_t s_state[HCSR04_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t hcsr04_init(const driver_config_t *cfg)
{
    if (s_instance_count >= HCSR04_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    hcsr04_state_t *st = &s_state[s_instance_count];
    st->trig_pin = (gpio_num_t)cfg->pins[0].gpio_num;
    st->echo_pin = (gpio_num_t)cfg->pins[1].gpio_num;

    /* Configure TRIG as output */
    gpio_config_t trig_cfg = {
        .pin_bit_mask  = (1ULL << st->trig_pin),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&trig_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TRIG GPIO config failed: %d", err);
        return err;
    }
    gpio_set_level(st->trig_pin, 0);

    /* Configure ECHO as input */
    gpio_config_t echo_cfg = {
        .pin_bit_mask  = (1ULL << st->echo_pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&echo_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ECHO GPIO config failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "HC-SR04 initialized TRIG=GPIO%d ECHO=GPIO%d",
             st->trig_pin, st->echo_pin);
    return ESP_OK;
}

static esp_err_t hcsr04_sample(hcsr04_state_t *st)
{
    /* 10 µs trigger pulse */
    gpio_set_level(st->trig_pin, 0);
    esp_rom_delay_us(2);
    gpio_set_level(st->trig_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(st->trig_pin, 0);

    /* Wait for ECHO rising edge */
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(st->echo_pin) == 0) {
        if ((esp_timer_get_time() - t0) > HCSR04_TIMEOUT_US) {
            ESP_LOGD(TAG, "Timeout waiting for ECHO high");
            return ESP_ERR_TIMEOUT;
        }
    }

    /* Measure ECHO pulse width */
    int64_t t_start = esp_timer_get_time();
    while (gpio_get_level(st->echo_pin) == 1) {
        if ((esp_timer_get_time() - t_start) > HCSR04_TIMEOUT_US) {
            ESP_LOGD(TAG, "Timeout waiting for ECHO low");
            return ESP_ERR_TIMEOUT;
        }
    }
    int64_t pulse_us = esp_timer_get_time() - t_start;

    st->distance_cm = (float)pulse_us / 58.0f;
    return ESP_OK;
}

static esp_err_t hcsr04_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    hcsr04_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_DISTANCE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = hcsr04_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_TIMEOUT;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = CAP_DISTANCE;
    out->f            = st->distance_cm;
    return ESP_OK;
}

static esp_err_t hcsr04_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        gpio_set_level(s_state[h.driver_index].trig_pin, 0);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t hcsr04_meta = {
    .name             = "drv_hcsr04",
    .display_name     = "HC-SR04 Ultrasonic Distance",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_DISTANCE},
    .num_capabilities = 1,
    .max_latency_us   = 35000,
    .min_interval_ms  = 60,
    .failure_modes    = {
        {DRV_ERR_TIMEOUT, "No echo received (no object in range)", {.type=VAL_TYPE_FLOAT, .f=-1}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(hcsr04_state_t),
};

const driver_vtable_t drv_hcsr04_vtable = {
    .init   = hcsr04_init,
    .read   = hcsr04_read,
    .write  = NULL,
    .deinit = hcsr04_deinit,
    .meta   = &hcsr04_meta,
};
